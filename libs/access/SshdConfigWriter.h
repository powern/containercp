#ifndef CONTAINERCP_ACCESS_SSHD_CONFIG_WRITER_H
#define CONTAINERCP_ACCESS_SSHD_CONFIG_WRITER_H

#include "core/OperationResult.h"
#include "runtime/CommandExecutor.h"

#include <string>

namespace containercp::access {

// Manages the Debian-only sshd include file:
//   /etc/ssh/sshd_config.d/90-containercp-sftp.conf
//
// Renders a single Match Group containercp-sftp block.
// Validates with sshd -t, installs atomically, reloads ssh.service,
// rolls back on any failure.
class SshdConfigWriter {
public:
    SshdConfigWriter(const runtime::CommandExecutor& exec,
                     const std::string& sshd_bin = "/usr/sbin/sshd",
                     const std::string& include_path = "/etc/ssh/sshd_config.d/90-containercp-sftp.conf");

    // Returns the rendered config content (no side effects).
    static std::string render_content();

    // Ensure the managed config is installed and valid.
    // Idempotent: only writes if content differs from current file.
    // Transaction: render → sshd -t → backup → atomic replace → sshd -t → reload → health → rollback on failure.
    core::OperationResult ensure_config();

    // Remove the managed config file and reload sshd.
    core::OperationResult remove_config();

    // Validate a candidate config string with sshd -t.
    core::OperationResult validate_content(const std::string& content) const;

    // Reload ssh.service and verify health.
    core::OperationResult reload_and_verify();

    // Rollback: restore previous content and reload.
    core::OperationResult rollback(const std::string& previous_content);

private:
    const runtime::CommandExecutor& exec_;
    std::string sshd_bin_;
    std::string include_path_;
    std::string backup_path_;
    std::string temp_dir_;
};

} // namespace containercp::access

#endif
