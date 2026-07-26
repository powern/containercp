#ifndef CONTAINERCP_ACCESS_SSHD_AUTHORIZED_KEYS_WRITER_H
#define CONTAINERCP_ACCESS_SSHD_AUTHORIZED_KEYS_WRITER_H

#include "core/OperationResult.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace containercp::access {

struct AccessKey;

// Manages per-user authorized_keys files at:
//   /srv/containercp/ssh/authorized_keys/<linux-username>
//
// Keys are sourced from persisted validated AccessKey records.
// Each key line is prefixed with "restrict".
// File is root-owned, mode 0600, atomic replace, no symlink.
// File is removed when the final key for the user is removed.
class SshdAuthorizedKeysWriter {
public:
    using LoadKeysFn = std::function<std::vector<AccessKey>(uint64_t access_user_id)>;

    SshdAuthorizedKeysWriter(const std::string& keys_root = "/srv/containercp/ssh/authorized_keys");

    // Build a sorted, deduplicated, restrict-prefixed authorized_keys content
    // from a list of AccessKey records.
    static std::string render_content(const std::vector<AccessKey>& keys);

    // Write authorized_keys for a given linux username.
    // Reads keys via load_keys_fn, renders, validates, writes atomically.
    // Deletes the file if no keys remain.
    // Creates parent directories if needed (root-owned 0755).
    core::OperationResult write(uint64_t access_user_id,
                                const std::string& linux_username,
                                LoadKeysFn load_keys_fn);

    // Remove the authorized_keys file for a linux username.
    core::OperationResult remove(const std::string& linux_username);

    // Validate an existing authorized_keys file (mode, owner, no symlink).
    core::OperationResult validate_file(const std::string& linux_username) const;

    std::string keys_root() const { return keys_root_; }
    std::string keys_path(const std::string& linux_username) const {
        return keys_root_ + "/" + linux_username;
    }

private:
    std::string keys_root_;
};

} // namespace containercp::access

#endif
