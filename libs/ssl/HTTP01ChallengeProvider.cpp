#include "HTTP01ChallengeProvider.h"
#include "ssl/CertificateProvider.h" // for ACME_CHALLENGE_PATH

#include <curl/curl.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

// libcurl write callback
static size_t http01_write_cb(char* data, size_t size, size_t nmemb, std::string* buf) {
    buf->append(data, size * nmemb);
    return size * nmemb;
}

namespace containercp::ssl {

HTTP01ChallengeProvider::HTTP01ChallengeProvider(logger::Logger& logger,
                                                  const std::string& ssl_root)
    : logger_(logger)
    , ssl_root_(ssl_root)
{
}

std::string HTTP01ChallengeProvider::type() const {
    return "http-01";
}

std::string HTTP01ChallengeProvider::challenge_dir(const std::string& domain) const {
    // Write challenge files under the ACME well-known path, served by central proxy
    return ssl_root_ + "/" + domain + "/.well-known/acme-challenge";
}

core::OperationResult HTTP01ChallengeProvider::prepare(
    const std::string& domain,
    const std::string& token,
    const std::string& key_authorization)
{
    // Ensure challenge directory exists
    std::string dir = challenge_dir(domain);
    std::string parent = ssl_root_ + "/" + domain;
    ::mkdir(parent.c_str(), 0700);
    ::mkdir(dir.c_str(), 0755);

    // Write token file
    std::string path = dir + "/" + token;
    std::ofstream file(path);
    if (!file.is_open()) {
        return {false, "Failed to write challenge file: " + path};
    }
    file << key_authorization;
    file.close();

    logger_.info("HTTP-01", "Challenge token written to " + path);
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
