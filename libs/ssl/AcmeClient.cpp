#define OPENSSL_SUPPRESS_DEPRECATED

#include "AcmeClient.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <curl/curl.h>
#include <memory>
#include <fstream>

// One-time libcurl global initialization
static bool curl_initialized = ([]() {
    curl_global_init(CURL_GLOBAL_ALL);
    std::atexit(curl_global_cleanup);
    return true;
})();
#include <openssl/bio.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509.h>
#include <sstream>
#include <thread>

namespace containercp::ssl {

// ============================================================
// libcurl write callback
// ============================================================
static size_t write_cb(char* data, size_t size, size_t nmemb, void* buf) {
    if (!data || !buf) return 0;
    size_t total = size * nmemb;
    std::string* sbuf = static_cast<std::string*>(buf);
    sbuf->append(data, total);
    return total;
}

// ============================================================
// Base64url encoding
// ============================================================
static const char b64url[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string AcmeClient::url64(const std::string& data) {
    std::string out;
    size_t len = data.size();
    for (size_t i = 0; i < len; i += 3) {
        unsigned char b0 = (unsigned char)data[i];
        unsigned char b1 = (i + 1 < len) ? (unsigned char)data[i + 1] : 0;
        unsigned char b2 = (i + 2 < len) ? (unsigned char)data[i + 2] : 0;
        out += b64url[b0 >> 2];
        out += b64url[((b0 << 4) | (b1 >> 4)) & 0x3f];
        if (i + 1 < len) out += b64url[((b1 << 2) | (b2 >> 6)) & 0x3f];
        if (i + 2 < len) out += b64url[b2 & 0x3f];
    }
    return out;
}

std::string AcmeClient::sha256_base64(const std::string& data) {
    unsigned char hash[32];
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, data.data(), data.size());
    EVP_DigestFinal_ex(ctx, hash, nullptr);
    EVP_MD_CTX_free(ctx);
    return url64(std::string((char*)hash, 32));
}

// ============================================================
// Constructor
// ============================================================
AcmeClient::AcmeClient(logger::Logger& logger)
    : logger_(logger)
{
    directory_url_ = "https://acme-staging-v02.api.letsencrypt.org/directory";
}

void AcmeClient::set_staging(bool staging) {
    staging_ = staging;
    if (staging) {
        directory_url_ = "https://acme-staging-v02.api.letsencrypt.org/directory";
    } else {
        directory_url_ = "https://acme-v02.api.letsencrypt.org/directory";
    }
}

// ============================================================
// Manual JSON string extraction (no dependencies)
// ============================================================
std::string AcmeClient::find_json_string(const std::string& json, const std::string& key) const {
    std::string search = "\"" + key + "\":\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) {
        search = "\"" + key + "\": \"";
        pos = json.find(search);
        if (pos == std::string::npos) return "";
    }
    pos += search.size();
    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            ++pos;
            if (json[pos] == 'n') result += '\n';
            else result += json[pos];
        } else {
            result += json[pos];
        }
        ++pos;
    }
    return result;
}

// Extract first string from a JSON array value
std::string AcmeClient::find_json_string_array(const std::string& json, const std::string& key) const {
    std::string search = "\"" + key + "\":[\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) {
        search = "\"" + key + "\": [\"";
        pos = json.find(search);
        if (pos == std::string::npos) return "";
    }
    pos += search.size();
    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) { ++pos; result += json[pos]; }
        else result += json[pos];
        ++pos;
    }
    return result;
}

// ============================================================
// libcurl helpers (stack-local strings only)
// ============================================================
struct HttpResponse {
    std::string body;
    std::string headers;
    int status_code = 0;
};

static HttpResponse http_post(const std::string& url, const std::string& body_data,
                               const std::string& content_type) {
    HttpResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) return resp;

    std::string buf_body;
    std::string buf_hdrs;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_data.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_data.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf_body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &buf_hdrs);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ContainerCP/0.5");
    curl_easy_setopt(curl, CURLOPT_CAINFO, "/etc/ssl/certs/ca-certificates.crt");

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Content-Type: " + content_type).c_str());
    headers = curl_slist_append(headers, "Accept: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        resp.status_code = (int)http_code;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    resp.body = buf_body;
    resp.headers = buf_hdrs;
    return resp;
}

