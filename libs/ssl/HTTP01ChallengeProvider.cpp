#include "HTTP01ChallengeProvider.h"
#include "ssl/CertificateProvider.h" // for ACME_CHALLENGE_PATH

#include <cerrno>
#include <curl/curl.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

// libcurl write callback
static size_t http01_write_cb(char* data, size_t size, size_t nmemb, std::string* buf) {
    buf->append(data, size * nmemb);
    return size * nmemb;
}

// Helper: create directory and all parents recursively
static bool mkdir_p(const std::string& path, mode_t mode, std::string& error_out) {
    if (path.empty()) { error_out = "empty path"; return false; }
    // Check if already exists
    struct stat st;
    if (::stat(path.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) return true;
        error_out = "exists but is not a directory";
        return false;
    }
    // Create parent first
    auto slash = path.rfind('/');
    if (slash != std::string::npos && slash > 0) {
        std::string parent = path.substr(0, slash);
        if (!mkdir_p(parent, mode, error_out)) return false;
    }
    // Create this directory
    if (::mkdir(path.c_str(), mode) != 0) {
        std::ostringstream err;
        err << "mkdir(" << path << ") failed: " << std::strerror(errno) << " (errno=" << errno << ")";
        error_out = err.str();
        return false;
    }
    return true;
}

namespace containercp::ssl {

HTTP01ChallengeProvider::HTTP01ChallengeProvider(logger::Logger& logger,
                                                  const std::string& sites_root,
                                                  const std::string& admin_challenge_root)
    : logger_(logger)
    , sites_root_(sites_root)
    , admin_challenge_root_(admin_challenge_root)
{
}

void HTTP01ChallengeProvider::set_admin_hostname(const std::string& hostname) {
    admin_hostname_ = hostname;
}

std::string HTTP01ChallengeProvider::type() const {
    return "http-01";
}

std::string HTTP01ChallengeProvider::challenge_dir(const std::string& domain) const {
    // Admin panel uses a dedicated challenge root (not a site directory)
    if (!admin_hostname_.empty() && domain == admin_hostname_ && !admin_challenge_root_.empty()) {
        return admin_challenge_root_;
    }
    // Regular sites: challenge files go to the site's public directory
    return sites_root_ + "/" + domain + "/public/.well-known/acme-challenge";
}

core::OperationResult HTTP01ChallengeProvider::prepare(
    const std::string& domain,
    const std::string& token,
    const std::string& key_authorization)
{
    // Validate domain is not empty
    if (domain.empty()) {
        return {false, "Domain cannot be empty for HTTP-01 challenge"};
    }

    // Ensure challenge directory exists (create recursively)
    std::string dir = challenge_dir(domain);
    std::string mkdir_err;
    if (!mkdir_p(dir, 0755, mkdir_err)) {
        std::string err = "Failed to create challenge directory: " + mkdir_err;
        logger_.error("HTTP-01", err);
        return {false, err};
    }
    logger_.info("HTTP-01", "challenge_root=" + dir);

    // Write token file
    std::string path = dir + "/" + token;
    std::ofstream file(path);
    if (!file.is_open()) {
        std::ostringstream err;
        err << "Failed to write challenge file: " << path
            << " (" << std::strerror(errno) << ", errno=" << errno << ")";
        logger_.error("HTTP-01", err.str());
        return {false, err.str()};
    }
    file << key_authorization;
    file.close();
    logger_.info("HTTP-01", "challenge_file=" + path);
    return {true, ""};
}

core::OperationResult HTTP01ChallengeProvider::cleanup(
    const std::string& domain,
    const std::string& token)
{
    std::string path = challenge_dir(domain) + "/" + token;
    ::unlink(path.c_str());
    logger_.info("HTTP-01", "Challenge token cleaned up: " + path);
    return {true, ""};
}

core::OperationResult HTTP01ChallengeProvider::verify(
    const std::string& domain)
{
    // Verify the challenge is served through HTTP
    std::string url = "http://" + domain + ACME_CHALLENGE_PATH;

    CURL* curl = curl_easy_init();
    if (!curl) {
        return {false, "Failed to initialize HTTP client"};
    }

    std::string resp_body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http01_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);

    long status = 0;
    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    }
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return {false, "Challenge verification failed: " + std::string(curl_easy_strerror(res))};
    }
    if (status != 200) {
        return {false, "Challenge verification failed: HTTP " + std::to_string(status)};
    }

    logger_.info("HTTP-01", "Challenge verified for " + domain);
    return {true, ""};
}

core::OperationResult HTTP01ChallengeProvider::can_validate(
    const std::string& domain)
{
    // Basic preflight: check if domain resolves
    CURL* curl = curl_easy_init();
    if (!curl) {
        return {false, "Failed to initialize HTTP client"};
    }

    std::string url = "http://" + domain + "/";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return {false, "Domain " + domain + " is not reachable: " + std::string(curl_easy_strerror(res))};
    }

    return {true, ""};
}

} // namespace containercp::ssl
