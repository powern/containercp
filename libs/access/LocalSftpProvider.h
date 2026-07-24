#ifndef CONTAINERCP_ACCESS_LOCAL_SFTP_PROVIDER_H
#define CONTAINERCP_ACCESS_LOCAL_SFTP_PROVIDER_H

#include "access/AccessProvider.h"
#include "access/FilesystemPermissionInspector.h"
#include "access/SystemAccountAllocator.h"
#include "access/SystemAccountCommandRunner.h"
#include "access/SystemAccountMapping.h"
#include "access/SystemIdentityInspector.h"
#include "logger/Logger.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace containercp::access {

class LocalSftpProvider : public AccessProvider {
public:
    explicit LocalSftpProvider(logger::Logger& logger);

    void set_identity_inspector(std::shared_ptr<SystemIdentityInspector> inspector);
    void set_command_runner(std::unique_ptr<SystemAccountCommandRunner> runner);
    void set_allocator(std::unique_ptr<SystemAccountAllocator> allocator);
    void set_enabled(bool enabled);
    void set_managed_home_root(const std::string& root_path);
    void set_managed_shell(const std::string& shell);
    void set_global_sftp_group(const std::string& groupname);

    using LoadMappingsFn = std::function<std::vector<SystemAccountMapping>()>;
    using SaveMappingFn = std::function<bool(const SystemAccountMapping&)>;
    using DeleteMappingFn = std::function<bool(const std::string& entity_type, uint64_t entity_id)>;
    void set_mapping_persistence(LoadMappingsFn load, SaveMappingFn save,
                                 DeleteMappingFn remove);

    // Returns how many grants reference a given site group.
    // Used to determine if a group can be safely deleted.
    using GrantsForSiteFn = std::function<size_t(uint64_t site_id, const std::string& permission)>;
    void set_grants_lookup(GrantsForSiteFn fn);

    // --- Phase 3a: Site Grant Groups ---

    // Ensure a site-<id>-rw or site-<id>-ro group exists. Idempotent.
    core::OperationResult ensure_site_group(uint64_t site_id, const std::string& permission);

    // Add a managed user to a site group's supplementary membership.
    core::OperationResult add_user_to_site_group(const std::string& username,
                                                  uint64_t site_id,
                                                  const std::string& permission);

    // Remove a managed user from a site group.
    core::OperationResult remove_user_from_site_group(const std::string& username,
                                                       uint64_t site_id,
                                                       const std::string& permission);

    // Delete a site group if no grants reference it.
    core::OperationResult delete_site_group_if_unused(uint64_t site_id,
                                                       const std::string& permission);

    // --- Phase 3b: Permission Enforcement ---

    // Callback to resolve a site_id to its filesystem root path.
    using SiteRootFn = std::function<std::string(uint64_t site_id)>;
    void set_site_root_resolver(SiteRootFn fn);

    // Apply directory ownership and mode. Internally resolves site_root, validates
    // path, verifies group ownership, captures original state, applies chgrp+chmod,
    // verifies postconditions, and rolls back on failure.
    core::OperationResult apply_directory_permissions(uint64_t site_id,
                                                       const std::string& permission);

    // Apply/remove read-only POSIX ACL. Internally resolves path and group.
    core::OperationResult apply_read_only_acl(uint64_t site_id);
    core::OperationResult remove_read_only_acl(uint64_t site_id);

    // Filesystem inspector is mandatory for Phase 3b operations.
    void set_filesystem_inspector(std::shared_ptr<FilesystemPermissionInspector> inspector);

    core::OperationResult create_user(const AccessUser& user) override;
    core::OperationResult remove_user(const AccessUser& user) override;
    core::OperationResult enable_user(const AccessUser& user) override;
    core::OperationResult disable_user(const AccessUser& user) override;
    core::OperationResult list_users() override;
    core::OperationResult show_user(const AccessUser& user) override;

private:
    bool disabled_result(core::OperationResult& out, const char* op) const;

    // Returns a value copy — no pointers, no static caches.
    std::optional<SystemAccountMapping> find_mapping(const std::string& entity_type,
                                                     uint64_t entity_id) const;
    bool verify_ownership(const SystemAccountMapping& mapping,
                          const ObservedUser& observed) const;
    bool ensure_global_sftp_group();
    void rollback_create(const std::string& username, const std::string& groupname,
                         uint64_t access_user_id);
    void restore_acl(const FsPermissionState& prev, const std::string& path,
                     const std::string& groupname, core::OperationResult& out);

    // Resolve entity_type from permission string
    static std::string site_group_entity_type(const std::string& permission);
    // Build site group name: "site-<id>-rw" or "site-<id>-ro"
    static std::string site_group_name(uint64_t site_id, const std::string& permission);

    logger::Logger& logger_;
    std::shared_ptr<SystemIdentityInspector> inspector_;
    std::unique_ptr<SystemAccountCommandRunner> runner_;
    std::unique_ptr<SystemAccountAllocator> allocator_;
    LoadMappingsFn load_mappings_;
    SaveMappingFn save_mapping_;
    DeleteMappingFn delete_mapping_;

    GrantsForSiteFn grants_lookup_;

    SiteRootFn site_root_resolver_;

    std::shared_ptr<FilesystemPermissionInspector> fs_inspector_;

    bool enabled_ = false;
    std::string managed_home_root_ = "/srv/containercp/users";
    std::string managed_shell_ = "/usr/sbin/nologin";
    std::string global_sftp_group_ = "containercp-sftp";
};

} // namespace containercp::access

#endif