static HttpResponse http_get(const std::string& url) {
    HttpResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) return resp;

    std::string resp_body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ContainerCP/0.5");
    curl_easy_setopt(curl, CURLOPT_CAINFO, "/etc/ssl/certs/ca-certificates.crt");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp_body);

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        resp.status_code = (int)http_code;
    }

    curl_easy_cleanup(curl);
    resp.body = resp_body;
    return resp;
}
static std::string extract_nonce(const std::string& headers) {
    auto pos = headers.find("Replay-Nonce: ");
    if (pos == std::string::npos) {
        pos = headers.find("replay-nonce: ");
        if (pos == std::string::npos) return "";
    }
    pos += 14; // "Replay-Nonce: ".length()
    auto end = headers.find("\r\n", pos);
    if (end == std::string::npos) return headers.substr(pos);
    return headers.substr(pos, end - pos);
}

// ============================================================
// Account key management
// ============================================================
std::string AcmeClient::generate_account_key(const std::string& key_path) {
    EVP_PKEY* pkey = EVP_PKEY_new();
    EC_KEY* ec = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    EC_KEY_generate_key(ec);
    EVP_PKEY_assign_EC_KEY(pkey, ec);

    BIO* bio = BIO_new(BIO_s_file());
    BIO_write_filename(bio, const_cast<char*>(key_path.c_str()));
    PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    BIO_free(bio);

    // Get raw public key bytes for JWK thumbprint
    BIO* pub_bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PUBKEY(pub_bio, pkey);
    char* pem_data = nullptr;
    long pem_len = BIO_get_mem_data(pub_bio, &pem_data);

    // Clean up
    EVP_PKEY_free(pkey);
    BIO_free(pub_bio);

    return std::string(pem_data, pem_len);
}

// ============================================================
// CSR generation
// ============================================================
std::string AcmeClient::generate_csr(const std::string& domain, const std::string& key_path) {
    // Generate a new key pair for the certificate
    EVP_PKEY* pkey = EVP_PKEY_new();
    EC_KEY* ec = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    EC_KEY_generate_key(ec);
    EVP_PKEY_assign_EC_KEY(pkey, ec);

    // Save the key to the specified path
    BIO* key_bio = BIO_new(BIO_s_file());
    BIO_write_filename(key_bio, const_cast<char*>(key_path.c_str()));
    PEM_write_bio_PrivateKey(key_bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    BIO_free(key_bio);

    // Build the DN and SAN extension
    X509_REQ* req = X509_REQ_new();
    X509_REQ_set_version(req, 0);

    // Subject: CN = domain
    X509_NAME* name = X509_NAME_new();
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                (unsigned char*)domain.c_str(), -1, -1, 0);
    X509_REQ_set_subject_name(req, name);

    // Set public key
    X509_REQ_set_pubkey(req, pkey);

    // CSR uses CN for domain; most CAs accept this

    // Sign the CSR
    X509_REQ_sign(req, pkey, EVP_sha256());

    // PEM encode
    BIO* csr_bio = BIO_new(BIO_s_mem());
    PEM_write_bio_X509_REQ(csr_bio, req);
    char* csr_data = nullptr;
    long csr_len = BIO_get_mem_data(csr_bio, &csr_data);

    std::string csr_pem(csr_data, csr_len);

    X509_REQ_free(req);
    X509_NAME_free(name);
    EVP_PKEY_free(pkey);
    BIO_free(csr_bio);

    return csr_pem;
}

// ============================================================
// JWS signing (ES256)
// ============================================================
std::string AcmeClient::compute_thumbprint() {
    // Load account key
    BIO* key_bio = BIO_new(BIO_s_file());
    if (BIO_read_filename(key_bio, account_key_path_.c_str()) <= 0) {
        BIO_free(key_bio);
        return "";
    }
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr);
    BIO_free(key_bio);
    if (!pkey) return "";

    EC_KEY* ec = EVP_PKEY_get1_EC_KEY(pkey);
    if (!ec) { EVP_PKEY_free(pkey); return ""; }
    const EC_GROUP* group = EC_KEY_get0_group(ec);
    const EC_POINT* point = EC_KEY_get0_public_key(ec);
    if (!group || !point) { EC_KEY_free(ec); EVP_PKEY_free(pkey); return ""; }

    BIGNUM* x = BN_new();
    BIGNUM* y = BN_new();
    if (!x || !y || !EC_POINT_get_affine_coordinates_GFp(group, point, x, y, nullptr)) {
        BN_free(x); BN_free(y); EC_KEY_free(ec); EVP_PKEY_free(pkey);
        return "";
    }

    char x_raw[32] = {0};
    char y_raw[32] = {0};
    BN_bn2binpad(x, (unsigned char*)x_raw, 32);
    BN_bn2binpad(y, (unsigned char*)y_raw, 32);
    std::string x_b64 = url64(std::string(x_raw, 32));
    std::string y_b64 = url64(std::string(y_raw, 32));

    std::string jwk_str = "{\"crv\":\"P-256\",\"kty\":\"EC\",\"x\":\"" + x_b64 + "\",\"y\":\"" + y_b64 + "\"}";
    std::string thumbprint = sha256_base64(jwk_str);

    BN_free(x); BN_free(y);
    EC_KEY_free(ec);
    EVP_PKEY_free(pkey);
    return thumbprint;
}

