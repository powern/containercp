#include "access/SshdDiscovery.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace containercp::access {
namespace {

constexpr const char* kSshdConfigTemplate = R"(Include /etc/ssh/sshd_config.d/*.conf
Match Group containercp-sftp
    ChrootDirectory /srv/containercp/users/%u
    ForceCommand internal-sftp
    PasswordAuthentication no
    PubkeyAuthentication yes
    AuthorizedKeysFile /srv/containercp/ssh/authorized_keys/%u
    PermitTTY no
    AllowTcpForwarding no
    AllowAgentForwarding no
    X11Forwarding no
    PermitTunnel no
    GatewayPorts no
)";

bool write_file_atomic(const std::string& path, const std::string& content, mode_t mode) {
    std::string tmp = path + ".tmp." + std::to_string(::getpid());
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!out) { std::error_code ec; std::filesystem::remove(tmp, ec); return false; }
    }
    if (::chmod(tmp.c_str(), mode) != 0) { std::error_code ec; std::filesystem::remove(tmp, ec); return false; }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) { std::error_code ec; std::filesystem::remove(tmp, ec); return false; }
    return true;
}

core::OperationResult run_sshd(const runtime::CommandExecutor& exec,
                                const std::string& sshd_bin,
                                const std::vector<std::string>& args,
                                int timeout = 15,
                                size_t max_out = 65536) {
    std::vector<std::string> full_args = {sshd_bin};
    full_args.insert(full_args.end(), args.begin(), args.end());
    auto result = exec.run_safe(full_args, "", timeout, max_out);
    std::string msg = result.err.empty() ? result.out : result.err;
    return {result.exit_code == 0, msg, result.out};
}

} // namespace

// ── SshdVersion parsing ──

SshdVersion parse_sshd_version(const std::string& version_output) {
    SshdVersion v;
    auto pos = version_output.find("OpenSSH_");
    if (pos == std::string::npos) {
        v.error = "no OpenSSH_ marker found in version output";
        return v;
    }
    pos += 8;
    if (pos >= version_output.size()) {
        v.error = "truncated version output after OpenSSH_";
        return v;
    }
    auto dot = version_output.find('.', pos);
    if (dot == std::string::npos || dot == pos) {
        v.error = "no dot separator in version";
        return v;
    }
    auto parse_int = [](const std::string& s, size_t start, size_t end, int& out) -> bool {
        if (start >= end) return false;
        std::string part = s.substr(start, end - start);
        if (part.empty()) return false;
        char* endp = nullptr;
        long val = std::strtol(part.c_str(), &endp, 10);
        if (*endp != '\0' || val < 0 || val > 999) return false;
        out = static_cast<int>(val);
        return true;
    };
    if (!parse_int(version_output, pos, dot, v.major)) {
        v.error = "failed to parse major version";
        return v;
    }
    pos = dot + 1;
    size_t minor_end = pos;
    while (minor_end < version_output.size() &&
           version_output[minor_end] >= '0' && version_output[minor_end] <= '9') {
        ++minor_end;
    }
    if (minor_end == pos) {
        v.error = "empty minor version";
        return v;
    }
    if (!parse_int(version_output, pos, minor_end, v.minor)) {
        v.error = "failed to parse minor version";
        return v;
    }
    pos = minor_end;
    if (pos < version_output.size() && version_output[pos] == 'p') {
        ++pos;
        size_t patch_start = pos;
        while (pos < version_output.size() &&
               version_output[pos] >= '0' && version_output[pos] <= '9') {
            ++pos;
        }
        if (pos > patch_start) {
            (void)parse_int(version_output, patch_start, pos, v.patch);
        }
    }
    v.valid = true;
    return v;
}

// ── TempSshdConfig ──

TempSshdConfig::TempSshdConfig(const std::string& content,
                                const std::string& temp_dir) {
    std::error_code ec;
    std::filesystem::create_directories(temp_dir, ec);
    path_ = temp_dir + "/probe_config";
    std::filesystem::remove(path_, ec);
    if (write_file_atomic(path_, content, 0644)) {
        valid_ = true;
    } else {
        path_.clear();
    }
}

TempSshdConfig::~TempSshdConfig() { cleanup(); }

void TempSshdConfig::cleanup() {
    if (!path_.empty()) {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
        path_.clear();
    }
    valid_ = false;
}

