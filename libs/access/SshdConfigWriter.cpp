#include "access/SshdConfigWriter.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace containercp::access {
namespace {

constexpr const char* kConfigContent =
    "# Managed by ContainerCP — do not edit manually\n"
    "# ARCH-009 local SFTP user access\n"
    "\n"
    "Match Group containercp-sftp\n"
    "    ChrootDirectory /srv/containercp/users/%u\n"
    "    ForceCommand internal-sftp\n"
    "    PasswordAuthentication no\n"
    "    PubkeyAuthentication yes\n"
    "    AuthorizedKeysFile /srv/containercp/ssh/authorized_keys/%u\n"
    "    PermitTTY no\n"
    "    AllowTcpForwarding no\n"
    "    AllowAgentForwarding no\n"
    "    X11Forwarding no\n"
    "    PermitTunnel no\n"
    "    GatewayPorts no\n";

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return {};
    auto size = in.tellg();
    in.seekg(0);
    std::string content(static_cast<size_t>(size), '\0');
    in.read(content.data(), size);
    return content;
}

bool write_atomic(const std::string& path, const std::string& content, mode_t mode) {
    std::string tmp = path + ".tmp." + std::to_string(::getpid());
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!out) { std::error_code ec; std::filesystem::remove(tmp, ec); return false; }
    }
    if (::chmod(tmp.c_str(), mode) != 0) { std::error_code ec; std::filesystem::remove(tmp, ec); return false; }
    if (::rename(tmp.c_str(), path.c_str()) != 0) { std::error_code ec; std::filesystem::remove(tmp, ec); return false; }
    return true;
}

core::OperationResult run_cmd(const runtime::CommandExecutor& exec,
                               const std::vector<std::string>& args,
                               int timeout = 15,
                               size_t max_out = 65536) {
    auto r = exec.run_safe(args, "", timeout, max_out);
    std::string msg = r.err.empty() ? r.out : r.err;
    return {r.exit_code == 0, msg, r.out};
}

} // namespace

SshdConfigWriter::SshdConfigWriter(const runtime::CommandExecutor& exec,
                                    const std::string& sshd_bin,
                                    const std::string& include_path)
    : exec_(exec)
    , sshd_bin_(sshd_bin)
    , include_path_(include_path)
    , backup_path_(include_path + ".prev")
    , temp_dir_("/tmp/containercp-sshd") {}

std::string SshdConfigWriter::render_content() {
    return kConfigContent;
}

core::OperationResult SshdConfigWriter::validate_content(const std::string& content) const {
    // Write candidate to temp file
    std::error_code ec;
    std::filesystem::create_directories(temp_dir_, ec);
    std::string tmp_path = temp_dir_ + "/candidate_config";
    if (!write_atomic(tmp_path, content, 0644)) {
        return {false, "failed to write candidate config", ""};
    }
    auto r = run_cmd(exec_, {sshd_bin_, "-t", "-f", tmp_path}, 15, 4096);
    std::filesystem::remove(tmp_path, ec);
    if (!r.success) {
        return {false, "sshd -t rejected candidate: " + r.message, ""};
    }
    return {true, "", ""};
}