std::string AcmeClient::compute_key_authorization(const std::string& token) {
    std::string thumbprint = compute_thumbprint();
    if (thumbprint.empty()) return "";
    return token + "." + thumbprint;
}

std::string AcmeClient::sign_jws(const std::string& payload, const std::string& url, bool use_kid) {
    // Load account key
    BIO* key_bio = BIO_new(BIO_s_file());
    if (BIO_read_filename(key_bio, account_key_path_.c_str()) <= 0) {
        BIO_free(key_bio);
        return "";
    }
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr);
    BIO_free(key_bio);
    if (!pkey) return "";

    // Get EC key to extract raw public key coordinates
    EC_KEY* ec = EVP_PKEY_get1_EC_KEY(pkey);
    if (!ec) {
        EVP_PKEY_free(pkey);
        return "";
    }
    const EC_GROUP* group = EC_KEY_get0_group(ec);
    if (!group) {
        EC_KEY_free(ec);
        EVP_PKEY_free(pkey);
        return "";
    }
    const EC_POINT* point = EC_KEY_get0_public_key(ec);
    if (!point) {
        EC_KEY_free(ec);
        EVP_PKEY_free(pkey);
        return "";
    }

    BIGNUM* x = BN_new();
    BIGNUM* y = BN_new();
    if (!x || !y || !EC_POINT_get_affine_coordinates_GFp(group, point, x, y, nullptr)) {
        BN_free(x);
        BN_free(y);
        EC_KEY_free(ec);
        EVP_PKEY_free(pkey);
        return "";
    }

    // Convert to raw 32-byte big-endian
    char x_raw[32] = {0};
    char y_raw[32] = {0};
    BN_bn2binpad(x, (unsigned char*)x_raw, 32);
    BN_bn2binpad(y, (unsigned char*)y_raw, 32);

    // Build JWK
    std::string x_b64 = url64(std::string(x_raw, 32));
    std::string y_b64 = url64(std::string(y_raw, 32));

    std::string thumbprint = compute_thumbprint();

    // Build protected header
    std::string protected_header;
    std::string nonce = get_nonce();
    if (use_kid && !account_.kid.empty()) {
        protected_header = "{\"alg\":\"ES256\",\"kid\":\"" + account_.kid + "\",\"nonce\":\"" + nonce + "\",\"url\":\"" + url + "\"}";
    } else {
        protected_header = "{\"alg\":\"ES256\",\"jwk\":{\"crv\":\"P-256\",\"kty\":\"EC\",\"x\":\"" + x_b64 + "\",\"y\":\"" + y_b64 + "\"},\"nonce\":\"" + nonce + "\",\"url\":\"" + url + "\"}";
    }

    std::string protected_b64 = url64(protected_header);
    std::string payload_b64 = url64(payload);

    // Sign "protected_b64.payload_b64"
    std::string signing_input = protected_b64 + "." + payload_b64;

    unsigned char sig_der[128];
    size_t sig_der_len = sizeof(sig_der);
    EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
    EVP_PKEY_CTX* pctx = nullptr;
    if (EVP_DigestSignInit(md_ctx, &pctx, EVP_sha256(), nullptr, pkey) != 1) {
        fprintf(stderr, "ACME-DBG: EVP_DigestSignInit FAILED\n");
        EVP_MD_CTX_free(md_ctx);
        EC_KEY_free(ec);
        EVP_PKEY_free(pkey);
        return "";
    }
    if (EVP_DigestSignUpdate(md_ctx, signing_input.data(), signing_input.size()) != 1) {
        fprintf(stderr, "ACME-DBG: EVP_DigestSignUpdate FAILED\n");
        EVP_MD_CTX_free(md_ctx);
        EC_KEY_free(ec);
        EVP_PKEY_free(pkey);
        return "";
    }
    if (EVP_DigestSignFinal(md_ctx, sig_der, &sig_der_len) != 1) {
        fprintf(stderr, "ACME-DBG: EVP_DigestSignFinal FAILED (err=0x%lx)\n", ERR_get_error());
        EVP_MD_CTX_free(md_ctx);
        EC_KEY_free(ec);
        EVP_PKEY_free(pkey);
        return "";
    }
    EVP_MD_CTX_free(md_ctx);

    // Convert DER-encoded signature to raw R||S (64 bytes for P-256)
    // DER format: 30 44 02 20 <R:32> 02 20 <S:32>
    // May also be: 30 45 02 21 <R:33> 02 20 <S:32> (with leading 00)
    std::string sig_raw;
    fprintf(stderr, "ACME-DBG: sig_der_len=%zu sig_der[0]=0x%02x sig_der[1]=0x%02x\n",
            sig_der_len, (unsigned)sig_der[0], (unsigned)sig_der[1]);
    if (sig_der_len >= 8 && sig_der[0] == 0x30) {
        size_t off = 2;
        // R integer
        if (off + 2 < sig_der_len && sig_der[off] == 0x02) {
            size_t r_len = sig_der[off + 1];
            off += 2;
            fprintf(stderr, "ACME-DBG: R tag at off=%zu r_len=%zu\n", off-2, r_len);
            if (r_len == 33) { off++; r_len = 32; }
            if (off + r_len <= sig_der_len) {
                sig_raw.append((char*)(sig_der + off), r_len);
                off += r_len;
            }
        } else {
            fprintf(stderr, "ACME-DBG: R tag NOT FOUND at off=%zu val=0x%02x\n", off, (unsigned)sig_der[off]);
        }
        // S integer
        if (off + 2 < sig_der_len && sig_der[off] == 0x02) {
            size_t s_len = sig_der[off + 1];
            off += 2;
            fprintf(stderr, "ACME-DBG: S tag at off=%zu s_len=%zu\n", off-2, s_len);
            if (s_len == 33) { off++; s_len = 32; }
            if (off + s_len <= sig_der_len) {
                sig_raw.append((char*)(sig_der + off), s_len);
            }
        } else {
            fprintf(stderr, "ACME-DBG: S tag NOT FOUND at off=%zu val=0x%02x\n", off, (unsigned)sig_der[off]);
        }
    } else {
        sig_raw = std::string((char*)sig_der, sig_der_len);
    }
    fprintf(stderr, "ACME-DBG: sig_raw_len=%zu\n", sig_raw.size());

    std::string sig_b64 = url64(sig_raw);

    BN_free(x);
    BN_free(y);
    EC_KEY_free(ec);
    EVP_PKEY_free(pkey);

    std::string jws = "{\"protected\":\"" + protected_b64 + "\",\"payload\":\"" + payload_b64 + "\",\"signature\":\"" + sig_b64 + "\"}";

    // Debug logging (no private key data)
    fprintf(stderr, "ACME-DBG: protected_b64_len=%zu payload_b64_len=%zu sig_raw_len=%zu sig_b64_len=%zu jws_len=%zu\n",
            protected_b64.size(), payload_b64.size(), sig_raw.size(), sig_b64.size(), jws.size());

    return jws;
}

