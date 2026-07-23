#include "access/SshKeyValidator.h"

#include <openssl/evp.h>
#include <openssl/sha.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector>

namespace containercp::access {
namespace {

constexpr std::size_t kMaxInputLength = 8192;
constexpr std::size_t kMinRsaModulusBits = 2048;
constexpr std::size_t kMinRsaModulusBytes = kMinRsaModulusBits / 8;
constexpr std::size_t kMaxKeyCommentLength = 256;

const char* kSupportedAlgorithms[] = {
    "ssh-ed25519",
    "ecdsa-sha2-nistp256",
    "ecdsa-sha2-nistp384",
    "ecdsa-sha2-nistp521",
    "ssh-rsa",
    nullptr
};

bool is_supported_algorithm(const std::string& algo) {
    for (std::size_t i = 0; kSupportedAlgorithms[i] != nullptr; ++i) {
        if (algo == kSupportedAlgorithms[i]) return true;
    }
    return false;
}

bool is_valid_base64_char(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
}

uint32_t read_uint32_be(const unsigned char*& p, const unsigned char* end) {
    if (p + 4 > end) return 0;
    uint32_t v = (static_cast<uint32_t>(p[0]) << 24) |
                 (static_cast<uint32_t>(p[1]) << 16) |
                 (static_cast<uint32_t>(p[2]) << 8) |
                 static_cast<uint32_t>(p[3]);
    p += 4;
    return v;
}

std::string read_string(const unsigned char*& p, const unsigned char* end) {
    uint32_t len = read_uint32_be(p, end);
    if (len == 0 && p - 4 == end) return {};
    if (p + len > end) return {};
    std::string s(reinterpret_cast<const char*>(p), len);
    p += len;
    return s;
}

bool validate_ed25519(const unsigned char* p, const unsigned char* end) {
    std::string key_data = read_string(p, end);
    return !key_data.empty() && key_data.size() == 32 && p == end;
}

bool validate_rsa(const unsigned char*& p, const unsigned char* end) {
    std::string e_str = read_string(p, end);
    std::string n_str = read_string(p, end);
    if (e_str.empty() || n_str.empty() || p != end) return false;
    return n_str.size() >= kMinRsaModulusBytes;
}

bool validate_ecdsa(const unsigned char*& p, const unsigned char* end) {
    std::string curve = read_string(p, end);
    std::string point = read_string(p, end);
    if (curve.empty() || point.empty() || p != end) return false;
    return true;
}

bool validate_key_blob(const std::string& key_type,
                       const unsigned char* data,
                       std::size_t size,
                       std::string& error) {
    const unsigned char* p = data;
    const unsigned char* end = data + size;

    std::string embedded_type = read_string(p, end);
    if (embedded_type.empty()) {
        error = "malformed key blob: cannot read key type";
        return false;
    }
    if (embedded_type != key_type) {
        error = "key type mismatch: expected " + key_type + ", blob contains " + embedded_type;
        return false;
    }

    if (key_type == "ssh-ed25519") {
        if (!validate_ed25519(p, end)) {
            error = "invalid ed25519 key: expected 32-byte public key";
            return false;
        }
    } else if (key_type == "ssh-rsa") {
        if (!validate_rsa(p, end)) {
            error = "invalid RSA key: key too short (minimum " +
                    std::to_string(kMinRsaModulusBits) + " bits)";
            return false;
        }
    } else if (key_type.rfind("ecdsa-sha2-", 0) == 0) {
        if (!validate_ecdsa(p, end)) {
            error = "invalid ECDSA key: malformed curve or point data";
            return false;
        }
    }
    return true;
}

} // namespace

SshKeyValidation SshKeyValidator::validate(const std::string& line) {
    SshKeyValidation result;

    if (line.empty()) {
        result.error = "empty input";
        return result;
    }
    if (line.size() > kMaxInputLength) {
        result.error = "input too long";
        return result;
    }

    // Check for control characters and NUL
    for (unsigned char c : line) {
        if (c == '\0') {
            result.error = "embedded NUL byte";
            return result;
        }
        if (std::iscntrl(c) != 0 && c != ' ' && c != '\t') {
            result.error = "control characters not allowed";
            return result;
        }
    }

    // Parse: <algorithm> <base64-blob> [comment]
    std::size_t pos = 0;

    // Skip leading whitespace
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
    if (pos >= line.size()) {
        result.error = "whitespace-only input";
        return result;
    }

    // Read algorithm
    std::size_t algo_start = pos;
    while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t') ++pos;
    std::string algorithm = line.substr(algo_start, pos - algo_start);

    if (!is_supported_algorithm(algorithm)) {
        result.error = "unsupported key algorithm: " + algorithm;
        return result;
    }

    // Skip whitespace
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
    if (pos >= line.size()) {
        result.error = "missing key data after algorithm";
        return result;
    }

    // Read base64 blob
    std::size_t blob_start = pos;
    while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t') ++pos;
    std::string base64_blob = line.substr(blob_start, pos - blob_start);

    // Validate base64 characters
    for (unsigned char c : base64_blob) {
        if (!is_valid_base64_char(c)) {
            result.error = "invalid base64 character in key data";
            return result;
        }
    }

    // Decode base64
    std::string decoded = base64_decode(base64_blob);
    if (decoded.empty() && !base64_blob.empty()) {
        result.error = "base64 decoding failed";
        return result;
    }

    // Validate binary blob structure
    std::string blob_error;
    if (!validate_key_blob(algorithm,
                           reinterpret_cast<const unsigned char*>(decoded.data()),
                           decoded.size(), blob_error)) {
        result.error = blob_error;
        return result;
    }

    // Read optional comment
    std::string comment;
    while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
    if (pos < line.size()) {
        comment = line.substr(pos);
        if (comment.size() > kMaxKeyCommentLength) {
            result.error = "key comment too long (max " +
                           std::to_string(kMaxKeyCommentLength) + " chars)";
            return result;
        }
        for (unsigned char c : comment) {
            if (std::iscntrl(c) != 0) {
                result.error = "control characters in comment";
                return result;
            }
        }
    }

    result.valid = true;
    result.key_type = algorithm;
    result.key_data = base64_blob;
    result.key_comment = comment;
    result.fingerprint = fingerprint(decoded);
    return result;
}

std::string SshKeyValidator::fingerprint(const std::string& decoded_blob) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(decoded_blob.data()),
           decoded_blob.size(), hash);

    // Base64 encode without padding
    static const char kBase64Chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string b64;
    b64.reserve(43);
    int val = 0;
    int valb = -6;
    for (std::size_t i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        val = (val << 8) + hash[i];
        valb += 8;
        while (valb >= 0) {
            b64.push_back(kBase64Chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        b64.push_back(kBase64Chars[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    // Remove trailing padding '=' characters
    while (!b64.empty() && b64.back() == '=') b64.pop_back();

    return "SHA256:" + b64;
}

std::string SshKeyValidator::base64_decode(const std::string& encoded) {
    static const unsigned char kTable[256] = {
        255 /*NUL*/,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,62 /*+*/,255,255,255,63 /*/*/,
        52,53,54,55,56,57,58,59,60,61,255,255,255,255 /*= as 0*/,255,255,
        255,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,255,255,255,255,255,
        255,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255
    };

    std::string out;
    out.reserve((encoded.size() * 3) / 4);
    std::vector<int> vals(4);
    int n = 0;

    for (unsigned char c : encoded) {
        unsigned char v = kTable[c];
        if (v == 255) continue; // skip whitespace/newlines
        vals[n++] = static_cast<int>(v);
        if (n == 4) {
            out.push_back(static_cast<char>((vals[0] << 2) | (vals[1] >> 4)));
            if (vals[2] < 64) {
                out.push_back(static_cast<char>((vals[1] << 4) | (vals[2] >> 2)));
                if (vals[3] < 64) {
                    out.push_back(static_cast<char>((vals[2] << 6) | vals[3]));
                }
            }
            n = 0;
        }
    }
    if (n > 0) return {}; // incomplete group = invalid

    return out;
}

} // namespace containercp::access
