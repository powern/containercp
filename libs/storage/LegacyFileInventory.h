#ifndef CONTAINERCP_STORAGE_LEGACY_FILE_INVENTORY_H
#define CONTAINERCP_STORAGE_LEGACY_FILE_INVENTORY_H

#include <string>
#include <vector>

namespace containercp::storage {

struct LegacyFileInfo {
    std::string filename;
    bool required;
};

// Authoritative physical inventory — used by importer, archive, verification, tests.
// Single source of truth for recognized legacy TXT files (19 total).
inline const std::vector<LegacyFileInfo>& legacy_file_inventory() {
    static const std::vector<LegacyFileInfo> inv = {
        {"nodes.db", true}, {"php_versions.db", true}, {"profiles.db", true},
        {"template_profiles.db", false}, {"users.db", true}, {"sites.db", true},
        {"domains.db", true}, {"databases.db", true}, {"backups.db", true},
        {"reverse_proxies.db", true}, {"access_users.db", false},
        {"access_grants.db", false}, {"auth_users.db", false},
        {"ssl_certificates.db", false}, {"mail_domains.db", false},
        {"mail_mailboxes.db", false}, {"mail_aliases.db", false},
        {"mail_state.db", false}, {"mail_smarthost.db", false}
    };
    return inv;
}

} // namespace containercp::storage

#endif