// ============================================================
// Http HEAD for nonce fetching (stack-local string)
// ============================================================
static std::string http_head(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string headers;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ContainerCP/0.5");

    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return headers;
}

// ============================================================
// ACME HTTP methods
// ============================================================
std::string AcmeClient::get_nonce() {
    std::string resp_headers = http_head(new_nonce_url_);
    std::string nonce = extract_nonce(resp_headers);
    return nonce;
}

AcmeClient::Response AcmeClient::acme_post(const std::string& url, const std::string& payload) {
    std::string jws = sign_jws(payload, url, true);
    if (jws.empty()) {
        jws = sign_jws(payload, url, false);
    }

    HttpResponse http_resp = http_post(url, jws, "application/jose+json");

    Response r;
    r.status_code = http_resp.status_code;
    r.body = http_resp.body;
    r.nonce = extract_nonce(http_resp.headers);

    // Extract Location header
    auto loc_pos = http_resp.headers.find("Location: ");
    if (loc_pos == std::string::npos) loc_pos = http_resp.headers.find("location: ");
    if (loc_pos != std::string::npos) {
        loc_pos += 10; // skip "Location: "
        auto end = http_resp.headers.find("\r\n", loc_pos);
        if (end != std::string::npos)
            r.location = http_resp.headers.substr(loc_pos, end - loc_pos);
    }

    return r;
}

