#include "LetsEncryptProvider.h"

namespace containercp::ssl {

LetsEncryptProvider::LetsEncryptProvider(logger::Logger& logger,
                                         ChallengeProvider& challenge,
                                         CertificateStore& store)
    : logger_(logger)
    , challenge_(challenge)
    , store_(store)
    , acme_(logger)
    , ssl_dir_("/srv/containercp/ssl")
{
}

core::OperationResult LetsEncryptProvider::request(const std::string& domain) {
    logger_.info("LetsEncrypt", "Certificate request for " + domain);

    // Preflight validation (fast fail before ACME)
    auto preflight = preflight_validation(domain);
    if (!preflight.success) {
        return preflight;
    }

    // TODO: Resolve domain to site_id, then call issue_certificate
    // For now, return placeholder success
    return {true, "Placeholder: certificate request logged"};
}

core::OperationResult LetsEncryptProvider::renew(const std::string& domain) {
    logger_.info("LetsEncrypt", "Certificate renewal for " + domain);

    // Renew follows same flow as initial request
    auto preflight = preflight_validation(domain);
    if (!preflight.success) {
        return preflight;
    }

    return {true, "Placeholder: certificate renewal logged"};
}

core::OperationResult LetsEncryptProvider::revoke(const std::string& domain) {
    logger_.info("LetsEncrypt", "Certificate revocation for " + domain);
    // TODO: Call ACME revokeCertificate endpoint
    (void)domain;
    return {true, ""};
}

core::OperationResult LetsEncryptProvider::status(const std::string& domain) {
    logger_.info("LetsEncrypt", "Certificate status check for " + domain);
    (void)domain;
    return {true, ""};
}

std::string LetsEncryptProvider::provider_id() const {
    return "letsencrypt";
}

std::string LetsEncryptProvider::provider_name() const {
    return "Let's Encrypt";
}

bool LetsEncryptProvider::supports_auto_renew() const {
    return true;
}

std::string LetsEncryptProvider::certificate_path(const std::string& domain) const {
    return ssl_dir_ + "/" + domain + "/current/fullchain.pem";
}

std::string LetsEncryptProvider::key_path(const std::string& domain) const {
    return ssl_dir_ + "/" + domain + "/current/privkey.pem";
}

std::string LetsEncryptProvider::chain_path(const std::string& domain) const {
    return ssl_dir_ + "/" + domain + "/current/chain.pem";
}

core::OperationResult LetsEncryptProvider::preflight_validation(const std::string& domain) {
    logger_.info("LetsEncrypt", "Preflight validation for " + domain);

    // Check domain is not localhost, .local, or .test
    if (domain == "localhost" || domain.find("localhost") != std::string::npos) {
        return {false, "Cannot issue certificate for localhost"};
    }
    if (domain.size() >= 6 && domain.substr(domain.size() - 6) == ".local") {
        return {false, "Cannot issue certificate for .local domains"};
    }
    if (domain.size() >= 5 && domain.substr(domain.size() - 5) == ".test") {
        return {false, "Cannot issue certificate for .test domains"};
    }

    // Delegate to ChallengeProvider for transport-level checks
    auto can_val = challenge_.can_validate(domain);
    if (!can_val.success) {
        return can_val;
    }

    return {true, ""};
}

core::OperationResult LetsEncryptProvider::issue_certificate(
    uint64_t site_id,
    const std::string& domain,
    const std::vector<std::string>& domains)
{
    logger_.info("LetsEncrypt", "Issuing certificate for " + domain);

    // Step 1: Discover ACME directory
    auto dir_result = acme_.discover_directory();
    if (!dir_result.success) return dir_result;

    // Step 2: Load or create account
    std::string account_key_path = ssl_dir_ + "/account.pem";
    auto acct_result = acme_.load_or_create_account(account_key_path);
    if (!acct_result.success) return acct_result;

    // Step 3: Create order
    AcmeClient::Order order;
    auto order_result = acme_.create_order(domains, order);
    if (!order_result.success) return order_result;

    // Step 4: Complete authorizations
    for (const auto& authz_url : order.authorizations) {
        AcmeClient::Authorization authz;
        auto authz_result = acme_.get_authorization(authz_url, authz);
        if (!authz_result.success) return authz_result;

        // Find the HTTP-01 challenge
        AcmeClient::Challenge* http_challenge = nullptr;
        for (auto& ch : authz.challenges) {
            if (ch.type == "http-01") {
                http_challenge = &ch;
                break;
            }
        }
        if (!http_challenge) {
            return {false, "No HTTP-01 challenge available for " + authz.domain};
        }

        // Prepare challenge via ChallengeProvider
        // key_auth = token + "." + base64url(SHA256(account JWK))
        std::string key_auth = http_challenge->token + ".placeholder_thumbprint";
        auto prep_result = challenge_.prepare(authz.domain, http_challenge->token, key_auth);
        if (!prep_result.success) return prep_result;

        // Respond to challenge
        auto resp_result = acme_.respond_to_challenge(http_challenge->url, key_auth);
        if (!resp_result.success) return resp_result;

        // Poll for completion
        std::string challenge_status;
        auto poll_result = acme_.poll_challenge(http_challenge->url, challenge_status);
        if (!poll_result.success) return poll_result;

        // Clean up challenge
        challenge_.cleanup(authz.domain, http_challenge->token);

        if (challenge_status != "valid") {
            return {false, "ACME challenge failed for " + authz.domain + ": " + challenge_status};
        }
    }

    // Step 5: Generate CSR and finalize
    std::string key_path = ssl_dir_ + "/" + std::to_string(site_id) + "/privkey.pem";
    std::string csr = AcmeClient::generate_csr(domain, key_path);

    std::string cert_url;
    auto final_result = acme_.finalize_order(order.finalize_url, csr, cert_url);
    if (!final_result.success) return final_result;

    // Step 6: Poll order for certificate URL
    std::string order_status;
    auto poll_order_result = acme_.poll_order(order.url, order_status, cert_url);
    if (!poll_order_result.success) return poll_order_result;

    // Step 7: Download certificate
    std::string fullchain_pem;
    std::string privkey_pem;
    auto dl_result = acme_.download_certificate(cert_url, fullchain_pem, privkey_pem);
    if (!dl_result.success) return dl_result;

    // Step 8: Store via CertificateStore
    CertificateStore::Metadata meta;
    meta.site_id = site_id;
    meta.provider_id = "letsencrypt";
    meta.status = "active";
    meta.domains = domains;
    meta.https_enabled = true;
    meta.auto_renew = true;
    meta.challenge_type = "http-01";
    meta.issued_at = CertificateStore::timestamp_utc();
    meta.created_at = meta.issued_at;
    meta.updated_at = meta.issued_at;

    auto save_result = store_.save_all(site_id, meta, fullchain_pem, privkey_pem, "");
    if (!save_result.success) return save_result;

    return {true, "Certificate issued for " + domain};
}

} // namespace containercp::ssl
