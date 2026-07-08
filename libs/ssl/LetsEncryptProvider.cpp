#include "LetsEncryptProvider.h"

#include <chrono>
#include <curl/curl.h>
#include <fstream>
#include <sstream>

namespace containercp::ssl {

// libcurl write callback for local verification
static size_t verify_write_cb(char* data, size_t size, size_t nmemb, void* buf) {
    if (!data || !buf) return 0;
    static_cast<std::string*>(buf)->append(data, size * nmemb);
    return size * nmemb;
}

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

    // Try to resolve domain to site_id from CertificateStore
    // The caller should have saved a placeholder metadata with site_id
    auto site_ids = store_.enumerate();
    for (auto sid : site_ids) {
        auto meta_result = store_.load_metadata(sid);
        if (meta_result.success) {
            for (const auto& d : meta_result.metadata.domains) {
                if (d == domain) {
                    std::vector<std::string> domains = meta_result.metadata.domains;
                    return issue_certificate(sid, domain, domains);
                }
            }
        }
    }

    // No metadata found — caller must create metadata first
    return {false, "No certificate metadata found. Create metadata before issuing."};
}

core::OperationResult LetsEncryptProvider::renew(const std::string& domain) {
    logger_.info("LetsEncrypt", domain + ": certificate renewal started");

    // Preflight
    auto preflight = preflight_validation(domain);
    if (!preflight.success) {
        return {false, domain + ": " + preflight.message};
    }

    // Resolve site_id from CertificateStore
    auto site_ids = store_.enumerate();
    for (auto sid : site_ids) {
        auto meta_result = store_.load_metadata(sid);
        if (meta_result.success) {
            for (const auto& d : meta_result.metadata.domains) {
                if (d == domain) {
                    std::vector<std::string> domains_list = meta_result.metadata.domains;
                    logger_.info("LetsEncrypt", domain + ": found site_id=" + std::to_string(sid));
                    auto result = issue_certificate(sid, domain, domains_list);
                    if (result.success) {
                        logger_.info("LetsEncrypt", domain + ": renewal completed");
                    } else {
                        logger_.error("LetsEncrypt", domain + ": renewal failed: " + result.message);
                    }
                    return result;
                }
            }
        }
    }

    return {false, domain + ": No certificate metadata found for renewal"};
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
    // Resolve site_id from CertificateStore for correct path
    auto site_ids = store_.enumerate();
    for (auto sid : site_ids) {
        auto meta = store_.load_metadata(sid);
        if (meta.success) {
            for (const auto& d : meta.metadata.domains) {
                if (d == domain) {
                    return store_.fullchain_path(sid);
                }
            }
        }
    }
    return ssl_dir_ + "/" + domain + "/current/fullchain.pem";
}

std::string LetsEncryptProvider::key_path(const std::string& domain) const {
    auto site_ids = store_.enumerate();
    for (auto sid : site_ids) {
        auto meta = store_.load_metadata(sid);
        if (meta.success) {
            for (const auto& d : meta.metadata.domains) {
                if (d == domain) {
                    return store_.privkey_path(sid);
                }
            }
        }
    }
    return ssl_dir_ + "/" + domain + "/current/privkey.pem";
}

std::string LetsEncryptProvider::chain_path(const std::string& domain) const {
    return ssl_dir_ + "/" + domain + "/current/chain.pem";
}

