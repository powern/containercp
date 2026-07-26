#ifndef CONTAINERCP_ACCESS_SSHD_DISCOVERY_H
#define CONTAINERCP_ACCESS_SSHD_DISCOVERY_H

#include "core/OperationResult.h"
#include "runtime/CommandExecutor.h"

#include <memory>
#include <string>
#include <vector>

namespace containercp::access {

struct SshdVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;
    bool valid = false;
    std::string error;
};

// Parse "OpenSSH_9.8p1" or "OpenSSH_9.8" from ssh -V output.
// Returns valid=false on malformed input.
SshdVersion parse_sshd_version(const std::string& version_output);

enum class ServiceManagerType {
    Unknown,
    Systemd
};

struct SshdServiceInfo {
    ServiceManagerType manager = ServiceManagerType::Unknown;
    std::string unit_name;
    bool reload_discovered = false;
    std::string reload_command;
    std::string health_command;
};

struct SshdConfigInfo {
    std::string main_config_path;
    std::string include_dir;
    bool include_directive_present = false;
    bool include_dir_exists = false;
    bool include_effective = false;
    bool current_config_valid = false;
    std::string current_config_error;
};

struct SshdDirectiveSupport {
    bool match_group = false;
    bool chroot_directory = false;
    bool force_command_internal_sftp = false;
    bool password_authentication = false;
    bool pubkey_authentication = false;
    bool authorized_keys_file = false;
    bool permit_tty = false;
    bool allow_tcp_forwarding = false;
    bool allow_agent_forwarding = false;
    bool x11_forwarding = false;
    bool permit_tunnel = false;
    bool gateway_ports = false;
    bool restrict_option = false;
};

struct SshdDiscoveryResult {
    bool success = false;
    bool recoverable = false;

    std::string sshd_path;
    SshdVersion version;
    SshdServiceInfo service;
    SshdConfigInfo config;
    SshdDirectiveSupport directives;
    bool internal_sftp_available = false;
    bool syntax_validation_available = false;
    bool effective_config_available = false;

    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    core::OperationResult to_operation_result() const;
};

// RAII temporary sshd config file for validation.
// Created under a secure temp path, removed on destruction.
class TempSshdConfig {
public:
    TempSshdConfig(const std::string& content,
                   const std::string& temp_dir = "/tmp/containercp-sshd-test");
    ~TempSshdConfig();

    TempSshdConfig(const TempSshdConfig&) = delete;
    TempSshdConfig& operator=(const TempSshdConfig&) = delete;
    TempSshdConfig(TempSshdConfig&& other) noexcept;
    TempSshdConfig& operator=(TempSshdConfig&& other) noexcept;

    const std::string& path() const { return path_; }
    bool valid() const { return valid_; }

private:
    void cleanup();
    std::string path_;
    bool valid_ = false;
};

class SshdDiscovery {
public:
    struct Config {
        std::vector<std::string> approved_paths = {
            "/usr/sbin/sshd",
            "/usr/local/sbin/sshd"
        };
        std::string main_config_default = "/etc/ssh/sshd_config";
        std::string managed_include_dir = "/etc/ssh/sshd_config.d";
        std::string temp_dir = "/tmp/containercp-sshd-test";
        int min_major_version = 8;
        int min_minor_version = 0;

        std::string get_sshd_bin() const {
            return approved_paths.empty() ? "/usr/sbin/sshd" : approved_paths[0];
        }
    };

    explicit SshdDiscovery(const runtime::CommandExecutor& executor,
                           Config cfg)
        : executor_(executor), config_(std::move(cfg)) {}
    explicit SshdDiscovery(const runtime::CommandExecutor& executor)
        : executor_(executor), config_() {}

    SshdDiscoveryResult discover();

    // Instance-level steps (use configured executor and config)
    core::OperationResult verify_sshd_executable(std::string& out_path) const;
    std::string detect_sshd_version_string() const;
    SshdVersion parse_version(const std::string& raw) const;
    SshdServiceInfo discover_service() const;
    SshdConfigInfo discover_config() const;
    SshdDirectiveSupport discover_directives() const;
    bool test_internal_sftp() const;
    bool test_syntax_validation() const;
    bool test_effective_config() const;
    bool test_match_group_request() const;
    bool test_restrict_option() const;

    // Static versions for use in privileged tests (pass executor explicitly)
    static SshdConfigInfo discover_config_for(
        const runtime::CommandExecutor& exec, const Config& cfg);
    static SshdDirectiveSupport discover_directives_for(
        const runtime::CommandExecutor& exec, const Config& cfg);
    static bool test_internal_sftp_for(
        const runtime::CommandExecutor& exec, const Config& cfg);
    static bool test_syntax_validation_for(
        const runtime::CommandExecutor& exec, const Config& cfg);
    static bool test_effective_config_for(
        const runtime::CommandExecutor& exec, const Config& cfg);
    static bool test_match_group_request_for(
        const runtime::CommandExecutor& exec, const Config& cfg);
    static bool test_restrict_option_for(
        const runtime::CommandExecutor& exec, const Config& cfg);

private:
    bool validate_executable_identity(const std::string& path, std::string& error) const;
    bool is_supported_version(const SshdVersion& v) const;

    const runtime::CommandExecutor& executor_;
    Config config_;
};

// Helper: detect whether systemctl is available and a unit exists.
SshdServiceInfo detect_systemd_service(const runtime::CommandExecutor& executor);

} // namespace containercp::access

#endif // CONTAINERCP_ACCESS_SSHD_DISCOVERY_H