AcmeClient::Response AcmeClient::acme_get(const std::string& url) {
    // POST-as-GET: POST with empty payload signed
    return acme_post(url, "");
}

// ============================================================
// ACME Directory discovery
// ============================================================
core::OperationResult AcmeClient::discover_directory() {
    logger_.info("ACME", "Discovering directory at " + directory_url_);

    HttpResponse get_resp = http_get(directory_url_);

    if (get_resp.status_code != 200 || get_resp.body.empty()) {
        return {false, "Failed to fetch ACME directory: HTTP " + std::to_string(get_resp.status_code)};
    }

    new_nonce_url_ = find_json_string(get_resp.body, "newNonce");
    new_account_url_ = find_json_string(get_resp.body, "newAccount");
    new_order_url_ = find_json_string(get_resp.body, "newOrder");

    if (new_nonce_url_.empty() || new_account_url_.empty() || new_order_url_.empty()) {
        return {false, "ACME directory response missing required URLs"};
    }

    logger_.info("ACME", "Directory discovered: newNonce=" + new_nonce_url_);
    return {true, ""};
}

// ============================================================
// Account management
// ============================================================
core::OperationResult AcmeClient::load_or_create_account(const std::string& key_path) {
    logger_.info("ACME-DBG", "1: enter load_or_create_account");
    account_key_path_ = key_path;
    logger_.info("ACME-DBG", "2: key_path=" + key_path);

    // Check if key exists
    std::ifstream key_file(key_path);
    bool exists = key_file.good();
    key_file.close();
    logger_.info("ACME-DBG", "3: key exists=" + std::to_string(exists));

    if (!exists) {
        logger_.info("ACME-DBG", "4a: generating new key");
        generate_account_key(key_path);
        logger_.info("ACME-DBG", "4b: key generated");
    }

    logger_.info("ACME-DBG", "5: before PEM_read_PrivateKey");
    BIO* key_bio = BIO_new(BIO_s_file());
    if (BIO_read_filename(key_bio, key_path.c_str()) <= 0) {
        logger_.info("ACME-DBG", "5-ERR: read failed");
        BIO_free(key_bio);
        return {false, "Failed to read account key"};
    }
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr);
    BIO_free(key_bio);
    logger_.info("ACME-DBG", std::string("6: after PEM_read, pkey=") + (pkey ? "valid" : "null"));

    if (!pkey) {
        return {false, "Failed to parse account key"};
    }
    EVP_PKEY_free(pkey);
    logger_.info("ACME-DBG", "7: pkey freed");

    // Create account via newAccount (sign without kid, use JWK)
    logger_.info("ACME-DBG", "8: before sign_jws");
    std::string payload = "{\"termsOfServiceAgreed\":true}";
    std::string jws = sign_jws(payload, new_account_url_, false);
    logger_.info("ACME-DBG", "9: after sign_jws, jws.size=" + std::to_string(jws.size()));

    logger_.info("ACME-DBG", "10: before http_post newAccount");
    HttpResponse post_resp = http_post(new_account_url_, jws, "application/jose+json");
    logger_.info("ACME-DBG", "11: after http_post status=" + std::to_string(post_resp.status_code)
                 + " body_size=" + std::to_string(post_resp.body.size()));

    if (post_resp.status_code == 201 || post_resp.status_code == 200) {
        logger_.info("ACME-DBG", "12a: account registered success");
        std::string loc_header;
        auto loc_pos = post_resp.headers.find("Location: ");
        if (loc_pos != std::string::npos) {
            loc_pos += 10;
            auto end = post_resp.headers.find("\r\n", loc_pos);
            if (end != std::string::npos)
                loc_header = post_resp.headers.substr(loc_pos, end - loc_pos);
        }
        if (loc_header.empty()) {
            loc_pos = post_resp.headers.find("location: ");
            if (loc_pos != std::string::npos) {
                loc_pos += 10;
                auto end = post_resp.headers.find("\r\n", loc_pos);
                if (end != std::string::npos)
                    loc_header = post_resp.headers.substr(loc_pos, end - loc_pos);
            }
        }
        account_.url = loc_header;
        account_.kid = loc_header;
        logger_.info("ACME", "Account registered: " + account_.url);
        return {true, ""};
    }

    std::string error_msg = "Account registration failed: HTTP "
                          + std::to_string(post_resp.status_code) + " " + post_resp.body;
    logger_.info("ACME-DBG", "12b: account failed, error=" + error_msg);
    return {false, error_msg};
}