core::OperationResult SshdConfigWriter::ensure_config() {
    // 1. Render content
    auto content = render_content();

    // 2. Check if current file already matches
    auto current = read_file(include_path_);
    if (current == content) {
        // File exists and matches — verify it passes sshd -t
        auto vt = run_cmd(exec_, {sshd_bin_, "-t"}, 15, 4096);
        if (!vt.success) {
            // Current file is broken — reinstall
        } else {
            return {true, "sshd config already up to date", ""};
        }
    }

    // 3. Validate candidate
    auto validation = validate_content(content);
    if (!validation.success) {
        return validation;
    }

    // 4. Read current content for rollback
    std::string previous_content = read_file(include_path_);
    bool had_previous = !previous_content.empty();

    // 5. Atomically install new content
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(include_path_).parent_path(), ec);
    if (!write_atomic(include_path_, content, 0644)) {
        return {false, "failed to write sshd config: " + include_path_, ""};
    }

    // 6. Validate effective config
    auto eff = run_cmd(exec_, {sshd_bin_, "-t"}, 15, 4096);
    if (!eff.success) {
        // Rollback
        if (had_previous) {
            (void)write_atomic(include_path_, previous_content, 0644);
        } else {
            std::filesystem::remove(include_path_, ec);
        }
        return {false, "effective config validation failed after install: " + eff.message +
                " — rolled back", ""};
    }

    // 7. Reload and verify health
    auto reload_result = reload_and_verify();
    if (!reload_result.success) {
        // Rollback
        if (had_previous) {
            (void)write_atomic(include_path_, previous_content, 0644);
            (void)reload_and_verify();
        } else {
            std::filesystem::remove(include_path_, ec);
        }
        return {false, "reload failed after config install: " + reload_result.message +
                " — rolled back", ""};
    }

    return {true, "sshd config installed and active: " + include_path_, ""};
}

core::OperationResult SshdConfigWriter::remove_config() {
    std::error_code ec;
    bool existed = std::filesystem::exists(include_path_, ec);
    if (existed) {
        std::string previous_content = read_file(include_path_);
        std::filesystem::remove(include_path_, ec);
        if (ec) {
            return {false, "failed to remove sshd config: " + include_path_, ""};
        }
        auto rl = reload_and_verify();
        if (!rl.success) {
            // Restore file if reload fails
            (void)write_atomic(include_path_, previous_content, 0644);
            (void)reload_and_verify();
            return {false, "reload failed after config removal — restored: " + rl.message, ""};
        }
    }
    return {true, "sshd config removed", ""};
}

core::OperationResult SshdConfigWriter::reload_and_verify() {
    // Validate config before reload
    auto vt = run_cmd(exec_, {sshd_bin_, "-t"}, 15, 4096);
    if (!vt.success) {
        return {false, "sshd -t rejected config: " + vt.message, ""};
    }
    // Send SIGHUP to sshd via PID file (avoids systemctl D-Bus dependency)
    std::string pid_path = "/run/sshd.pid";
    std::error_code ec;
    auto pid_content = read_file(pid_path);
    auto nl = pid_content.find('\n');
    if (nl != std::string::npos) pid_content = pid_content.substr(0, nl);
    if (pid_content.empty()) {
        return {false, "cannot read sshd PID from " + pid_path, ""};
    }
    char* end = nullptr;
    long pid = std::strtol(pid_content.c_str(), &end, 10);
    if (*end != '\0' || pid <= 0) {
        return {false, "invalid sshd PID: " + pid_content, ""};
    }
    auto hl = run_cmd(exec_, {"/bin/kill", "-HUP", std::to_string(pid)}, 10, 1024);
    if (!hl.success) {
        return {false, "kill -HUP sshd (" + std::to_string(pid) + ") failed: " + hl.message, ""};
    }
    // Verify sshd is still alive
    auto check = run_cmd(exec_, {"/bin/kill", "-0", std::to_string(pid)}, 5, 1024);
    if (!check.success) {
        return {false, "sshd process " + std::to_string(pid) + " not responding after HUP", ""};
    }
    return {true, "sshd reloaded via HUP", ""};
}

core::OperationResult SshdConfigWriter::rollback(const std::string& previous_content) {
    if (previous_content.empty()) {
        // No previous state — remove the file
        return remove_config();
    }
    if (!write_atomic(include_path_, previous_content, 0644)) {
        return {false, "rollback write failed", ""};
    }
    auto rl = reload_and_verify();
    if (!rl.success) {
        return {false, "rollback reload failed: " + rl.message, ""};
    }
    return {true, "rollback successful", ""};
}

} // namespace containercp::access
