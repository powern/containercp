#ifndef CONTAINERCP_SSL_ACME_CLIENT_H
#define CONTAINERCP_SSL_ACME_CLIENT_H

#include "core/OperationResult.h"
#include "logger/Logger.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::ssl {

// Reusable ACME client implementing RFC 8555.
//
// Layers:
//   Account   — register or load ACME account
//   Order     — request new certificate order
//   Authz     — complete domain authorization
//   Challenge — respond to HTTP-01 / DNS-01 challenge
//   Finalize  — submit CSR and finalize order
//   Download  — retrieve issued certificate
//
// LetsEncryptProvider is an adapter that calls these methods.
// ChallengeProvider handles the transport-specific part (HTTP-01, DNS-01).

class AcmeClient {
public:
    struct Account {
        std::string url;
        std::string kid;
        std::string key_path;  // path to account private key PEM
    };

    struct Order {
        std::string url;
        std::string status;  // "pending", "ready", "processing", "valid", "invalid"
        std::vector<std::string> authorizations;
        std::string finalize_url;
        std::string certificate_url;
    };

    struct Challenge {
        std::string url;
        std::string type;     // "http-01", "dns-01"
        std::string token;
        std::string status;   // "pending", "processing", "valid", "invalid"
    };

    struct Authorization {
        std::string url;
        std::string domain;
        std::string status;   // "pending", "valid", "invalid", "deactivated"
        std::vector<Challenge> challenges;
    };

    AcmeClient(logger::Logger& logger);

    // Directory discovery
    core::OperationResult discover_directory();
    bool is_staging() const { return staging_; }
    void set_staging(bool staging);

    // Account management
    core::OperationResult load_or_create_account(const std::string& account_key_path);

    // Order lifecycle
    core::OperationResult create_order(const std::vector<std::string>& domains, Order& order);
    core::OperationResult get_authorization(const std::string& authz_url, Authorization& authz);
    core::OperationResult respond_to_challenge(const std::string& challenge_url,
                                                const std::string& key_authorization);
    core::OperationResult poll_challenge(const std::string& challenge_url, std::string& status,
                                          int max_retries = 10);
    core::OperationResult poll_authorization(const std::string& authz_url, std::string& status,
                                              int max_retries = 10);

    // Finalization
    core::OperationResult finalize_order(const std::string& finalize_url,
                                          const std::string& csr_pem,
                                          std::string& certificate_url);
    core::OperationResult poll_order(const std::string& order_url, std::string& status,
                                      std::string& certificate_url, int max_retries = 10);

    // Certificate download
    core::OperationResult download_certificate(const std::string& cert_url,
                                                std::string& fullchain_pem,
                                                std::string& privkey_pem);

    // Utilities
    static std::string generate_csr(const std::string& domain,
                                     const std::string& key_path);
    static std::string generate_account_key(const std::string& key_path);
    static std::string url64(const std::string& data);
    static std::string sha256_base64(const std::string& data);

private:
    logger::Logger& logger_;
    bool staging_ = false;

    std::string directory_url_;
    std::string new_nonce_url_;
    std::string new_account_url_;
    std::string new_order_url_;

    Account account_;
};

} // namespace containercp::ssl

#endif // CONTAINERCP_SSL_ACME_CLIENT_H