// ============================================================
// Order creation
// ============================================================
core::OperationResult AcmeClient::create_order(const std::vector<std::string>& domains, Order& order) {
    std::string ids = "[";
    for (size_t i = 0; i < domains.size(); ++i) {
        if (i > 0) ids += ",";
        ids += "{\"type\":\"dns\",\"value\":\"" + domains[i] + "\"}";
    }
    ids += "]";
    std::string payload = "{\"identifiers\":" + ids + "}";

    auto resp = acme_post(new_order_url_, payload);
    if (resp.status_code != 201) {
        return {false, "Order creation failed: HTTP " + std::to_string(resp.status_code) + " " + resp.body};
    }

    // Log full response body and Location for debugging
    logger_.info("ACME-DBG", "newOrder status=" + std::to_string(resp.status_code)
                 + " location=" + resp.location + " body=" + resp.body);

    order.url = resp.location;
    logger_.info("ACME-DBG", "order.url=" + order.url);

    // Parse response body
    order.status = find_json_string(resp.body, "status");
    order.finalize_url = find_json_string(resp.body, "finalize");

    // Parse authorizations array robustly
    // Look for "authorizations": then find all quoted strings in the array
    {
        // Find the key "authorizations" and the start of its value
        auto authz_key = resp.body.find("\"authorizations\"");
        if (authz_key == std::string::npos) {
            return {false, "Order response missing authorizations"};
        }
        auto colon = resp.body.find(':', authz_key);
        if (colon == std::string::npos) {
            return {false, "Order response malformed: no colon after authorizations"};
        }
        // Find the opening bracket of the array
        auto bracket = resp.body.find('[', colon);
        if (bracket == std::string::npos) {
            return {false, "Order response malformed: no array in authorizations"};
        }
        // Find all quoted strings inside the array
        auto pos = bracket + 1;
        while (pos < resp.body.size()) {
            auto quote = resp.body.find('"', pos);
            if (quote == std::string::npos) break;
            // Check if we've passed the closing bracket
            auto close_bracket = resp.body.find(']', pos);
            if (close_bracket != std::string::npos && quote > close_bracket) break;
            auto end_quote = resp.body.find('"', quote + 1);
            if (end_quote == std::string::npos) break;
            if (end_quote > quote + 1) {
                order.authorizations.push_back(resp.body.substr(quote + 1, end_quote - quote - 1));
            }
            pos = end_quote + 1;
        }
    }

    logger_.info("ACME", "Order created: status=" + order.status
                 + " authorizations=" + std::to_string(order.authorizations.size()));

    // Validate we have at least one authorization
    if (order.authorizations.empty()) {
        return {false, "Order has no authorizations. ACME response may be invalid."};
    }

    return {true, ""};
}

