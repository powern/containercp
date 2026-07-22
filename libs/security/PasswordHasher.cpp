#include "PasswordHasher.h"

#include "security/SecureRandom.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>

#include <charconv>
#include <sstream>
#include <string_view>
#include <vector>

#ifdef CONTAINERCP_HAS_ARGON2
#include <argon2.h>
#endif

namespace containercp::security {

namespace {

#ifdef CONTAINERCP_HAS_ARGON2
constexpr uint32_t kArgonMemoryKiB = 65536;
constexpr uint32_t kArgonIterations = 3;
constexpr uint32_t kArgonParallelism = 1;
constexpr std::size_t kArgonSaltBytes = 16;
constexpr std::size_t kArgonHashBytes = 32;
#else
constexpr int kPbkdf2Iterations = 210000;
constexpr std::size_t kPbkdf2SaltBytes = 16;
constexpr std::size_t kPbkdf2HashBytes = 32;
const std::string kPbkdf2Prefix = "$containercp-pbkdf2-sha256$v=1$i=";
#endif

const std::string kBase64Chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const std::vector<unsigned char>& data) {
    std::string out;
    int val = 0;
    int valb = -6;
    for (unsigned char c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(kBase64Chars[static_cast<std::size_t>((val >> valb) & 0x3f)]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(kBase64Chars[static_cast<std::size_t>(((val << 8) >> (valb + 8)) & 0x3f)]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::vector<unsigned char> base64_decode(const std::string& input) {
    std::vector<int> table(256, -1);
    for (std::size_t i = 0; i < kBase64Chars.size(); ++i) {
        table[static_cast<unsigned char>(kBase64Chars[i])] = static_cast<int>(i);
    }
    std::vector<unsigned char> out;
    int val = 0;
    int valb = -8;
    for (unsigned char c : input) {
        if (c == '=') break;
        if (table[c] == -1) return {};
        val = (val << 6) + table[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<unsigned char>((val >> valb) & 0xff));
            valb -= 8;
        }
    }
    return out;
}

bool parse_positive_int(std::string_view s, int& out) {
    auto begin = s.data();
    auto end = s.data() + s.size();
    int value = 0;
    auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc() || ptr != end || value <= 0) return false;
    out = value;
    return true;
}

} // namespace

std::string PasswordHasher::hash(const std::string& password) {
#ifdef CONTAINERCP_HAS_ARGON2
    auto salt = SecureRandom::bytes(kArgonSaltBytes);
    if (!salt) return "";
    const std::size_t encoded_len = argon2_encodedlen(
        kArgonIterations, kArgonMemoryKiB, kArgonParallelism,
        kArgonSaltBytes, kArgonHashBytes, Argon2_id);
    std::string encoded(encoded_len, '\0');
    int rc = argon2id_hash_encoded(kArgonIterations, kArgonMemoryKiB, kArgonParallelism,
        password.data(), password.size(), salt->data(), salt->size(),
        kArgonHashBytes, encoded.data(), encoded.size());
    if (rc != ARGON2_OK) return "";
    if (!encoded.empty() && encoded.back() == '\0') encoded.pop_back();
    return encoded;
#else
    auto salt = SecureRandom::bytes(kPbkdf2SaltBytes);
    if (!salt) return "";
    std::vector<unsigned char> digest(kPbkdf2HashBytes);
    if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()),
            salt->data(), static_cast<int>(salt->size()), kPbkdf2Iterations,
            EVP_sha256(), static_cast<int>(digest.size()), digest.data()) != 1) {
        return "";
    }
    return kPbkdf2Prefix + std::to_string(kPbkdf2Iterations)
        + "$l=" + std::to_string(kPbkdf2HashBytes)
        + "$" + base64_encode(*salt)
        + "$" + base64_encode(digest);
#endif
}

bool PasswordHasher::verify(const std::string& password, const std::string& encoded_hash) {
    if (!is_supported_hash(encoded_hash)) return false;
#ifdef CONTAINERCP_HAS_ARGON2
    return argon2id_verify(encoded_hash.c_str(), password.data(), password.size()) == ARGON2_OK;
#else
    std::vector<std::string> parts;
    std::stringstream ss(encoded_hash);
    std::string part;
    while (std::getline(ss, part, '$')) parts.push_back(part);
    if (parts.size() != 7 || !parts[0].empty() || parts[1] != "containercp-pbkdf2-sha256" || parts[2] != "v=1") return false;
    if (parts[3].rfind("i=", 0) != 0 || parts[4].rfind("l=", 0) != 0) return false;
    int iterations = 0;
    int length = 0;
    if (!parse_positive_int(std::string_view(parts[3]).substr(2), iterations)) return false;
    if (!parse_positive_int(std::string_view(parts[4]).substr(2), length)) return false;
    auto salt = base64_decode(parts[5]);
    auto expected = base64_decode(parts[6]);
    if (salt.empty() || expected.empty() || expected.size() != static_cast<std::size_t>(length)) return false;
    std::vector<unsigned char> actual(expected.size());
    if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()),
            salt.data(), static_cast<int>(salt.size()), iterations,
            EVP_sha256(), static_cast<int>(actual.size()), actual.data()) != 1) {
        return false;
    }
    return CRYPTO_memcmp(actual.data(), expected.data(), expected.size()) == 0;
#endif
}

bool PasswordHasher::is_supported_hash(const std::string& encoded_hash) {
#ifdef CONTAINERCP_HAS_ARGON2
    return encoded_hash.rfind("$argon2id$", 0) == 0;
#else
    return encoded_hash.rfind(kPbkdf2Prefix, 0) == 0;
#endif
}

const char* PasswordHasher::backend_name() {
#ifdef CONTAINERCP_HAS_ARGON2
    return "argon2id";
#else
    return "pbkdf2-sha256";
#endif
}

} // namespace containercp::security