TempSshdConfig::TempSshdConfig(TempSshdConfig&& other) noexcept
    : path_(std::move(other.path_)), valid_(other.valid_) {
    other.path_.clear();
    other.valid_ = false;
}

TempSshdConfig& TempSshdConfig::operator=(TempSshdConfig&& other) noexcept {
    if (this != &other) {
        cleanup();
        path_ = std::move(other.path_);
        valid_ = other.valid_;
        other.path_.clear();
        other.valid_ = false;
    }
    return *this;
}

// ── SshdDiscoveryResult ──

core::OperationResult SshdDiscoveryResult::to_operation_result() const {
    std::string msg;
    if (success) {
        msg = "sshd discovery succeeded: OpenSSH " + std::to_string(version.major)
            + "." + std::to_string(version.minor)
            + " at " + sshd_path;
    } else {
        msg = "sshd discovery failed: ";
        if (!errors.empty()) {
            for (size_t i = 0; i < errors.size() && i < 3; ++i) {
                if (i > 0) msg += "; ";
                msg += errors[i];
            }
        } else {
            msg += "unknown error";
        }
    }
    return {success, msg, ""};
}

// ── SshdDiscovery implementation ──

bool SshdDiscovery::validate_executable_identity(const std::string& path, std::string& error) const {
    struct stat st{};
    if (::lstat(path.c_str(), &st) != 0) {
        error = "sshd executable missing: " + path;
        return false;
    }
    if (S_ISLNK(st.st_mode)) {
        error = "sshd executable is a symlink: " + path;
        return false;
    }
    if (!S_ISREG(st.st_mode)) {
        error = "sshd executable is not a regular file: " + path;
        return false;
    }
    if (st.st_uid != 0) {
        error = "sshd executable not root-owned: " + path;
        return false;
    }
    if ((st.st_mode & S_IXUSR) == 0) {
        error = "sshd executable not executable: " + path;
        return false;
    }
    if ((st.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        error = "sshd executable writable by group or world: " + path;
        return false;
    }
    return true;
}

bool SshdDiscovery::is_supported_version(const SshdVersion& v) const {
    if (!v.valid) return false;
    if (v.major < config_.min_major_version) return false;
    if (v.major == config_.min_major_version && v.minor < config_.min_minor_version) return false;
    return true;
}

core::OperationResult SshdDiscovery::verify_sshd_executable(std::string& out_path) const {
    std::string found;
    for (const auto& candidate : config_.approved_paths) {
        std::string err;
        if (validate_executable_identity(candidate, err)) {
            if (!found.empty()) {
                return {false, "multiple approved sshd candidates found: " + found + " and " + candidate, ""};
            }
            found = candidate;
        }
    }
    if (found.empty()) {
        std::string joined;
        for (const auto& p : config_.approved_paths) {
            if (!joined.empty()) joined += ", ";
            joined += p;
        }
        return {false, "no approved sshd executable found in: " + joined, ""};
    }
    out_path = found;
    return {true, out_path, ""};
}

std::string SshdDiscovery::detect_sshd_version_string() const {
    auto result = executor_.run_safe({"/usr/bin/ssh", "-V"}, "", 10, 4096);
    if (!result.err.empty()) return result.err;
    if (!result.out.empty()) return result.out;
    return {};
}

SshdVersion SshdDiscovery::parse_version(const std::string& raw) const {
    return parse_sshd_version(raw);
}

SshdServiceInfo SshdDiscovery::discover_service() const {
    return detect_systemd_service(executor_);
}

SshdConfigInfo SshdDiscovery::discover_config() const {
    return discover_config_for(executor_, config_);
}

SshdDirectiveSupport SshdDiscovery::discover_directives() const {
    return discover_directives_for(executor_, config_);
}

bool SshdDiscovery::test_internal_sftp() const {
    return test_internal_sftp_for(executor_, config_);
}

bool SshdDiscovery::test_syntax_validation() const {
    return test_syntax_validation_for(executor_, config_);
}

bool SshdDiscovery::test_effective_config() const {
    return test_effective_config_for(executor_, config_);
}

bool SshdDiscovery::test_match_group_request() const {
    return test_match_group_request_for(executor_, config_);
}

bool SshdDiscovery::test_restrict_option() const {
    return test_restrict_option_for(executor_, config_);
}

// ── Static helper implementations ──

SshdConfigInfo SshdDiscovery::discover_config_for(
    const runtime::CommandExecutor& exec, const Config& cfg) {
    SshdConfigInfo info;
    info.main_config_path = cfg.main_config_default;

    struct stat st{};
    if (::lstat(info.main_config_path.c_str(), &st) == 0) {
        if (!S_ISREG(st.st_mode)) {
            info.current_config_error = "main config not a regular file";
            return info;
        }
        if (st.st_uid != 0) {
            info.current_config_error = "main config not root-owned";
            return info;
        }
    } else {
        info.current_config_error = "main config not found";
        return info;
    }

    {
        std::ifstream in(info.main_config_path);
        if (in) {
            std::string line;
            while (std::getline(in, line)) {
                auto first = line.find_first_not_of(" \t");
                if (first == std::string::npos) continue;
                if (line.substr(first, 8) == "Include ") {
                    if (line.find(cfg.managed_include_dir) != std::string::npos) {
                        info.include_directive_present = true;
                    }
                    break;
                }
            }
        }
    }

    if (::lstat(cfg.managed_include_dir.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            info.include_dir_exists = true;
        }
    }

    {
        auto result = run_sshd(exec, cfg.get_sshd_bin(), {"-T"}, 15, 65536);
        if (result.success) {
            info.include_effective = (result.output.find("include") != std::string::npos ||
                                       result.output.find("Include") != std::string::npos);
        }
    }

    {
        auto result = run_sshd(exec, cfg.get_sshd_bin(), {"-t"}, 15, 4096);
        if (result.success) {
            info.current_config_valid = true;
        } else {
            info.current_config_error = "sshd -t failed: " + result.message;
        }
    }

    return info;
}

SshdDirectiveSupport SshdDiscovery::discover_directives_for(
    const runtime::CommandExecutor& exec, const Config& cfg) {
    SshdDirectiveSupport ds;

    struct DirectiveProbe {
        const char* directive;
        const char* value;
        bool* out;
    };

    DirectiveProbe probes[] = {
        {"Match Group", "unknown_probe_group_containercp", &ds.match_group},
        {"ChrootDirectory", "/tmp", &ds.chroot_directory},
        {"ForceCommand", "internal-sftp", &ds.force_command_internal_sftp},
        {"PasswordAuthentication", "no", &ds.password_authentication},
        {"PubkeyAuthentication", "yes", &ds.pubkey_authentication},
        {"AuthorizedKeysFile", "/dev/null", &ds.authorized_keys_file},
        {"PermitTTY", "no", &ds.permit_tty},
        {"AllowTcpForwarding", "no", &ds.allow_tcp_forwarding},
        {"AllowAgentForwarding", "no", &ds.allow_agent_forwarding},
        {"X11Forwarding", "no", &ds.x11_forwarding},
        {"PermitTunnel", "no", &ds.permit_tunnel},
        {"GatewayPorts", "no", &ds.gateway_ports},
    };

    for (auto& probe : probes) {
        std::string content = "# probe\nMatch Group unknown_probe_group_containercp\n    ";
        content += std::string(probe.directive) + " " + std::string(probe.value) + "\n";
        TempSshdConfig temp_cfg(content, cfg.temp_dir);
        if (!temp_cfg.valid()) {
            *probe.out = false;
            continue;
        }
        auto result = run_sshd(exec, cfg.get_sshd_bin(),
                               {"-t", "-f", temp_cfg.path()}, 15, 4096);
        *probe.out = (result.success);
    }

    ds.restrict_option = test_restrict_option_for(exec, cfg);
    return ds;
}

bool SshdDiscovery::test_internal_sftp_for(
    const runtime::CommandExecutor& exec, const Config& cfg) {
    std::string content = "Subsystem sftp internal-sftp\nForceCommand internal-sftp\n";
    TempSshdConfig temp_cfg(content, cfg.temp_dir);
    if (!temp_cfg.valid()) return false;
    auto result = run_sshd(exec, cfg.get_sshd_bin(),
                           {"-t", "-f", temp_cfg.path()}, 15, 4096);
    return result.success;
}

bool SshdDiscovery::test_syntax_validation_for(
    const runtime::CommandExecutor& exec, const Config& cfg) {
    auto result = run_sshd(exec, cfg.get_sshd_bin(), {"-t"}, 15, 4096);
    return result.success;
}

bool SshdDiscovery::test_effective_config_for(
    const runtime::CommandExecutor& exec, const Config& cfg) {
    auto result = run_sshd(exec, cfg.get_sshd_bin(), {"-T"}, 15, 65536);
    return result.success;
}

bool SshdDiscovery::test_match_group_request_for(
    const runtime::CommandExecutor& exec, const Config& cfg) {
    auto result = run_sshd(exec, cfg.get_sshd_bin(),
                           {"-T", "-C", "user=au-probe", "host=localhost",
                            "addr=127.0.0.1", "laddr=127.0.0.1", "lport=22"},
                           15, 65536);
    return result.success;
}

bool SshdDiscovery::test_restrict_option_for(
    const runtime::CommandExecutor& exec, const Config& cfg) {
    std::string content = "Match Group unknown_probe_group_containercp\n"
                          "    AuthorizedKeysFile /dev/null\n";
    TempSshdConfig temp_cfg(content, cfg.temp_dir);
    if (!temp_cfg.valid()) return false;
    auto result = run_sshd(exec, cfg.get_sshd_bin(),
                           {"-t", "-f", temp_cfg.path()}, 15, 4096);
    return result.success;
}

SshdDiscoveryResult SshdDiscovery::discover() {
    SshdDiscoveryResult result;

    if (!config_.approved_paths.empty()) {
        auto exec_check = verify_sshd_executable(result.sshd_path);
        if (!exec_check.success) {
            result.success = false;
            result.recoverable = false;
            result.errors.push_back(exec_check.message);
            return result;
        }
    } else {
        result.sshd_path = config_.approved_paths[0];
    }

    auto raw_version = detect_sshd_version_string();
    result.version = parse_version(raw_version);
    if (!result.version.valid) {
        result.success = false;
        result.recoverable = false;
        result.errors.push_back("version detection failed: " + result.version.error);
        return result;
    }
    if (!is_supported_version(result.version)) {
        result.success = false;
        result.recoverable = true;
        result.errors.push_back("unsupported OpenSSH version: "
            + std::to_string(result.version.major) + "."
            + std::to_string(result.version.minor));
        return result;
    }

    result.service = discover_service();
    result.config = discover_config();
    result.internal_sftp_available = test_internal_sftp();
    result.syntax_validation_available = test_syntax_validation();
    result.effective_config_available = test_effective_config();
    result.directives = discover_directives();

    if (!result.config.current_config_valid && !result.config.current_config_error.empty()) {
        result.warnings.push_back("current sshd config has issues: "
                                  + result.config.current_config_error);
    }
    if (result.service.unit_name.empty()) {
        result.warnings.push_back("ssh service unit not found");
    }
    if (!result.config.include_dir_exists) {
        result.warnings.push_back("managed include directory does not exist: "
                                  + config_.managed_include_dir);
    }

    result.success = true;
    result.recoverable = true;
    return result;
}

// ── Helper: detect systemd service ──

SshdServiceInfo detect_systemd_service(const runtime::CommandExecutor& executor) {
    SshdServiceInfo info;
    std::string candidates[] = {"ssh", "sshd"};
    for (const auto& name : candidates) {
        auto result = executor.run_safe(
            {"systemctl", "is-active", "--quiet", name}, "", 10, 1024);
        if (result.exit_code == 0) {
            info.manager = ServiceManagerType::Systemd;
            info.unit_name = name + ".service";
            info.reload_discovered = true;
            info.reload_command = "systemctl reload " + info.unit_name;
            info.health_command = "systemctl is-active " + info.unit_name;
            return info;
        }
        auto check = executor.run_safe(
            {"systemctl", "show", "-P", "Id", name + ".service"}, "", 10, 1024);
        if (check.exit_code == 0 && !check.out.empty() &&
            check.out.find(name) != std::string::npos) {
            info.manager = ServiceManagerType::Systemd;
            info.unit_name = name + ".service";
            info.reload_discovered = true;
            info.reload_command = "systemctl reload " + info.unit_name;
            info.health_command = "systemctl is-active " + info.unit_name;
            return info;
        }
    }
    info.manager = ServiceManagerType::Unknown;
    return info;
}

} // namespace containercp::access