// ============================================================
// Authorization
// ============================================================
core::OperationResult AcmeClient::get_authorization(const std::string& authz_url, Authorization& authz) {
    auto resp = acme_get(authz_url);
    if (resp.status_code != 200) {
        return {false, "Authorization fetch failed: HTTP " + std::to_string(resp.status_code)};
    }

    // Log full response for debugging
    logger_.info("ACME-DBG", "authz body: " + resp.body);

    authz.url = authz_url;

    // Extract domain from the "identifier" object's "value" field
    {
        auto val_key = resp.body.find("\"identifier\"");
        if (val_key != std::string::npos) {
            auto val_val = resp.body.find("\"value\"", val_key);
            if (val_val != std::string::npos) {
                val_val = resp.body.find('"', val_val + 7); // find the quote after "value":
                if (val_val != std::string::npos && val_val + 1 < resp.body.size() && resp.body[val_val + 1] == ':') {
                    val_val = resp.body.find('"', val_val + 2); // find opening quote of the value
                }
                if (val_val != std::string::npos && resp.body[val_val] == '"') {
                    val_val++; // skip opening quote
                    auto end = resp.body.find('"', val_val);
                    if (end != std::string::npos) {
                        authz.domain = resp.body.substr(val_val, end - val_val);
                    }
                }
            }
        }
    }
    logger_.info("ACME-DBG", "authz.identifier.value=" + authz.domain);
    authz.status = find_json_string(resp.body, "status");

    // Parse challenges array robustly
    {
        auto ch_key = resp.body.find("\"challenges\"");
        if (ch_key == std::string::npos) {
            return {false, "Authorization response missing challenges"};
        }
        auto colon = resp.body.find(':', ch_key);
        if (colon == std::string::npos) {
            return {false, "Authorization malformed: no colon after challenges"};
        }
        auto bracket = resp.body.find('[', colon);
        if (bracket == std::string::npos) {
            return {false, "Authorization malformed: no array in challenges"};
        }

        // Parse each JSON object in the challenges array
        size_t pos = bracket + 1;
        while (pos < resp.body.size()) {
            // Find opening brace of a challenge object
            auto brace = resp.body.find('{', pos);
            if (brace == std::string::npos) break;

            // Check if we've passed the closing bracket
            auto close_bracket = resp.body.find(']', pos);
            if (close_bracket != std::string::npos && brace > close_bracket) break;

            // Find matching closing brace (track depth)
            int depth = 0;
            size_t end_brace = brace;
            for (size_t k = brace; k < resp.body.size(); ++k) {
                if (resp.body[k] == '{') depth++;
                if (resp.body[k] == '}') {
                    depth--;
                    if (depth == 0) { end_brace = k; break; }
                }
            }
            if (end_brace <= brace) break;

            // Extract the challenge JSON object
            std::string chal_str = resp.body.substr(brace, end_brace - brace + 1);

            Challenge ch;
            ch.url = find_json_string(chal_str, "url");
            ch.type = find_json_string(chal_str, "type");
            ch.token = find_json_string(chal_str, "token");
            ch.status = find_json_string(chal_str, "status");
            if (!ch.type.empty()) {
                authz.challenges.push_back(ch);
            }

            pos = end_brace + 1;
        }
    }

    logger_.info("ACME", "Authorization for " + authz.domain + ": status=" + authz.status
                 + " challenges=" + std::to_string(authz.challenges.size()));

    if (authz.challenges.empty()) {
        return {false, "No challenges found in authorization response"};
    }

    return {true, ""};
}

// ============================================================
// Challenge response
// ============================================================
core::OperationResult AcmeClient::respond_to_challenge(const std::string& challenge_url) {
    auto resp = acme_post(challenge_url, "{}");
    if (resp.status_code != 200) {
        return {false, "Challenge response failed: HTTP " + std::to_string(resp.status_code) + " " + resp.body};
    }
    logger_.info("ACME", "Challenge response sent");
    return {true, ""};
}

