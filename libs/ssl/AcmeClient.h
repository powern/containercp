#ifndef CONTAINERCP_SSL_ACME_CLIENT_H
#define CONTAINERCP_SSL_ACME_CLIENT_H

#include "core/OperationResult.h"
#include "logger/Logger.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::ssl {

class AcmeClient {
public:
    struct Account {
        std::string url;
        std::string kid;
    };

    struct Order {
        std::string url;
        std::string status;
        std::vector<std::string> authorizations;
        std::string finalize_url;
        std::string certificate_url;
    };

    struct Challenge {
        std::string url;
        std::string type;
        std::string token;
        std::string status;
    };

    struct Authorization {
        std::string url;
        std::string domain;
        std::string status;
        std::vector<Challenge> challenges;
    };

    AcmeClient(logger::Logger& logger);

    void set_staging(bool staging);
    bool is_staging() const { return staging_; }

    core::OperationResult discover_directory();
    core::OperationResult load_or_create_account(const std::string& key_path);

    core::OperationResult create_order(const std::vector<std::string>& domains, Order& order);
    core::OperationResult get_authorization(const std::string& authz_url, Authorization& authz);
    core::OperationResult respond_to_challenge(const std::string& challenge_url);
    core::OperationResult poll_challenge(const std::string& challenge_url, std::string& status, int max_retries = 15);
    core::OperationResult finalize_order(const std::string& finalize_url, const std::string& csr_pem, std::string& cert_url);
    core::OperationResult download_certificate(const std::string& cert_url, std::string& fullchain_pem);

    static std::string generate_account_key(const std::string& key_path);
    static std::string generate_csr(const std::string& domain, const std::string& key_path);
    static std::string url64(const std::string& data);
    static std::string sha256_base64(const std::string& data);

private:
    struct Response {
        int status_code = 0;
        std::string body;
        std::string nonce;
    };

    std::string find_json_string(const std::string& json, const std::string& key) const;
    std::string find_json_string_array(const std::string& json, const std::string& key) const;

    std::string get_nonce();
    std::string sign_jws(const std::string& payload, const std::string& url, bool use_kid = false);
    Response acme_post(const std::string& url, const std::string& payload);
    Response acme_get(const std::string& url);

    logger::Logger& logger_;
    bool staging_ = true;

    std::string directory_url_;
    std::string new_nonce_url_;
    std::string new_account_url_;
    std::string new_order_url_;

    Account account_;
    std::string account_key_path_;
};

} // namespace containercp::ssl

#endif // CONTAINERCP_SSL_ACME_CLIENT_H
