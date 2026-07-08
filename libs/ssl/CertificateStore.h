#ifndef CONTAINERCP_SSL_CERTIFICATE_STORE_H
#define CONTAINERCP_SSL_CERTIFICATE_STORE_H

#include "core/OperationResult.h"
#include "logger/Logger.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::ssl {

class CertificateStore {
public:
    struct Metadata {
        int version = 1;
        uint64_t site_id = 0;
        std::string provider_id;
        std::string certificate_type = "pem";
        std::string status = "http_only";

        std::vector<std::string> domains;

        std::string issued_at;
        std::string expires_at;
        std::string renew_after;

        bool https_enabled = false;
        bool redirect_enabled = false;
        bool auto_renew = true;

        std::string challenge_type;
        std::string last_validation;
        std::string last_error;
        int renew_attempts = 0;

        std::string fingerprint_sha256;
        std::string serial_number;
        std::string issuer;
        std::string subject;

        std::string created_at;
        std::string updated_at;
    };

    struct ValidationResult {
        bool valid = true;
        std::vector<std::string> warnings;
        std::vector<std::string> errors;
    };

    CertificateStore(logger::Logger& logger, const std::string& ssl_root);

    std::string site_dir(uint64_t site_id) const;
    std::string metadata_path(uint64_t site_id) const;
    std::string fullchain_path(uint64_t site_id) const;
    std::string privkey_path(uint64_t site_id) const;
    std::string chain_path(uint64_t site_id) const;

    bool ensure_site_dir(uint64_t site_id);

    bool metadata_exists(uint64_t site_id) const;
    bool certificate_files_exist(uint64_t site_id) const;

    bool save_metadata(uint64_t site_id, const Metadata& meta);
    Metadata load_metadata(uint64_t site_id);

    bool save_fullchain(uint64_t site_id, const std::string& pem_data);
    bool save_privkey(uint64_t site_id, const std::string& pem_data);
    bool save_chain(uint64_t site_id, const std::string& pem_data);

    core::OperationResult save_all(uint64_t site_id, const Metadata& meta,
                                    const std::string& fullchain_pem,
                                    const std::string& privkey_pem,
                                    const std::string& chain_pem);

    std::string load_fullchain(uint64_t site_id);
    std::string load_privkey(uint64_t site_id);
    std::string load_chain(uint64_t site_id);

    bool remove_all(uint64_t site_id);
    std::vector<uint64_t> enumerate();

    ValidationResult validate(uint64_t site_id);

    static std::string timestamp_utc();
    static std::string domains_to_string(const std::vector<std::string>& domains);
    static std::vector<std::string> string_to_domains(const std::string& str);

private:
    bool atomic_write(const std::string& path, const std::string& content, int mode);
    std::string read_file(const std::string& path) const;
    std::string metadata_to_json(const Metadata& meta) const;
    Metadata metadata_from_json(const std::string& json) const;
    std::string escape_json(const std::string& s) const;
    std::string parse_json_string(const std::string& json, size_t& pos) const;
    std::string parse_json_value(const std::string& json, size_t& pos) const;
    void skip_whitespace(const std::string& json, size_t& pos) const;

    logger::Logger& logger_;
    std::string ssl_root_;
};

} // namespace containercp::ssl

#endif // CONTAINERCP_SSL_CERTIFICATE_STORE_H