// ============================================================
// Polling
// ============================================================
core::OperationResult AcmeClient::poll_challenge(const std::string& challenge_url, std::string& status, int max_retries) {
    for (int i = 0; i < max_retries; ++i) {
        auto resp = acme_get(challenge_url);
        if (resp.status_code != 200) {
            return {false, "Poll failed: HTTP " + std::to_string(resp.status_code)};
        }

        status = find_json_string(resp.body, "status");
        logger_.info("ACME", "Challenge poll #" + std::to_string(i + 1) + ": " + status);

        if (status == "valid") return {true, ""};
        if (status == "invalid") {
            auto error = find_json_string(resp.body, "error");
            return {false, "Challenge failed: " + error};
        }

        // Sleep 2 seconds between polls
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    return {false, "Challenge did not become valid within timeout"};
}

// ============================================================
// Finalize order
// ============================================================
core::OperationResult AcmeClient::finalize_order(const std::string& finalize_url, const std::string& order_url, const std::string& csr_pem, std::string& cert_url) {
    logger_.info("ACME-DBG", "finalize: csr_pem.size=" + std::to_string(csr_pem.size())
                 + " finalize_url=" + finalize_url + " order_url=" + order_url);

    // Step 1: Convert CSR PEM to DER then base64url (RFC 8555 section 7.4)
    BIO* pem_bio = BIO_new_mem_buf(csr_pem.data(), csr_pem.size());
    if (!pem_bio) return {false, "Failed to create PEM BIO for CSR"};

    X509_REQ* req = PEM_read_bio_X509_REQ(pem_bio, nullptr, nullptr, nullptr);
    BIO_free(pem_bio);
    if (!req) return {false, "Failed to decode CSR"};

    int der_len = i2d_X509_REQ(req, nullptr);
    if (der_len <= 0) { X509_REQ_free(req); return {false, "Failed to get CSR DER length"}; }

    std::vector<unsigned char> der_buf(der_len);
    unsigned char* der_ptr = der_buf.data();
    int der_len2 = i2d_X509_REQ(req, &der_ptr);
    X509_REQ_free(req);
    if (der_len2 <= 0) return {false, "Failed to convert CSR to DER"};

    std::string csr_b64 = url64(std::string((char*)der_buf.data(), der_len2));
    std::string payload = "{\"csr\":\"" + csr_b64 + "\"}";

    // Step 2: POST finalize URL exactly ONCE (RFC 8555 section 7.4)
    logger_.info("ACME-DBG", "finalize: POST " + finalize_url);
    auto resp = acme_post(finalize_url, payload);
    logger_.info("ACME-DBG", "finalize response: http=" + std::to_string(resp.status_code)
                 + " body=" + resp.body);

    if (resp.status_code != 200) {
        return {false, "Finalize failed: HTTP " + std::to_string(resp.status_code) + " " + resp.body};
    }

    // Step 3: Parse order status from the finalize response
    std::string order_status = find_json_string(resp.body, "status");
    logger_.info("ACME", "Order status after finalize: " + order_status);

    if (order_status == "valid") {
        cert_url = find_json_string(resp.body, "certificate");
        logger_.info("ACME", "Certificate URL: " + cert_url);
        if (cert_url.empty()) return {false, "Order valid but no certificate URL"};
        return {true, ""};
    }

    // Step 4: If processing, poll the ORDER URL (not finalize!) per RFC 8555 section 7.4.1
    if (order_status == "processing") {
        if (order_url.empty()) {
            return {false, "Order URL is required for polling but was not captured"};
        }
        logger_.info("ACME", "Order processing, polling " + order_url);
        for (int i = 0; i < 30; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            // POST-as-GET to order URL (RFC 8555 section 6.5: GET-as-POST)
            auto poll_resp = acme_post(order_url, "");
            logger_.info("ACME-DBG", "poll #" + std::to_string(i+1)
                         + " http=" + std::to_string(poll_resp.status_code)
                         + " status=" + find_json_string(poll_resp.body, "status")
                         + " body=" + poll_resp.body);

            if (poll_resp.status_code != 200) {
                return {false, "Order poll HTTP " + std::to_string(poll_resp.status_code)};
            }

            order_status = find_json_string(poll_resp.body, "status");
            if (order_status == "valid") {
                cert_url = find_json_string(poll_resp.body, "certificate");
                logger_.info("ACME", "Certificate URL: " + cert_url);
                if (cert_url.empty()) return {false, "Order valid but no certificate URL"};
                return {true, ""};
            }
            if (order_status == "invalid") {
                return {false, "Order invalid: " + find_json_string(poll_resp.body, "error")};
            }
            // "pending" or "processing" — continue polling
        }
        return {false, "Order did not become valid within timeout"};
    }

    if (order_status == "invalid") {
        return {false, "Order invalid: " + find_json_string(resp.body, "error")};
    }

    return {false, "Unexpected order status: " + order_status};
}

// ============================================================
// Certificate download
// ============================================================
core::OperationResult AcmeClient::download_certificate(const std::string& cert_url, std::string& fullchain_pem) {
    if (cert_url.empty()) {
        return {false, "Certificate URL is empty"};
    }

    // POST-as-GET to download
    auto resp = acme_get(cert_url);
    if (resp.status_code != 200) {
        return {false, "Certificate download failed: HTTP " + std::to_string(resp.status_code)};
    }

    fullchain_pem = resp.body;
    logger_.info("ACME", "Certificate downloaded (size=" + std::to_string(fullchain_pem.size()) + ")");
    return {true, ""};
}

} // namespace containercp::ssl