void LetsEncryptProvider::set_staging(bool staging) {
    acme_.set_staging(staging);
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

    // Helper: prepend domain to error messages
    auto err = [&](const std::string& msg) -> core::OperationResult {
        return {false, domain + ": " + msg};
    };

    // Step 1: Discover ACME directory
    auto dir_result = acme_.discover_directory();
    if (!dir_result.success) return err(dir_result.message);

    // Step 2: Load or create account
    std::string account_key_path = ssl_dir_ + "/account.pem";
    auto acct_result = acme_.load_or_create_account(account_key_path);
    if (!acct_result.success) return err(acct_result.message);

    // Step 3: Create order
    AcmeClient::Order order;
    auto order_result = acme_.create_order(domains, order);
    if (!order_result.success) return err(order_result.message);

    // Step 4: Complete authorizations
    for (const auto& authz_url : order.authorizations) {
        AcmeClient::Authorization authz;
        auto authz_result = acme_.get_authorization(authz_url, authz);
        if (!authz_result.success) return err(authz_result.message);

        if (authz.domain.empty()) {
            return err("Authorization returned empty domain");
        }
        logger_.info("LetsEncrypt", "Processing authorization for " + authz.domain);

        // Find the HTTP-01 challenge
        AcmeClient::Challenge* http_challenge = nullptr;
        for (auto& ch : authz.challenges) {
            if (ch.type == "http-01") {
                http_challenge = &ch;
                break;
            }
        }
        if (!http_challenge) {
            return err("No HTTP-01 challenge available for " + authz.domain);
        }

        // Prepare challenge via ChallengeProvider
        std::string key_auth = acme_.compute_key_authorization(http_challenge->token);
        logger_.info("LetsEncrypt", "key_authorization=" + key_auth);
        auto prep_result = challenge_.prepare(authz.domain, http_challenge->token, key_auth);
        if (!prep_result.success) return err(prep_result.message);

        // Local verification: fetch challenge via HTTP and compare
        {
            std::string challenge_url = "http://" + authz.domain + "/.well-known/acme-challenge/" + http_challenge->token;
            logger_.info("LetsEncrypt", "Verifying challenge at " + challenge_url);
            CURL* curl = curl_easy_init();
            if (curl) {
                std::string resp_body;
                curl_easy_setopt(curl, CURLOPT_URL, challenge_url.c_str());
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, verify_write_cb);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp_body);
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
                CURLcode cr = curl_easy_perform(curl);
                long http_code = 0;
                if (cr == CURLE_OK)
                    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
                curl_easy_cleanup(curl);
                logger_.info("LetsEncrypt", "Challenge HTTP status=" + std::to_string(http_code)
                             + " body='" + resp_body + "' expected='" + key_auth + "'");
            }
        }

        // Respond to challenge (signal ACME server that we're ready)
        auto resp_result = acme_.respond_to_challenge(http_challenge->url);
        if (!resp_result.success) return err(resp_result.message);

        // Poll for completion
        std::string challenge_status;
        auto poll_result = acme_.poll_challenge(http_challenge->url, challenge_status);
        if (!poll_result.success) return err(poll_result.message);

        // On invalid, log full response
        if (challenge_status == "invalid") {
            // Re-fetch challenge status for full details
            AcmeClient::Authorization debug_authz;
            acme_.get_authorization(authz.url, debug_authz);
        }

        // Clean up challenge tokens
        challenge_.cleanup(authz.domain, http_challenge->token);

        if (challenge_status != "valid") {
            return err("ACME challenge failed: " + challenge_status);
        }
    }

    // Step 5: Generate CSR and key pair, then finalize
    std::string csr_key_path = ssl_dir_ + "/" + std::to_string(site_id) + "/privkey.pem";
    std::string csr = AcmeClient::generate_csr(domain, csr_key_path);

    std::string cert_url;
    auto final_result = acme_.finalize_order(order.finalize_url, order.url, csr, cert_url);
    if (!final_result.success) return err(final_result.message);

    // Step 6: Download certificate from ACME
    logger_.info("LetsEncrypt", domain + ": downloading certificate");
    std::string fullchain_pem;
    auto dl_result = acme_.download_certificate(cert_url, fullchain_pem);
    if (!dl_result.success) return err("download: " + dl_result.message);
    logger_.info("LetsEncrypt", domain + ": certificate downloaded (" + std::to_string(fullchain_pem.size()) + " bytes)");

    // Step 7: Read private key generated during CSR creation
    logger_.info("LetsEncrypt", domain + ": reading private key from " + csr_key_path);
    std::string privkey_pem;
    {
        std::ifstream key_file(csr_key_path);
        if (!key_file.is_open()) {
            return err("Failed to read private key from " + csr_key_path);
        }
        std::stringstream ss;
        ss << key_file.rdbuf();
        privkey_pem = ss.str();
        logger_.info("LetsEncrypt", domain + ": private key read (" + std::to_string(privkey_pem.size()) + " bytes)");
    }

    // Step 8: Store certificate, key, and metadata via CertificateStore
    logger_.info("LetsEncrypt", domain + ": saving certificate to CertificateStore");
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
    // ACME certificates are valid for 90 days. Set expires_at and renew_after
    // so the scheduler renews 30 days before expiry.
    {
        auto now = std::chrono::system_clock::now();
        auto exp_tp = now + std::chrono::hours(90 * 24);
        auto renew_tp = now + std::chrono::hours(60 * 24);
        auto fmt = [](time_t tt) {
            struct tm tm_buf;
            gmtime_r(&tt, &tm_buf);
            char buf[24];
            strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
            return std::string(buf);
        };
        meta.expires_at = fmt(std::chrono::system_clock::to_time_t(exp_tp));
        meta.renew_after = fmt(std::chrono::system_clock::to_time_t(renew_tp));
    }

    auto save_result = store_.save_all(site_id, meta, fullchain_pem, privkey_pem, "");
    if (!save_result.success) return err("save: " + save_result.message);
    logger_.info("LetsEncrypt", domain + ": certificate saved to /srv/containercp/ssl/" + std::to_string(site_id));

    // Step 9: Clean up challenge files (already done in authorization loop)

    logger_.info("LetsEncrypt", "Certificate issued for " + domain);
    return {true, "Certificate issued for " + domain};
}

} // namespace containercp::ssl
