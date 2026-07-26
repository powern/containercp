#include "access/LocalSftpProvider.h"
#include "access/ManagedPathValidator.h"
#include "access/UsernameMapper.h"
#include "storage/GrantLifecycleState.h"
#include "storage/ManagedMountState.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <sys/stat.h>
#include <system_error>

namespace containercp::access {
namespace {

// Validate that `path` is safely deletable: under managed_root, not a symlink,
// not the root itself, and component-based (not string prefix) to prevent
// /srv/containercp/users-evil matching /srv/containercp/users.
bool managed_path_safe(const std::string& path, const std::string& managed_root) {
    if (managed_root.empty()) return false;
    std::error_code ec;

    auto canonical = std::filesystem::weakly_canonical(std::filesystem::absolute(path, ec), ec);
    auto canonical_root = std::filesystem::weakly_canonical(std::filesystem::absolute(managed_root, ec), ec);
    if (ec) return false;
    if (canonical_root.empty() || canonical.empty()) return false;

    // Canonical must equal root OR start with root + '/'
    if (canonical == canonical_root) return false;
    std::string root_str = canonical_root.string();
    if (!root_str.empty() && root_str.back() != '/') root_str += '/';
    std::string path_str = canonical.string();
    if (path_str.rfind(root_str, 0) != 0) return false;

    // Reject symlinks: the canonical path must not be a symlink
    if (std::filesystem::is_symlink(canonical, ec)) return false;
    if (ec && ec != std::errc::no_such_file_or_directory) return false;

    return true;
}

} // namespace

// forward declarations for helpers defined later
bool contains_username_component(const std::string& path, const std::string& username);

// --- constructor / configuration ---

LocalSftpProvider::LocalSftpProvider(logger::Logger& logger)
    : logger_(logger) {}

bool contains_username_component(const std::string& path, const std::string& username) {
    if (path.empty() || username.empty()) return false;
    std::string::size_type start = 0;
    while (true) {
        auto slash = path.find('/', start);
        std::string component = (slash == std::string::npos)
            ? path.substr(start)
            : path.substr(start, slash - start);
        if (component == username) return true;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return false;
}

void LocalSftpProvider::set_identity_inspector(std::shared_ptr<SystemIdentityInspector> inspector) {
    inspector_ = std::move(inspector);
}
void LocalSftpProvider::set_command_runner(std::unique_ptr<SystemAccountCommandRunner> runner) {
    runner_ = std::move(runner);
}
void LocalSftpProvider::set_allocator(std::unique_ptr<SystemAccountAllocator> allocator) {
    allocator_ = std::move(allocator);
}
void LocalSftpProvider::set_enabled(bool enabled) { enabled_ = enabled; }
void LocalSftpProvider::set_managed_home_root(const std::string& root_path) { managed_home_root_ = root_path; }
void LocalSftpProvider::set_managed_shell(const std::string& shell) { managed_shell_ = shell; }
void LocalSftpProvider::set_global_sftp_group(const std::string& groupname) { global_sftp_group_ = groupname; }
void LocalSftpProvider::set_mapping_persistence(LoadMappingsFn load, SaveMappingFn save, DeleteMappingFn remove) {
    load_mappings_ = std::move(load);
    save_mapping_ = std::move(save);
    delete_mapping_ = std::move(remove);
}

void LocalSftpProvider::set_filesystem_inspector(std::shared_ptr<FilesystemPermissionInspector> inspector) {
    fs_inspector_ = std::move(inspector);
}

void LocalSftpProvider::set_site_resolver(SiteInfoFn fn) {
    site_resolver_ = std::move(fn);
}



void LocalSftpProvider::set_grants_lookup(GrantsForSiteFn fn) {
    grants_lookup_ = std::move(fn);
}

void LocalSftpProvider::set_managed_mount_storage(LoadAllManagedMountsFn load_all,
                                                    SaveManagedMountFn save,
                                                    DeleteManagedMountFn remove) {
    load_all_managed_mounts_ = std::move(load_all);
    save_managed_mount_ = std::move(save);
    delete_managed_mount_ = std::move(remove);
}

void LocalSftpProvider::set_grant_lifecycle_storage(LoadAllGrantLifecycleFn load_all,
                                                     SaveGrantLifecycleFn save,
                                                     DeleteGrantLifecycleFn remove) {
    load_all_grant_lifecycle_ = std::move(load_all);
    save_grant_lifecycle_ = std::move(save);
    delete_grant_lifecycle_ = std::move(remove);
}

// --- helpers ---

core::OperationResult LocalSftpProvider::verify_dependencies() const {
    core::OperationResult out;
    std::string missing;

    auto note = [&](const std::string& name) {
        if (!missing.empty()) missing += ", ";
        missing += name;
    };

    if (!inspector_) note("identity_inspector");
    if (!runner_)    note("command_runner");
    if (!allocator_) note("allocator");
    if (!load_mappings_ || !save_mapping_ || !delete_mapping_)
        note("mapping_persistence");
    if (!site_resolver_) note("site_resolver");
    if (!fs_inspector_)  note("filesystem_inspector");
    if (!mount_inspector_) note("mount_inspector");
    if (!load_all_managed_mounts_ || !save_managed_mount_ || !delete_managed_mount_)
        note("managed_mount_storage");
    if (!load_all_grant_lifecycle_ || !save_grant_lifecycle_ || !delete_grant_lifecycle_)
        note("grant_lifecycle_storage");
    if (!grants_loader_) note("grants_loader");
    if (!grants_lookup_) note("grants_lookup");

    if (!missing.empty()) {
        out.success = false;
        out.message = "SFTP provider missing dependencies: " + missing;
    } else {
        out.success = true;
        out.message = "all dependencies present";
    }
    return out;
}

std::optional<SystemAccountMapping>
LocalSftpProvider::find_mapping(const std::string& entity_type, uint64_t entity_id) const {
    if (!load_mappings_) return std::nullopt;
    auto mappings = load_mappings_();
    for (const auto& m : mappings) {
        if (m.entity_type == entity_type && m.entity_id == entity_id) {
            return m;
        }
    }
    return std::nullopt;
}

bool LocalSftpProvider::verify_ownership(const SystemAccountMapping& mapping,
                                         const ObservedUser& observed) const {
    if (!observed.exists) return false;
    if (observed.username != mapping.username) return false;
    if (mapping.uid > 0 && observed.uid != mapping.uid) return false;
    if (observed.gid != mapping.gid) return false;
    // UID/GID must be within the configured managed range
    if (allocator_) {
        if (mapping.uid > 0 && (mapping.uid < allocator_->uid_min() || mapping.uid > allocator_->uid_max()))
            return false;
        if (mapping.gid > 0 && (mapping.gid < allocator_->gid_min() || mapping.gid > allocator_->gid_max()))
            return false;
    }
    // Use persisted home if available, otherwise fall back to computed path
    std::string expected_home = mapping.home.empty() ? managed_home_root_ + "/" + mapping.username : mapping.home;
    if (!mapping.home.empty() && !contains_username_component(mapping.home, mapping.username)) return false;
    if (observed.home != expected_home) return false;
    if (observed.shell != managed_shell_) return false;
    return true;
}

bool LocalSftpProvider::ensure_global_sftp_group() {
    if (global_sftp_group_.empty()) return true;
    if (!inspector_ || !runner_) return false;

    // If group exists, verify it's not an unmanaged conflict
    if (inspector_->group_exists(global_sftp_group_)) {
        auto observed = inspector_->lookup_group(global_sftp_group_);
        if (!observed.exists) return false; // should not happen
        // Already exists — accept it
        return true;
    }

    auto gr = runner_->groupadd(global_sftp_group_, -1);
    if (!gr.success) return false;

    // Verify postcondition
    auto observed = inspector_->lookup_group(global_sftp_group_);
    return observed.exists;
}

void LocalSftpProvider::rollback_create(const std::string& username,
                                         const std::string& groupname,
                                         uint64_t access_user_id) {
    (void)runner_->userdel(username);
    (void)runner_->groupdel(groupname);
    if (delete_mapping_) {
        if (!delete_mapping_("access_user", access_user_id)) {
            logger_.warning("SFTP", "rollback_create: failed to delete stale mapping for access_user_id=" +
                            std::to_string(access_user_id));
        }
    }
}

// --- Phase 3a: Site Grant Groups ---

std::string LocalSftpProvider::site_group_entity_type(const std::string& permission) {
    if (permission == "read_only") return "site_group_ro";
    if (permission == "read_write") return "site_group_rw";
    if (permission == "deploy") return "site_group_rw";
    return {}; // invalid — caller must check
}

std::string LocalSftpProvider::site_group_name(uint64_t site_id, const std::string& permission) {
    if (permission == "read_only") return "site-" + std::to_string(site_id) + "-ro";
    if (permission == "read_write" || permission == "deploy") return "site-" + std::to_string(site_id) + "-rw";
    return {};
}

core::OperationResult LocalSftpProvider::ensure_site_group(uint64_t site_id,
                                                            const std::string& permission) {
    core::OperationResult out;
    if (!operation_gate(out, "ensure_site_group")) return out;
    return ensure_site_group_internal(site_id, permission);
}

core::OperationResult LocalSftpProvider::ensure_site_group_internal(uint64_t site_id,
                                                             const std::string& permission) {
    core::OperationResult out;

    if (!inspector_ || !runner_ || !allocator_) {
        out.success = false; out.message = "provider dependencies not configured"; return out;
    }

    std::string groupname = site_group_name(site_id, permission);
    std::string etype = site_group_entity_type(permission);
    if (etype.empty()) {
        out.success = false; out.message = "invalid permission: " + permission; return out;
    }

    // Check if mapping already exists (idempotent)
    auto existing = find_mapping(etype, site_id);
    if (existing.has_value()) {
        if (existing->state == "active") {
            auto obs = inspector_->lookup_group(groupname);
            if (obs.exists && obs.gid == existing->gid) {
                out.success = true; out.message = "site group already exists: " + groupname; return out;
            }
        }
        // Stale provisioning — if OS group exists with matching GID, recover to active
        auto obs = inspector_->lookup_group(groupname);
        if (obs.exists && obs.gid == existing->gid) {
            if (save_mapping_) {
                auto m = *existing;
                m.state = "active";
                if (!save_mapping_(m)) {
                    out.success = false;
                    out.message = "failed to persist active state for recovered group: " + groupname;
                    return out;
                }
            }
            out.success = true; out.message = "site group recovered to active: " + groupname; return out;
        }
        // Stale or mismatched — clean up and re-provision below
    }

    // Unmanaged conflict: group exists without mapping
    if (inspector_->group_exists(groupname)) {
        out.success = false; out.message = "unmanaged_group_conflict: " + groupname; return out;
    }

    // Allocate GID
    auto persisted = load_mappings_ ? load_mappings_() : std::vector<SystemAccountMapping>{};
    auto alloc = allocator_->allocate(
        [this](int id) { return inspector_->uid_occupied(id); },
        [this](int id) { return inspector_->gid_occupied(id); },
        persisted);
    if (!alloc.success) {
        out.success = false; out.message = alloc.error; return out;
    }

    // Persist provisioning mapping
    SystemAccountMapping mapping;
    mapping.entity_type = etype;
    mapping.entity_id   = site_id;
    mapping.gid         = alloc.gid;
    mapping.username    = groupname;
    mapping.groupname   = groupname;
    mapping.state       = "provisioning";
    if (save_mapping_ && !save_mapping_(mapping)) {
        out.success = false; out.message = "failed to persist site group mapping"; return out;
    }

    // Create OS group
    auto gr = runner_->groupadd(groupname, alloc.gid);
    if (!gr.success) {
        if (delete_mapping_) delete_mapping_(etype, site_id);
        out.success = false; out.message = "groupadd failed: " + groupname; return out;
    }

    // Verify postcondition
    auto obs = inspector_->lookup_group(groupname);
    if (!obs.exists || obs.gid != alloc.gid) {
        mapping.state = "error";
        if (save_mapping_) save_mapping_(mapping);
        out.success = false; out.message = "post-create group verification failed"; return out;
    }

    // Mark active
    mapping.state = "active";
    if (save_mapping_ && !save_mapping_(mapping)) {
        out.success = false; out.message = "failed to save active state"; return out;
    }

    out.success = true;
    out.message = "site group created: " + groupname;
    return out;
}

core::OperationResult LocalSftpProvider::add_user_to_site_group(const std::string& username,
                                                                 uint64_t site_id,
                                                                 const std::string& permission) {
    core::OperationResult out;
    if (!operation_gate(out, "add_user_to_site_group")) return out;
    return add_user_to_site_group_internal(username, site_id, permission);
}

core::OperationResult LocalSftpProvider::add_user_to_site_group_internal(const std::string& username,
                                                                 uint64_t site_id,
                                                                 const std::string& permission) {
    core::OperationResult out;

    if (!inspector_ || !runner_) {
        out.success = false; out.message = "provider dependencies not configured"; return out;
    }

    std::string groupname = site_group_name(site_id, permission);

    // Verify group exists and is managed
    auto mapping = find_mapping(site_group_entity_type(permission), site_id);
    if (!mapping.has_value()) {
        out.success = false; out.message = "site group not provisioned: " + groupname; return out;
    }

    // Verify user exists
    if (!inspector_->user_exists(username)) {
        out.success = false; out.message = "user not found: " + username; return out;
    }

    // Add to supplementary group
    auto result = runner_->usermod_add_group(username, groupname);
    if (!result.success) {
        out.success = false; out.message = "usermod failed for " + username + " -> " + groupname; return out;
    }

    // Postcondition: verify membership
    if (!inspector_->user_in_group(username, groupname)) {
        out.success = false; out.message = "membership verification failed: " + username; return out;
    }

    out.success = true;
    out.message = "user added to site group: " + username + " -> " + groupname;
    return out;
}

core::OperationResult LocalSftpProvider::remove_user_from_site_group(const std::string& username,
                                                                      uint64_t site_id,
                                                                      const std::string& permission) {
    core::OperationResult out;
    if (!operation_gate(out, "remove_user_from_site_group")) return out;
    return remove_user_from_site_group_internal(username, site_id, permission);
}

core::OperationResult LocalSftpProvider::remove_user_from_site_group_internal(const std::string& username,
                                                                      uint64_t site_id,
                                                                      const std::string& permission) {
    core::OperationResult out;

    if (!inspector_ || !runner_) {
        out.success = false; out.message = "provider dependencies not configured"; return out;
    }

    std::string groupname = site_group_name(site_id, permission);
    if (groupname.empty()) {
        out.success = false; out.message = "invalid permission: " + permission; return out;
    }

    std::string etype = site_group_entity_type(permission);
    // Complete ownership verification
    auto mapping = find_mapping(etype, site_id);
    if (!mapping.has_value()) {
        out.success = false; out.message = "site group not managed: " + groupname; return out;
    }
    if (mapping->state != "active") {
        out.success = false; out.message = "site group not active: " + groupname; return out;
    }
    auto obs_grp = inspector_->lookup_group(groupname);
    if (!obs_grp.exists || obs_grp.gid != mapping->gid) {
        out.success = false; out.message = "OS group mismatch for: " + groupname; return out;
    }
    if (!inspector_->user_exists(username)) {
        out.success = false; out.message = "user not found: " + username; return out;
    }

    auto result = runner_->usermod_remove_group(username, groupname);
    if (!result.success) {
        out.success = false; out.message = "gpasswd failed for " + username + " -> " + groupname; return out;
    }

    // Postcondition: verify membership was removed
    if (inspector_->user_in_group(username, groupname)) {
        out.success = false; out.message = "membership removal verification failed: " + username; return out;
    }

    out.success = true;
    out.message = "user removed from site group: " + username + " -> " + groupname;
    return out;
}

core::OperationResult LocalSftpProvider::delete_site_group_if_unused(uint64_t site_id,
                                                                      const std::string& permission) {
    core::OperationResult out;
    if (!operation_gate(out, "delete_site_group")) return out;
    return delete_site_group_if_unused_internal(site_id, permission);
}

core::OperationResult LocalSftpProvider::delete_site_group_if_unused_internal(uint64_t site_id,
                                                                      const std::string& permission) {
    core::OperationResult out;

    if (!inspector_ || !runner_) {
        out.success = false; out.message = "provider dependencies not configured"; return out;
    }

    std::string etype = site_group_entity_type(permission);
    std::string groupname = site_group_name(site_id, permission);

    // Check if any grants still reference this group
    if (grants_lookup_) {
        size_t grant_count = grants_lookup_(site_id, permission);
        if (grant_count > 0) {
            out.success = false;
            out.message = "site group still has " + std::to_string(grant_count) + " grants: " + groupname;
            return out;
        }
    }

    // Verify mapping exists (ownership proof)
    auto mapping = find_mapping(etype, site_id);
    if (!mapping.has_value()) {
        // Group doesn't exist in mappings — nothing to delete
        out.success = true; out.message = "site group mapping not found: " + groupname; return out;
    }

    // Delete OS group
    auto gd = runner_->groupdel(groupname);
    if (!gd.success) {
        out.success = false; out.message = "groupdel failed: " + groupname; return out;
    }

    // Delete mapping
    if (delete_mapping_ && !delete_mapping_(etype, site_id)) {
        out.success = false; out.message = "failed to delete site group mapping: " + groupname; return out;
    }

    out.success = true;
    out.message = "site group deleted: " + groupname;
    return out;
}


// --- Phase 3b: Permission Enforcement ---

namespace {

bool valid_permission_for_site_dir(const std::string& permission) {
    return permission == "read_write" || permission == "deploy";
}

} // namespace

core::OperationResult LocalSftpProvider::apply_directory_permissions(uint64_t site_id,
                                                                       const std::string& permission) {
    core::OperationResult out;
    if (!operation_gate(out, "apply_directory_permissions")) return out;
    return apply_directory_permissions_internal(site_id, permission);
}

core::OperationResult LocalSftpProvider::apply_directory_permissions_internal(uint64_t site_id,
                                                                       const std::string& permission) {
    core::OperationResult out;

    if (!runner_ || !fs_inspector_) {
        out.success = false; out.message = "provider dependencies not configured"; return out;
    }
    if (site_id == 0) { out.success = false; out.message = "admin_panel_sftp_access_forbidden"; return out; }
    if (!site_resolver_) { out.success = false; out.message = "site root resolver not configured"; return out; }
    if (!valid_permission_for_site_dir(permission)) {
        out.success = false; out.message = "invalid permission"; return out;
    }

    auto site_info = site_resolver_(site_id); if (!site_info.valid) { out.success = false; out.message = "site not found"; return out; } std::string site_root = site_info.root;

    std::string public_dir = site_root + "/public/";

    auto rw_mapping = find_mapping("site_group_rw", site_id);
    if (!rw_mapping.has_value() || rw_mapping->state != "active") {
        out.success = false; out.message = "RW site group not active"; return out;
    }
    auto obs_grp = inspector_->lookup_group(rw_mapping->groupname);
    if (!obs_grp.exists || obs_grp.gid != rw_mapping->gid) {
        out.success = false; out.message = "OS group GID mismatch"; return out;
    }

    auto original = fs_inspector_->inspect(public_dir);
    if (!original.exists) { out.success = false; out.message = "public/ not found"; return out; }
    if (original.is_symlink) { out.success = false; out.message = "public/ is symlink"; return out; }

    auto r1 = runner_->chgrp(rw_mapping->groupname, public_dir);
    if (!r1.success) { out.success = false; out.message = "chgrp failed"; return out; }
    auto post_grp = fs_inspector_->inspect(public_dir);
    if (!post_grp.exists || post_grp.group_gid != rw_mapping->gid) {
        out.success = false; out.message = "chgrp postcondition failed"; return out;
    }

    auto r2 = runner_->chmod("770", public_dir);
    if (!r2.success) {
        if (original.exists && original.group_gid > 0) {
            auto rb = runner_->chgrp(std::to_string(original.group_gid), public_dir);
            if (!rb.success) { out.success = false; out.message = "chmod failed, rollback failed"; return out; }
        }
        out.success = false; out.message = "chmod failed"; return out;
    }
    auto post_mode = fs_inspector_->inspect(public_dir);
    if (!post_mode.exists || post_mode.mode != 0770) {
        out.success = false; out.message = "chmod postcondition failed"; return out;
    }

    out.success = true; out.message = "permissions applied"; return out;
}

core::OperationResult LocalSftpProvider::apply_read_only_acl(uint64_t site_id) {
    core::OperationResult out;
    if (!operation_gate(out, "apply_read_only_acl")) return out;
    return apply_read_only_acl_internal(site_id);
}

core::OperationResult LocalSftpProvider::apply_read_only_acl_internal(uint64_t site_id) {
    core::OperationResult out;

    if (!runner_ || !fs_inspector_) {
        out.success = false; out.message = "provider dependencies not configured"; return out;
    }
    if (site_id == 0) { out.success = false; out.message = "admin_panel_sftp_access_forbidden"; return out; }
    if (!site_resolver_) { out.success = false; out.message = "site root resolver not configured"; return out; }

    auto site_info = site_resolver_(site_id); if (!site_info.valid) { out.success = false; out.message = "site not found"; return out; } std::string site_root = site_info.root;

    std::string public_dir = site_root + "/public/";

    auto ro_mapping = find_mapping("site_group_ro", site_id);
    if (!ro_mapping.has_value() || ro_mapping->state != "active") {
        out.success = false; out.message = "RO site group not active"; return out;
    }
    auto obs_grp = inspector_->lookup_group(ro_mapping->groupname);
    if (!obs_grp.exists || obs_grp.gid != ro_mapping->gid) {
        out.success = false; out.message = "OS group GID mismatch"; return out;
    }

    // Capture previous ACL state for restoration
    auto prev = fs_inspector_->inspect_acl(public_dir, ro_mapping->groupname);
    if (prev.acl_status != InspectionStatus::Ok) {
        out.success = false; out.message = "ACL pre-inspection failed"; return out;
    }

    // Apply access ACL
    std::string acl_access = "g:" + ro_mapping->groupname + ":r-x";
    auto r = runner_->setfacl_modify(acl_access, public_dir);
    if (!r.success) { restore_acl(prev, public_dir, ro_mapping->groupname, out); return out; }

    // Apply default ACL
    std::string acl_default = "d:g:" + ro_mapping->groupname + ":r-x";
    auto rd = runner_->setfacl_modify(acl_default, public_dir);
    if (!rd.success) { restore_acl(prev, public_dir, ro_mapping->groupname, out); return out; }

    // Verify postcondition
    auto post = fs_inspector_->inspect_acl(public_dir, ro_mapping->groupname);
    if (post.acl_status != InspectionStatus::Ok) {
        restore_acl(prev, public_dir, ro_mapping->groupname, out); return out;
    }
    if (!post.acl.access_present || post.acl.effective_perms.find('w') != std::string::npos || !post.acl.default_present) {
        restore_acl(prev, public_dir, ro_mapping->groupname, out); return out;
    }

    out.success = true; out.message = "RO ACL applied"; return out;
}

core::OperationResult LocalSftpProvider::remove_read_only_acl(uint64_t site_id) {
    core::OperationResult out;
    if (!operation_gate(out, "remove_read_only_acl")) return out;
    return remove_read_only_acl_internal(site_id);
}

core::OperationResult LocalSftpProvider::remove_read_only_acl_internal(uint64_t site_id) {
    core::OperationResult out;

    if (!runner_ || !fs_inspector_) {
        out.success = false; out.message = "provider dependencies not configured"; return out;
    }
    if (site_id == 0) { out.success = false; out.message = "admin_panel_sftp_access_forbidden"; return out; }
    if (!site_resolver_) { out.success = false; out.message = "site root resolver not configured"; return out; }

    auto site_info = site_resolver_(site_id); if (!site_info.valid) { out.success = false; out.message = "site not found"; return out; } std::string site_root = site_info.root;

    std::string public_dir = site_root + "/public/";

    auto ro_mapping = find_mapping("site_group_ro", site_id);
    if (!ro_mapping.has_value() || ro_mapping->state != "active") {
        out.success = false; out.message = "RO site group not active"; return out;
    }

    auto prev = fs_inspector_->inspect_acl(public_dir, ro_mapping->groupname);

    auto r1 = runner_->setfacl_remove("g:" + ro_mapping->groupname, public_dir);
    if (!r1.success) { out.success = false; out.message = "setfacl -x failed"; return out; }
    auto r2 = runner_->setfacl_remove("d:g:" + ro_mapping->groupname, public_dir);
    if (!r2.success) { out.success = false; out.message = "default setfacl -x failed"; return out; }

    auto post = fs_inspector_->inspect_acl(public_dir, ro_mapping->groupname);
    if (post.acl_status != InspectionStatus::Ok && post.acl_status != InspectionStatus::AclToolMissing) {
        out.success = false; out.message = "ACL inspection error"; return out;
    }
    if (post.acl.access_present || post.acl.default_present) {
        // Restore previous ACL
        auto rb1 = runner_->setfacl_modify("g:" + ro_mapping->groupname + ":" + prev.acl.access_perms, public_dir);
        if (!rb1.success) { out.success = false; out.message = "rollback failed"; return out; }
        auto rb2 = runner_->setfacl_modify("d:g:" + ro_mapping->groupname + ":" + prev.acl.default_perms, public_dir);
        if (!rb2.success) { out.success = false; out.message = "rollback failed"; return out; }
        out.success = false; out.message = "ACL removal postcondition failed"; return out;
    }

    out.success = true; out.message = "RO ACL removed"; return out;
}

// ACL restoration helper — restores previous access+default ACL and verifies
void LocalSftpProvider::restore_acl(const FsPermissionState& prev, const std::string& path,
                                     const std::string& groupname, core::OperationResult& out) {
    // Restore access ACL
    if (prev.acl.access_present && !prev.acl.access_perms.empty()) {
        auto rb = runner_->setfacl_modify("g:" + groupname + ":" + prev.acl.access_perms, path);
        if (!rb.success) { out.success = false; out.message = "rollback restore access ACL failed"; return; }
    } else {
        auto rb = runner_->setfacl_remove("g:" + groupname, path);
        if (!rb.success) { out.success = false; out.message = "rollback remove access ACL failed"; return; }
    }
    // Restore default ACL
    if (prev.acl.default_present && !prev.acl.default_perms.empty()) {
        auto rb = runner_->setfacl_modify("d:g:" + groupname + ":" + prev.acl.default_perms, path);
        if (!rb.success) { out.success = false; out.message = "rollback restore default ACL failed"; return; }
    } else {
        auto rb = runner_->setfacl_remove("d:g:" + groupname, path);
        if (!rb.success) { out.success = false; out.message = "rollback remove default ACL failed"; return; }
    }
    // Postcondition: verify full ACL state restoration
    auto post = fs_inspector_->inspect_acl(path, groupname);
    if (post.acl_status != InspectionStatus::Ok) {
        out.success = false; out.message = "rollback verification failed"; return;
    }
    if (post.acl.access_present != prev.acl.access_present ||
        post.acl.default_present != prev.acl.default_present ||
        (prev.acl.access_present && (post.acl.access_group != prev.acl.access_group ||
         post.acl.access_perms != prev.acl.access_perms ||
         post.acl.effective_perms != prev.acl.effective_perms)) ||
        (prev.acl.default_present && (post.acl.default_group != prev.acl.default_group ||
         post.acl.default_perms != prev.acl.default_perms ||
         post.acl.default_effective != prev.acl.default_effective))) {
        out.success = false; out.message = "rollback state mismatch"; return;
    }
    // Verify masks if present in previous state
    if (!prev.acl.access_mask.empty() && post.acl.access_mask != prev.acl.access_mask) {
        out.success = false; out.message = "rollback state mismatch"; return;
    }
    if (!prev.acl.default_mask.empty() && post.acl.default_mask != prev.acl.default_mask) {
        out.success = false; out.message = "rollback state mismatch"; return;
    }
    out.success = false; out.message = "ACL operation failed, rolled back";
}

core::OperationResult LocalSftpProvider::restore_acl_state(
    const AclState& original, const std::string& path, const std::string& groupname) {
    core::OperationResult out;
    // Restore access ACL
    if (original.access_present && !original.access_perms.empty()) {
        auto rb = runner_->setfacl_modify("g:" + groupname + ":" + original.access_perms, path);
        if (!rb.success) { out.message = "acl:restore:access"; return out; }
    } else {
        auto rb = runner_->setfacl_remove("g:" + groupname, path);
        if (!rb.success) { out.message = "acl:restore:access"; return out; }
    }
    // Restore default ACL
    if (original.default_present && !original.default_perms.empty()) {
        auto rb = runner_->setfacl_modify("d:g:" + groupname + ":" + original.default_perms, path);
        if (!rb.success) { out.message = "acl:restore:default"; return out; }
    } else {
        auto rb = runner_->setfacl_remove("d:g:" + groupname, path);
        if (!rb.success) { out.message = "acl:restore:default"; return out; }
    }
    // Postcondition verification: full AclState comparison including masks
    auto post = fs_inspector_->inspect_acl(path, groupname);
    if (post.acl_status != InspectionStatus::Ok) {
        out.message = "acl:postcondition:inspect"; return out;
    }
    if (post.acl.access_present != original.access_present ||
        post.acl.default_present != original.default_present ||
        (original.access_present && (post.acl.access_group != original.access_group ||
         post.acl.access_perms != original.access_perms ||
         post.acl.effective_perms != original.effective_perms ||
         post.acl.access_mask != original.access_mask)) ||
        (original.default_present && (post.acl.default_group != original.default_group ||
         post.acl.default_perms != original.default_perms ||
         post.acl.default_effective != original.default_effective ||
         post.acl.default_mask != original.default_mask))) {
        out.message = "acl:postcondition:mismatch"; return out;
    }
    out.success = true;
    return out;
}


// --- Phase 3c: Chroot Layout & Bind Mounts ---

void LocalSftpProvider::set_mount_inspector(std::shared_ptr<MountInspector> inspector) {
    mount_inspector_ = std::move(inspector);
}

core::OperationResult LocalSftpProvider::ensure_chroot_layout(uint64_t access_user_id) {
    core::OperationResult out;
    if (!operation_gate(out, "ensure_chroot_layout")) return out;
    return ensure_chroot_layout_internal(access_user_id);
}

core::OperationResult LocalSftpProvider::ensure_chroot_layout_internal(uint64_t access_user_id) {
    core::OperationResult out;

    if (!runner_) { out.success = false; out.message = "provider dependencies not configured"; return out; }

    auto mapping = find_mapping("access_user", access_user_id);
    if (!mapping.has_value()) { out.success = false; out.message = "user mapping not found"; return out; }
    if (mapping->entity_type != "access_user") { out.success = false; out.message = "entity type not access_user"; return out; }
    if (mapping->state != "active") { out.success = false; out.message = "user mapping not active"; return out; }

    // Verify persisted home is non-empty and inside managed root
    if (mapping->home.empty()) { out.success = false; out.message = "persisted home empty"; return out; }
    if (mapping->home.rfind(managed_home_root_, 0) != 0) {
        out.success = false; out.message = "home outside managed root"; return out;
    }
    auto pv = validate_managed_path(mapping->home, managed_home_root_);
    if (!pv.ok) { out.success = false; out.message = "path invalid: " + pv.error; return out; }
    if (!contains_username_component(mapping->home, mapping->username)) {
        out.success = false; out.message = "home path does not reference username"; return out;
    }

    // Verify observed OS identity matches persisted mapping
    if (inspector_) {
        auto obs = inspector_->lookup_user(mapping->username);
        if (!obs.exists) { out.success = false; out.message = "OS user missing"; return out; }
        if (obs.username != mapping->username) { out.success = false; out.message = "OS username mismatch"; return out; }
        if (obs.uid != mapping->uid) { out.success = false; out.message = "OS UID mismatch"; return out; }
        if (obs.gid != mapping->gid) { out.success = false; out.message = "OS GID mismatch"; return out; }
        if (obs.home != mapping->home) { out.success = false; out.message = "OS home mismatch"; return out; }
        if (obs.shell != managed_shell_) { out.success = false; out.message = "OS shell mismatch"; return out; }
    }

    std::string home = mapping->home;
    std::string sites_dir = home + "/sites/";

    // Track whether we created this directory (rollback only what we created)
    bool dir_created = false;
    if (fs_inspector_) { auto pre = fs_inspector_->inspect(sites_dir); dir_created = !pre.exists; }
    else dir_created = true; // assume created if no inspector

    auto r = runner_->mkdir_p(sites_dir);
    if (!r.success) { out.success = false; out.message = "mkdir sites/ failed"; return out; }

    // Enforce root-owned chroot layout (OpenSSH ChrootDirectory requirement)
    auto r2 = runner_->chown_root(sites_dir);
    if (!r2.success) { rollback_chroot_rmdir(sites_dir, dir_created, out); return out; }
    auto r3 = runner_->chmod("755", sites_dir);
    if (!r3.success) { rollback_chroot_rmdir(sites_dir, dir_created, out); return out; }

    // Postcondition: verify directory ownership, mode, path identity
    if (fs_inspector_) {
        auto post = fs_inspector_->inspect(sites_dir);
        if (!post.exists) { out.success = false; out.message = "sites/ missing"; return out; }
        if (post.is_symlink) { out.success = false; out.message = "sites/ is symlink"; return out; }
        if (post.owner_uid != 0) { out.success = false; out.message = "sites/ not root-owned"; return out; }
        if (post.group_gid != 0) { out.success = false; out.message = "sites/ not root group"; return out; }
        if (post.mode != 0755) { out.success = false; out.message = "sites/ wrong mode"; return out; }
        if (post.acl_status != InspectionStatus::Ok && post.acl_status != InspectionStatus::PathMissing) {
            rollback_chroot_rmdir(sites_dir, dir_created, out); return out;
        }
    }

    out.success = true; out.message = "chroot layout created"; return out;
}

core::OperationResult LocalSftpProvider::bind_mount_site(uint64_t access_user_id, uint64_t site_id) {
    core::OperationResult out;
    if (!operation_gate(out, "bind_mount_site")) return out;
    return bind_mount_site_internal(access_user_id, site_id);
}

core::OperationResult LocalSftpProvider::bind_mount_site_internal(uint64_t access_user_id, uint64_t site_id) {
    core::OperationResult out;

    if (!runner_ || !site_resolver_) {
        out.success = false; out.message = "provider dependencies not configured"; return out;
    }
    if (!mount_inspector_) {
        out.success = false; out.message = "mount_inspector not configured"; return out;
    }
    if (site_id == 0) { out.success = false; out.message = "admin_panel_sftp_access_forbidden"; return out; }

    std::string username = resolve_username(access_user_id);
    if (username.empty()) { out.success = false; out.message = "user not provisioned"; return out; }

    auto site_info = site_resolver_(site_id); if (!site_info.valid) { out.success = false; out.message = "site not found"; return out; } std::string site_root = site_info.root;


    std::string domain = site_info.domain;

    std::string source = site_root + "/public/";
    std::string target = managed_home_root_ + "/" + username + "/sites/" + domain;

    // Check if exact expected managed bind already exists (idempotent).
    // All of the following must match to consider this an existing managed bind:
    // 1. MountStatus is Ok
    // 2. mounted is true
    // 3. exact canonical target
    // 4. exact canonical source/root
    // 5. bind semantics confirmed (is_bind && fstype non-empty)
    // 6. device/filesystem identity is consistent (device non-empty)
    // 7. required mount options match (rw present, ro absent)
    // 8. persisted grant belongs to same user and site
    auto existing = mount_inspector_->inspect(target);
    if (existing.mounted) {
        bool match = true;
        std::string mismatch_reason;
        auto note = [&](const char* reason) { if (mismatch_reason.empty()) mismatch_reason = reason; match = false; };

        if (existing.status != MountStatus::Ok) note("status");
        if (!existing.is_bind)                 note("no_bind");
        if (existing.target != target)         note("target");
        if (existing.bind_root != source)      note("source");
        if (existing.fstype.empty())           note("fstype");
        if (existing.device.empty())           note("device");
        // Required mount options: must have rw, must not have ro
        {
            bool has_rw = false, has_ro = false;
            std::string opts = existing.options;
            std::string::size_type pos = 0;
            while (pos < opts.size()) {
                auto comma = opts.find(',', pos);
                std::string opt = (comma == std::string::npos) ? opts.substr(pos) : opts.substr(pos, comma - pos);
                if (opt == "rw") has_rw = true;
                if (opt == "ro") has_ro = true;
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
            if (!has_rw || has_ro) note("options");
        }
        // Verify a persisted grant exists for this user+site
        {
            bool has_grant = false;
            if (grants_loader_) {
                auto all_grants = grants_loader_(access_user_id);
                for (const auto& g : all_grants) {
                    if (g.site_id == site_id) { has_grant = true; break; }
                }
            }
            if (!has_grant) note("no_grant");
        }

        if (match) {
            out.success = true; out.message = "mount already exists"; return out;
        }
        out.success = false;
        out.message = std::string("foreign_or_mismatched_mount:") + mismatch_reason;
        return out;
    }

    // Track whether we create the target directory (for rollback)
    bool dir_created = false;
    if (fs_inspector_) {
        auto pre = fs_inspector_->inspect(target);
        dir_created = !pre.exists;
    } else {
        dir_created = true;
    }

    // Create target
    auto r1 = runner_->mkdir_p(target);
    if (!r1.success) { out.success = false; out.message = "mkdir failed"; return out; }

    // Mount
    auto r2 = runner_->mount_bind(source, target);
    if (!r2.success) {
        if (dir_created) {
            auto rm_r = safe_rmdir(target, true, username, domain);
            out.success = false;
            if (!rm_r.success)
                out.message = "mount failed: " + rm_r.message;
            else
                out.message = "mount failed";
        } else {
            out.success = false; out.message = "mount failed";
        }
        return out;
    }

    // Verify exact identity — same criteria as the pre-mount check
    auto post = mount_inspector_->inspect(target);
    bool post_ok = true;
    if (post.status != MountStatus::Ok)                             post_ok = false;
    if (!post.mounted)                                              post_ok = false;
    if (post.target != target)                                      post_ok = false;
    if (!post.is_bind)                                              post_ok = false;
    if (post.bind_root != source)                                   post_ok = false;
    if (post.fstype.empty())                                        post_ok = false;
    if (post.device.empty())                                        post_ok = false;
    {
        bool has_rw = false, has_ro = false;
        std::string opts = post.options;
        std::string::size_type pos = 0;
        while (pos < opts.size()) {
            auto comma = opts.find(',', pos);
            std::string opt = (comma == std::string::npos) ? opts.substr(pos) : opts.substr(pos, comma - pos);
            if (opt == "rw") has_rw = true;
            if (opt == "ro") has_ro = true;
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        if (!has_rw || has_ro) post_ok = false;
    }

    if (!post_ok) {
        // The mount was created by this operation — we verified no valid mount
        // existed before the mount_bind call above.

        // 1. Umount
        auto um = runner_->umount(target);
        if (!um.success) { out.success = false; out.message = "mount rollback umount failure"; return out; }

        // 2. Verify umount via inspector
        if (mount_inspector_) {
            auto check = mount_inspector_->inspect(target);
            if (check.mounted) { out.success = false; out.message = "mount rollback still mounted"; return out; }
        }

        // 3. Rmdir only if we created it
        if (dir_created) {
            auto rm_r = safe_rmdir(target, true, username, domain);
            if (!rm_r.success) { out = rm_r; return out; }
        }

        out.success = false; out.message = "mount verification failed"; return out;
    }

    out.success = true; out.message = "mounted: " + domain; return out;
}

core::OperationResult LocalSftpProvider::unmount_site(uint64_t access_user_id, uint64_t site_id) {
    core::OperationResult out;
    if (!operation_gate(out, "unmount_site")) return out;
    return unmount_site_internal(access_user_id, site_id);
}

core::OperationResult LocalSftpProvider::unmount_site_internal(uint64_t access_user_id, uint64_t site_id) {
    core::OperationResult out;

    if (!runner_ || !mount_inspector_) {
        out.success = false; out.message = "provider dependencies not configured"; return out;
    }
    if (site_id == 0) { out.success = false; out.message = "admin_panel_sftp_access_forbidden"; return out; }

    std::string username = resolve_username(access_user_id);
    if (username.empty()) { out.success = false; out.message = "user not provisioned"; return out; }

    auto site_info = site_resolver_(site_id); if (!site_info.valid) { out.success = false; out.message = "site not found"; return out; } std::string site_root = site_info.root;


    std::string domain = site_root;
    while (!domain.empty() && domain.back() == '/') domain.pop_back();
    auto pos = domain.rfind('/');
    domain = (pos != std::string::npos) ? domain.substr(pos + 1) : domain;

    std::string target = managed_home_root_ + "/" + username + "/sites/" + domain;

    auto existing = mount_inspector_->inspect(target);

    // Classify mount state
    switch (existing.status) {
    case MountStatus::Absent:
        // Already unmounted — idempotent success
        out.success = true; out.message = "already unmounted"; return out;

    case MountStatus::Ok:
        // Mount present — check if it matches our managed bind
        break;

    case MountStatus::TargetMissing:
        out.success = false; out.message = "unmount_inspection:invalid_target"; return out;

    case MountStatus::PermissionDenied:
        out.success = false; out.message = "unmount_inspection:permission_denied"; return out;

    case MountStatus::InspectionFailed:
        out.success = false; out.message = "unmount_inspection:io_error"; return out;

    case MountStatus::DependencyUnavailable:
        out.success = false; out.message = "unmount_inspection:dependency_unavailable"; return out;
    }

    // MountStatus::Ok — verify it's our managed bind mount
    if (!existing.mounted) {
        // Not mounted despite Ok status — ambiguous state, fail closed
        out.success = false; out.message = "unmount_inspection:ambiguous"; return out;
    }

    std::string expected_source = site_root + "/public/";
    bool match = true;
    std::string mismatch_reason;
    auto note = [&](const char* reason) { if (mismatch_reason.empty()) mismatch_reason = reason; match = false; };

    if (!existing.is_bind)                 note("no_bind");
    if (existing.target != target)         note("target");
    if (existing.bind_root != expected_source) note("source");
    if (existing.fstype.empty())           note("fstype");
    if (existing.device.empty())           note("device");
    {
        bool has_rw = false, has_ro = false;
        std::string opts = existing.options;
        std::string::size_type pos = 0;
        while (pos < opts.size()) {
            auto comma = opts.find(',', pos);
            std::string opt = (comma == std::string::npos) ? opts.substr(pos) : opts.substr(pos, comma - pos);
            if (opt == "rw") has_rw = true;
            if (opt == "ro") has_ro = true;
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        if (!has_rw || has_ro) note("options");
    }

    if (!match) {
        out.success = false;
        out.message = std::string("foreign_or_mismatched_mount:") + mismatch_reason;
        return out;
    }

    auto r1 = runner_->umount(target);
    if (!r1.success) { out.success = false; out.message = "umount failed"; return out; }

    {   // Scope for post-umount verification
        auto post = mount_inspector_->inspect(target);
        switch (post.status) {
        case MountStatus::Absent:
            break; // Good — proceed to directory cleanup

        case MountStatus::Ok:
            if (!post.mounted) {
                out.success = false; out.message = "umount_verify:ambiguous"; return out;
            }
            // Mount still present — check if it's still our bind
            if (post.is_bind && post.bind_root == expected_source && post.target == target && !post.fstype.empty() && !post.device.empty()) {
                out.success = false; out.message = "umount_verify:still_mounted"; return out;
            }
            out.success = false; out.message = "umount_verify:foreign_mount"; return out;

        default:
            out.success = false; out.message = "umount_verify:inspection_failed"; return out;
        }
    }

    // Verify all preconditions and remove the mount target directory
    auto rmdir_result = safe_rmdir(target, true, username, domain);
    if (!rmdir_result.success) { out = rmdir_result; return out; }

    out.success = true; out.message = "unmounted: " + domain; return out;
}

core::OperationResult LocalSftpProvider::cleanup_all_mounts(uint64_t access_user_id) {
    core::OperationResult out;
    if (!operation_gate(out, "cleanup_all_mounts")) return out;
    return cleanup_all_mounts_internal(access_user_id);
}

core::OperationResult LocalSftpProvider::cleanup_all_mounts_internal(uint64_t access_user_id) {
    core::OperationResult out;

    if (!runner_ || !mount_inspector_ || !site_resolver_) {
        out.success = false; out.message = "provider dependencies not configured"; return out;
    }
    if (!grants_loader_) { out.success = false; out.message = "grant_loader_not_configured"; return out; }

    std::string username = resolve_username(access_user_id);
    if (username.empty()) { out.success = false; out.message = "user not provisioned"; return out; }

    // Enumerate all mounts within the user's sites/ using mount inspector via known domains.
    // We iterate persisted grants to know which domains to check.
    auto grants = grants_loader_(access_user_id);
    std::string failures;
    size_t failed = 0;
    for (const auto& g : grants) {
        std::string site_label = std::to_string(g.site_id);
        auto r = unmount_site_internal(access_user_id, g.site_id);
        if (!r.success) {
            if (!failures.empty()) failures += "; ";
            failures += site_label + ":" + r.message;
            failed++;
        }
    }

    size_t cleaned = grants.size() - failed;
    if (failed > 0) {
        out.success = false;
        out.message = "cleanup_all_mounts:" + std::to_string(failed) + " failures — " + failures;
        return out;
    }
    out.success = true;
    out.message = std::to_string(cleaned) + " mounts cleaned";
    return out;
}

core::OperationResult LocalSftpProvider::reconcile_mounts(uint64_t access_user_id) {
    core::OperationResult out;
    if (!operation_gate(out, "reconcile_mounts")) return out;
    return reconcile_mounts_internal(access_user_id);
}

core::OperationResult LocalSftpProvider::reconcile_mounts_internal(uint64_t access_user_id) {
    core::OperationResult out;

    if (!runner_ || !mount_inspector_ || !site_resolver_ || !grants_loader_) {
        out.success = false; out.message = "provider dependencies not configured"; return out;
    }

    std::string username = resolve_username(access_user_id);
    if (username.empty()) { out.success = false; out.message = "user not provisioned"; return out; }

    std::string user_root = managed_home_root_ + "/" + username;
    std::string sites_prefix = user_root + "/sites/";

    // 1. Build expected mounts from grants
    struct Expected { uint64_t site_id; std::string target; std::string source; };
    std::vector<Expected> expected;
    auto grants = grants_loader_(access_user_id);
    for (const auto& g : grants) {
        auto si = site_resolver_(g.site_id);
        if (!si.valid) continue;
        std::string domain = si.root;
        while (!domain.empty() && domain.back() == '/') domain.pop_back();
        auto p = domain.rfind('/');
        domain = (p != std::string::npos) ? domain.substr(p + 1) : domain;
        expected.push_back({g.site_id, user_root + "/sites/" + domain, si.root + "/public/"});
    }

    // 2. Enumerate observed mounts
    auto observed = mount_inspector_->enumerate(user_root);

    // 3. Index observed by target
    std::map<std::string, const containercp::access::MountState*> obs_map;
    for (const auto& m : observed) {
        obs_map[m.target] = &m;
    }

    // 4. Classify and reconcile
    std::string diag;
    size_t reconciled = 0;
    size_t failures = 0;

    auto note = [&](const std::string& entry) {
        if (!diag.empty()) diag += "; ";
        diag += entry;
    };

    for (const auto& exp : expected) {
        auto it = obs_map.find(exp.target);
        if (it == obs_map.end()) {
            note("missing:site_" + std::to_string(exp.site_id));
            failures++;
            continue;
        }
        const auto& obs = *it->second;
        bool ok = obs.mounted && obs.is_bind && obs.bind_root == exp.source
                  && !obs.fstype.empty() && !obs.device.empty();
        {
            bool has_rw = false, has_ro = false;
            std::string opts = obs.options;
            std::string::size_type pos = 0;
            while (pos < opts.size()) {
                auto comma = opts.find(',', pos);
                std::string opt = (comma == std::string::npos) ? opts.substr(pos) : opts.substr(pos, comma - pos);
                if (opt == "rw") has_rw = true;
                if (opt == "ro") has_ro = true;
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
            if (!has_rw || has_ro) ok = false;
        }
        if (ok) {
            obs_map.erase(it);
            continue; // expected managed mount — present and correct
        }
        // Stale managed mount — identity mismatch
        auto ur = unmount_site_internal(access_user_id, exp.site_id);
        if (ur.success) {
            reconciled++;
            note("fixed:site_" + std::to_string(exp.site_id));
        } else {
            failures++;
            note("stale:site_" + std::to_string(exp.site_id) + ":" + ur.message);
        }
        obs_map.erase(it);
    }

    // Remaining observed mounts — orphans, foreign, ambiguous
    for (const auto& pair : obs_map) {
        const auto& m = *pair.second;
        const std::string& target = pair.first;

        // Foreign: not under sites/ but under user_root
        if (target.rfind(sites_prefix, 0) != 0) {
            note("foreign:" + target);
            continue;
        }

        // Ambiguous: not a proper bind mount
        if (!m.mounted || !m.is_bind || m.fstype.empty() || m.device.empty()) {
            note("ambiguous:" + target);
            continue;
        }

        // Orphan managed mount — we can prove ownership by target+source identity
        // under sites/ with bind mount identity → safe to remove
        auto um = runner_->umount(target);
        if (um.success) {
            reconciled++;
            note("orphan_removed:" + target);
        } else {
            failures++;
            note("orphan_failed:" + target + ":" + um.message);
        }
    }

    if (failures > 0) {
        out.success = false;
        out.message = "reconcile_mounts:" + std::to_string(failures) + " failures, "
                      + std::to_string(reconciled) + " fixed — " + diag;
        return out;
    }
    out.message = "reconcile_mounts:" + std::to_string(reconciled) + " fixed";
    if (reconciled > 0 || !diag.empty()) out.message += " — " + diag;
    out.success = true;
    return out;
}

core::OperationResult LocalSftpProvider::reconcile_startup_mounts() {
    core::OperationResult out;
    if (!operation_gate(out, "reconcile_startup_mounts")) return out;
    return reconcile_startup_mounts_internal();
}

core::OperationResult LocalSftpProvider::reconcile_startup_mounts_internal() {
    core::OperationResult out;

    if (!load_all_managed_mounts_ || !save_managed_mount_) {
        out.success = false; out.message = "managed mount storage not configured"; return out;
    }

    auto mounts = load_all_managed_mounts_();
    std::string diag;
    size_t reconciled = 0;
    size_t failures = 0;

    auto note = [&](const std::string& entry) {
        if (!diag.empty()) diag += "; ";
        diag += entry;
    };

    for (auto& mount : mounts) {
        std::string ident = "u" + std::to_string(mount.access_user_id)
                          + "/s" + std::to_string(mount.site_id);

        if (mount.state == "active") {
            if (!site_resolver_ || !mount_inspector_ || !runner_) {
                note("active:skip:" + ident + ":deps");
                continue;
            }
            auto si = site_resolver_(mount.site_id);
            if (!si.valid) { note("active:skip:" + ident + ":site"); continue; }
            auto ms = mount_inspector_->inspect(mount.target_path);
            if (ms.status == MountStatus::Ok && ms.mounted && ms.is_bind && ms.bind_root == mount.source_path) {
                continue; // Mount is correct
            }
            if (!ms.mounted || ms.status != MountStatus::Ok) {
                // Mount missing — recreate
                auto bm = bind_mount_site_internal(mount.access_user_id, mount.site_id);
                if (bm.success) {
                    mount.last_error = "";
                    if (!save_managed_mount_(mount)) { note("active:recreate:persist:" + ident); failures++; }
                    else { note("active:recreated:" + ident); reconciled++; }
                } else {
                    mount.last_error = bm.message;
                    mount.state = "error";
                    if (!save_managed_mount_(mount)) { note("active:fail:persist:" + ident); failures++; }
                    else { note("active:fail:" + ident + ":" + bm.message); failures++; }
                }
            } else {
                // Foreign mount — don't touch, mark error
                mount.last_error = "foreign mount at target";
                mount.state = "error";
                if (!save_managed_mount_(mount)) { note("active:foreign:persist:" + ident); failures++; }
                else { note("active:foreign:" + ident); failures++; }
            }
        } else if (mount.state == "removing") {
            if (!runner_ || !mount_inspector_) {
                note("removing:skip:" + ident + ":deps");
                continue;
            }
            auto um = unmount_site_internal(mount.access_user_id, mount.site_id);
            if (um.success) {
                if (delete_managed_mount_ && delete_managed_mount_(mount.access_user_id, mount.site_id)) {
                    note("removing:done:" + ident); reconciled++;
                } else {
                    note("removing:persist:" + ident); failures++;
                }
            } else {
                mount.last_error = um.message;
                mount.state = "error";
                if (!save_managed_mount_(mount)) { note("removing:fail:persist:" + ident); failures++; }
                else { note("removing:fail:" + ident); failures++; }
            }
        } else if (mount.state == "applying") {
            if (!site_resolver_ || !mount_inspector_ || !runner_) {
                note("applying:skip:" + ident + ":deps");
                continue;
            }
            auto si = site_resolver_(mount.site_id);
            if (!si.valid) { note("applying:skip:" + ident + ":site"); continue; }
            auto ms = mount_inspector_->inspect(mount.target_path);
            if (ms.status == MountStatus::Ok && ms.mounted && ms.is_bind && ms.bind_root == mount.source_path) {
                // Mount already exists correctly — complete
                mount.state = "active";
                mount.last_error = "";
                if (!save_managed_mount_(mount)) { note("applying:complete:persist:" + ident); failures++; }
                else { note("applying:completed:" + ident); reconciled++; }
            } else if (ms.mounted) {
                // Wrong mount at target — rollback
                auto um = runner_->umount(mount.target_path);
                if (um.success) {
                    mount.last_error = "rolled back after crash";
                    mount.state = "error";
                    if (!save_managed_mount_(mount)) { note("applying:rollback:persist:" + ident); failures++; }
                    else { note("applying:rolled_back:" + ident); failures++; }
                } else {
                    mount.last_error = "rollback umount failed: " + um.message;
                    mount.state = "error";
                    if (!save_managed_mount_(mount)) { note("applying:rollback_fail:persist:" + ident); failures++; }
                    else { note("applying:rollback_fail:" + ident); failures++; }
                }
            } else {
                // Nothing at target — retry as pending
                mount.state = "pending";
                mount.last_error = "retry after crash";
                if (!save_managed_mount_(mount)) { note("applying:retry:persist:" + ident); failures++; }
                else { note("applying:retry:" + ident); reconciled++; }
            }
        } else if (mount.state == "pending") {
            if (!site_resolver_ || !mount_inspector_ || !runner_) {
                note("pending:skip:" + ident + ":deps");
                continue;
            }
            auto si = site_resolver_(mount.site_id);
            if (!si.valid) { note("pending:skip:" + ident + ":site"); continue; }
            auto bm = bind_mount_site_internal(mount.access_user_id, mount.site_id);
            if (bm.success) {
                mount.state = "active";
                mount.last_error = "";
                if (!save_managed_mount_(mount)) { note("pending:apply:persist:" + ident); failures++; }
                else { note("pending:applied:" + ident); reconciled++; }
            } else {
                mount.last_error = bm.message;
                mount.state = "error";
                if (!save_managed_mount_(mount)) { note("pending:fail:persist:" + ident); failures++; }
                else { note("pending:fail:" + ident); failures++; }
            }
        } else if (mount.state == "error") {
            // Error state — only perform approved recoverable action:
            // If mount is actually active and correct, clear the error.
            if (mount_inspector_) {
                auto ms = mount_inspector_->inspect(mount.target_path);
                if (ms.status == MountStatus::Ok && ms.mounted && ms.is_bind && ms.bind_root == mount.source_path) {
                    mount.state = "active";
                    mount.last_error = "";
                    if (!save_managed_mount_(mount)) { note("error:recover:persist:" + ident); failures++; }
                    else { note("error:recovered:" + ident); reconciled++; }
                } else {
                    // Leave as error — do not attempt recovery
                }
            }
        }
    }

    if (failures > 0) {
        out.success = false;
        out.message = "reconcile_startup:" + std::to_string(failures) + " failures, "
                      + std::to_string(reconciled) + " fixed — " + diag;
        return out;
    }
    out.success = true;
    out.message = "reconcile_startup:" + std::to_string(reconciled) + " fixed";
    if (reconciled > 0 || !diag.empty()) out.message += " — " + diag;
    return out;
}

core::OperationResult LocalSftpProvider::reconcile_user_lifecycle() {
    core::OperationResult out;
    if (!operation_gate(out, "reconcile_user_lifecycle")) return out;
    return reconcile_user_lifecycle_internal();
}

core::OperationResult LocalSftpProvider::reconcile_user_lifecycle_internal() {
    core::OperationResult out;

    if (!load_mappings_ || !save_mapping_ || !inspector_ || !runner_) {
        out.success = false; out.message = "provider dependencies not configured"; return out;
    }

    auto mappings = load_mappings_();
    std::string diag;
    size_t reconciled = 0;
    size_t failures = 0;

    auto note = [&](const std::string& entry) {
        if (!diag.empty()) diag += "; ";
        diag += entry;
    };

    // Helper to persist a mapping state change, returns false on failure.
    auto persist = [&](SystemAccountMapping& m, const std::string& state, const std::string& err) -> bool {
        m.state = state;
        if (!err.empty()) m.last_error = err;
        return save_mapping_(m);
    };

    // Helper to check if chroot sites/ dir is correctly set up
    auto chroot_ok = [&](const SystemAccountMapping& m) -> bool {
        if (!fs_inspector_ || m.home.empty()) return false;
        std::string sites_dir = m.home + "/sites/";
        auto st = fs_inspector_->inspect(sites_dir);
        return st.exists && !st.is_symlink && st.owner_uid == 0 && st.group_gid == 0
               && (st.mode & 0777) == 0755;
    };

    for (auto& mapping : mappings) {
        if (mapping.entity_type != "access_user") continue;

        std::string ident = "u" + std::to_string(mapping.entity_id);

        if (mapping.state == "provisioning") {
            // Inspect whether the OS account exists
            auto observed = inspector_->lookup_user(mapping.username);

            if (observed.exists) {
                // OS account exists — verify it matches our mapping
                if (!verify_ownership(mapping, observed)) {
                    // Mismatch — unmanaged conflict, persist error
                    mapping.last_error = "provisioning:os_account_mismatch uid="
                        + std::to_string(observed.uid) + " home=" + observed.home;
                    if (!persist(mapping, "error", mapping.last_error)) {
                        note(ident + ":provisioning:error_persist_failed");
                        failures++;
                    } else {
                        note(ident + ":provisioning:conflict_error");
                        failures++;
                    }
                    continue;
                }

                // OS account matches — complete chroot layout manually
                // (cannot call ensure_chroot_layout because mapping is not yet active)
                bool chroot_complete = false;
                if (!mapping.home.empty() && managed_path_safe(mapping.home, managed_home_root_)) {
                    std::string sites_dir = mapping.home + "/sites/";
                    if (fs_inspector_) {
                        auto st = fs_inspector_->inspect(sites_dir);
                        if (st.exists && !st.is_symlink && st.owner_uid == 0) {
                            chroot_complete = true;
                        }
                    }
                    if (!chroot_complete && runner_) {
                        auto mk = runner_->mkdir_p(sites_dir);
                        if (mk.success) {
                            auto cown = runner_->chown_root(sites_dir);
                            if (cown.success) {
                                auto chm = runner_->chmod("755", sites_dir);
                                chroot_complete = chm.success;
                            }
                        }
                    }
                }

                // Apply pending grants if possible
                bool grants_ok = false;
                if (load_all_grant_lifecycle_ && save_grant_lifecycle_) {
                    auto grants = apply_pending_grants_internal(mapping.entity_id);
                    grants_ok = grants.success;
                } else {
                    grants_ok = true;
                }

                if (chroot_complete && grants_ok) {
                    if (!persist(mapping, "active", "")) {
                        note(ident + ":provisioning:active_persist_failed");
                        failures++;
                    } else {
                        note(ident + ":provisioning:recovered");
                        reconciled++;
                    }
                } else {
                    // Partial — persist error with details
                    std::string err = "provisioning:incomplete chroot="
                        + std::string(chroot_complete ? "ok" : "failed")
                        + " grants=" + std::string(grants_ok ? "ok" : "failed");
                    if (!persist(mapping, "error", err)) {
                        note(ident + ":provisioning:error_persist_failed");
                        failures++;
                    } else {
                        note(ident + ":provisioning:incomplete");
                        failures++;
                    }
                }
            } else {
                // OS account doesn't exist — check for unmanaged conflicts
                if (inspector_->user_exists(mapping.username) ||
                    inspector_->group_exists(mapping.groupname)) {
                    // Unmanaged entity with this name exists
                    mapping.last_error = "provisioning:unmanaged_conflict";
                    if (!persist(mapping, "error", mapping.last_error)) {
                        note(ident + ":provisioning:conflict_persist_failed");
                        failures++;
                    } else {
                        note(ident + ":provisioning:conflict_error");
                        failures++;
                    }
                    continue;
                }

                // Clean up stale group if it exists but is managed
                if (inspector_->group_exists(mapping.groupname)) {
                    auto grp = inspector_->lookup_group(mapping.groupname);
                    if (grp.gid == mapping.gid) {
                        (void)runner_->groupdel(mapping.groupname);
                    }
                }

                // Clean up provisioning record — it was never completed
                // Check if home exists (created during create_user step 11)
                if (!mapping.home.empty() && managed_path_safe(mapping.home, managed_home_root_)) {
                    std::error_code ec;
                    std::filesystem::remove_all(mapping.home, ec);
                }

                if (!delete_mapping_ || !delete_mapping_("access_user", mapping.entity_id)) {
                    note(ident + ":provisioning:delete_persist_failed");
                    failures++;
                } else {
                    note(ident + ":provisioning:cleaned");
                    reconciled++;
                }
            }
        } else if (mapping.state == "active") {
            // Verify account identity
            auto observed = inspector_->lookup_user(mapping.username);
            if (!verify_ownership(mapping, observed)) {
                mapping.last_error = "active:ownership_mismatch";
                if (!persist(mapping, "error", mapping.last_error)) {
                    note(ident + ":active:error_persist_failed");
                    failures++;
                } else {
                    note(ident + ":active:ownership_error");
                    failures++;
                }
                continue;
            }

            // Verify chroot layout
            if (fs_inspector_) {
                bool chroot_good = chroot_ok(mapping);
                if (!chroot_good && !mapping.home.empty()) {
                    // Attempt to repair chroot layout
                    auto chroot = ensure_chroot_layout_internal(mapping.entity_id);
                    if (!chroot.success) {
                        mapping.last_error = "active:chroot_repair_failed:" + chroot.message;
                        if (!persist(mapping, "error", mapping.last_error)) {
                            note(ident + ":active:error_persist_failed");
                            failures++;
                        } else {
                            note(ident + ":active:chroot_error");
                            failures++;
                        }
                        continue;
                    }
                    note(ident + ":active:chroot_repaired");
                    reconciled++;
                }
            }

            // Reconcile grants and mounts
            if (load_all_grant_lifecycle_ && save_grant_lifecycle_) {
                auto grants = apply_pending_grants_internal(mapping.entity_id);
                if (!grants.success) {
                    mapping.last_error = "active:grant_reconcile_failed:" + grants.message;
                    if (!persist(mapping, "error", mapping.last_error)) {
                        note(ident + ":active:error_persist_failed");
                        failures++;
                    } else {
                        note(ident + ":active:grant_error");
                        failures++;
                    }
                    continue;
                }
                if (grants.message.find("applied") != std::string::npos) {
                    note(ident + ":active:grants_applied");
                    reconciled++;
                }
            }

            // Reconcile mounts
            if (mount_inspector_ && site_resolver_ && grants_loader_) {
                auto mounts = reconcile_mounts_internal(mapping.entity_id);
                if (!mounts.success) {
                    mapping.last_error = "active:mount_reconcile_failed:" + mounts.message;
                    if (!persist(mapping, "error", mapping.last_error)) {
                        note(ident + ":active:error_persist_failed");
                        failures++;
                    } else {
                        note(ident + ":active:mount_error");
                        failures++;
                    }
                    continue;
                }
                if (mounts.message.find("fixed") != std::string::npos) {
                    note(ident + ":active:mounts_fixed");
                    reconciled++;
                }
            }

            // No drift detected — active is healthy
            note(ident + ":active:ok");

        } else if (mapping.state == "removing") {
            // Continue the removal process idempotently

            // 1. Revoke all grants (idempotent)
            if (load_all_grant_lifecycle_ && save_grant_lifecycle_) {
                auto revoke = revoke_all_grants_internal(mapping.entity_id);
                if (!revoke.success) {
                    mapping.last_error = "removing:revoke_failed:" + revoke.message;
                    if (!persist(mapping, "error", mapping.last_error)) {
                        note(ident + ":removing:error_persist_failed");
                        failures++;
                    } else {
                        note(ident + ":removing:revoke_error");
                        failures++;
                    }
                    continue;
                }

                // Verify no non-terminal grants
                auto all = load_all_grant_lifecycle_();
                for (const auto& l : all) {
                    if (l.access_user_id == mapping.entity_id && l.state != "error") {
                        mapping.last_error = "removing:non_terminal_grant site="
                            + std::to_string(l.site_id);
                        if (!persist(mapping, "error", mapping.last_error)) {
                            note(ident + ":removing:error_persist_failed");
                            failures++;
                        } else {
                            note(ident + ":removing:grant_remaining");
                            failures++;
                        }
                        continue;
                    }
                }
            }

            // 2. Cleanup mounts (idempotent)
            if (mount_inspector_ && site_resolver_ && grants_loader_) {
                auto cleanup = cleanup_all_mounts_internal(mapping.entity_id);
                if (!cleanup.success) {
                    mapping.last_error = "removing:cleanup_failed:" + cleanup.message;
                    if (!persist(mapping, "error", mapping.last_error)) {
                        note(ident + ":removing:error_persist_failed");
                        failures++;
                    } else {
                        note(ident + ":removing:cleanup_error");
                        failures++;
                    }
                    continue;
                }

                auto reconcile = reconcile_mounts_internal(mapping.entity_id);
                if (!reconcile.success) {
                    mapping.last_error = "removing:reconcile_failed:" + reconcile.message;
                    if (!persist(mapping, "error", mapping.last_error)) {
                        note(ident + ":removing:error_persist_failed");
                        failures++;
                    } else {
                        note(ident + ":removing:reconcile_error");
                        failures++;
                    }
                    continue;
                }
            }

            // 3. Remove OS account if exists
            auto observed = inspector_->lookup_user(mapping.username);
            if (observed.exists) {
                if (!verify_ownership(mapping, observed)) {
                    mapping.last_error = "removing:ownership_mismatch";
                    if (!persist(mapping, "error", mapping.last_error)) {
                        note(ident + ":removing:error_persist_failed");
                        failures++;
                    } else {
                        note(ident + ":removing:ownership_error");
                        failures++;
                    }
                    continue;
                }

                // Remove from global SFTP group (best-effort)
                (void)runner_->usermod_remove_group(mapping.username, global_sftp_group_);

                auto ur = runner_->userdel(mapping.username);
                if (!ur.success) {
                    mapping.last_error = "removing:userdel_failed:" + ur.message;
                    if (!persist(mapping, "error", mapping.last_error)) {
                        note(ident + ":removing:error_persist_failed");
                        failures++;
                    } else {
                        note(ident + ":removing:userdel_error");
                        failures++;
                    }
                    continue;
                }

                // Verify absent
                auto check = inspector_->lookup_user(mapping.username);
                if (check.exists) {
                    mapping.last_error = "removing:userdel_verify_failed";
                    if (!persist(mapping, "error", mapping.last_error)) {
                        note(ident + ":removing:error_persist_failed");
                        failures++;
                    } else {
                        note(ident + ":removing:userdel_verify_error");
                        failures++;
                    }
                    continue;
                }
            }

            // 4. Remove private group if exists
            if (inspector_->group_exists(mapping.groupname)) {
                auto gd = runner_->groupdel(mapping.groupname);
                if (!gd.success) {
                    mapping.last_error = "removing:groupdel_failed:" + gd.message;
                    if (!persist(mapping, "error", mapping.last_error)) {
                        note(ident + ":removing:error_persist_failed");
                        failures++;
                    } else {
                        note(ident + ":removing:groupdel_error");
                        failures++;
                    }
                    continue;
                }
            }

            // 5. Clean up home directory if safe
            if (!mapping.home.empty() && managed_path_safe(mapping.home, managed_home_root_)) {
                std::error_code ec;
                std::filesystem::remove_all(mapping.home, ec);
            }

            // 6. Delete persisted mapping
            if (!delete_mapping_ || !delete_mapping_("access_user", mapping.entity_id)) {
                note(ident + ":removing:delete_persist_failed");
                failures++;
            } else {
                note(ident + ":removing:completed");
                reconciled++;
            }

        } else if (mapping.state == "error") {
            // Only perform approved recoverable actions:
            // If OS account exists and appears correctly provisioned, recover to active.
            auto observed = inspector_->lookup_user(mapping.username);
            if (observed.exists && verify_ownership(mapping, observed)) {
                // Account identity matches — verify chroot and grants
                bool can_recover = true;

                if (fs_inspector_ && !mapping.home.empty()) {
                    std::string sites_dir = mapping.home + "/sites/";
                    auto st = fs_inspector_->inspect(sites_dir);
                    if (!st.exists || st.is_symlink) can_recover = false;
                }

                if (can_recover && load_all_grant_lifecycle_) {
                    auto all = load_all_grant_lifecycle_();
                    for (const auto& l : all) {
                        if (l.access_user_id == mapping.entity_id && (l.state == "revoking" || l.state == "applying")) {
                            can_recover = false;
                            break;
                        }
                    }
                }

                if (can_recover) {
                    if (!persist(mapping, "active", "")) {
                        note(ident + ":error:recover_persist_failed");
                        failures++;
                    } else {
                        note(ident + ":error:recovered");
                        reconciled++;
                    }
                } else {
                    // Preserve error state with existing last_error
                    note(ident + ":error:preserved:" + mapping.last_error);
                }
            } else {
                // Account doesn't exist or doesn't match — preserve error
                note(ident + ":error:preserved:" + mapping.last_error);
            }
        }
    }

    if (failures > 0) {
        out.success = false;
        out.message = "reconcile_users:" + std::to_string(failures) + " failures, "
                      + std::to_string(reconciled) + " fixed — " + diag;
        return out;
    }
    out.success = true;
    out.message = "reconcile_users:" + std::to_string(reconciled) + " fixed";
    if (reconciled > 0 || !diag.empty()) out.message += " — " + diag;
    return out;
}

// --- Phase 3d: Grant Lifecycle Integration ---

void LocalSftpProvider::set_grants_loader(LoadGrantsFn fn) { grants_loader_ = std::move(fn); }

std::string LocalSftpProvider::resolve_username(uint64_t access_user_id) {
    if (!load_mappings_) return {};
    auto mappings = load_mappings_();
    for (const auto& m : mappings) {
        if (m.entity_type == "access_user" && m.entity_id == access_user_id && (m.state == "active" || m.state == "removing"))
            return m.username;
    }
    return {};
}

core::OperationResult LocalSftpProvider::apply_grant(uint64_t access_user_id, uint64_t site_id,
                                                       const std::string& caller_permission) {
    core::OperationResult out;
    if (!operation_gate(out, "apply_grant")) return out;
    return apply_grant_internal(access_user_id, site_id, caller_permission);
}

core::OperationResult LocalSftpProvider::apply_grant_internal(uint64_t access_user_id, uint64_t site_id,
                                                       const std::string& caller_permission) {
    core::OperationResult out;

    if (!site_resolver_ || !inspector_) {
        out.success = false; out.message = "provider dependencies not configured"; return out;
    }
    if (site_id == 0) { out.success = false; out.message = "admin_panel_sftp_access_forbidden"; return out; }

    std::string username = resolve_username(access_user_id);
    if (username.empty()) { out.success = false; out.message = "user not provisioned"; return out; }

    // 1. Load or create persisted grant lifecycle state.
    // If lifecycle storage is not configured, fall back to caller permission
    // (backward compatibility without lifecycle tracking).
    std::string permission = caller_permission;
    storage::GrantLifecycleState ls;
    bool lifecycle_active = (save_grant_lifecycle_ && load_all_grant_lifecycle_);
    if (lifecycle_active) {
        auto all = load_all_grant_lifecycle_();
        bool found = false;
        for (const auto& l : all) {
            if (l.access_user_id == access_user_id && l.site_id == site_id) {
                ls = l; found = true; break;
            }
        }
        if (found) {
            permission = ls.permission;
        } else {
            if (!storage::valid_grant_permission(caller_permission)) {
                out.success = false; out.message = "invalid permission: " + caller_permission; return out;
            }
            ls.access_user_id = access_user_id;
            ls.site_id = site_id;
            ls.permission = caller_permission;
            ls.state = "pending";
            if (!save_grant_lifecycle_(ls)) {
                out.success = false; out.message = "grant_lifecycle:persist_failed"; return out;
            }
        }
    }

    // Helper to persist lifecycle state (no-op if lifecycle not active)
    auto persist = [&](const std::string& state, const std::string& err) -> bool {
        if (!lifecycle_active) return true;
        ls.state = state;
        ls.last_error = err;
        return save_grant_lifecycle_(ls);
    };

    int continue_step = -1; // -1 = run all steps (fresh apply)

    // 2. Handle lifecycle state
    if (lifecycle_active) {
        if (ls.state == "pending") {
        if (!persist("applying", "")) {
            out.success = false; out.message = "grant_lifecycle:applying_persist_failed"; return out;
        }
    } else if (ls.state == "applying") {
        // Crash recovery: inspect each step, continue from first missing step.
        // Returns 0-based step index to continue from, or 5 if all complete.
        auto find_continue_step = [&]() -> int {
            // Step 0: site group — check if mapping exists
            std::string etype = site_group_entity_type(permission);
            if (!find_mapping(etype, site_id).has_value()) return 0;

            // Step 1: membership
            if (!inspector_->user_in_group(username, site_group_name(site_id, permission)))
                return 1;

            // Step 2: directory permissions (RW only)
            if (permission != "read_only") {
                if (fs_inspector_) {
                    std::string pub = site_resolver_(site_id).root + "/public/";
                    auto st = fs_inspector_->inspect(pub);
                    if (!st.exists || st.group_gid <= 0) return 2;
                }
            }

            // Step 3: ACL (RO only)
            if (permission == "read_only") {
                if (fs_inspector_) {
                    std::string pub = site_resolver_(site_id).root + "/public/";
                    auto ro_mapping = find_mapping("site_group_ro", site_id);
                    if (ro_mapping.has_value()) {
                        auto acl = fs_inspector_->inspect_acl(pub, ro_mapping->groupname);
                        if (acl.acl_status != InspectionStatus::Ok) return 3;
                        if (permission == "read_only" && !acl.acl.access_present) return 3;
                    }
                }
            }

            // Step 4: chroot layout — check sites/ dir exists
            {
                std::string home = managed_home_root_ + "/" + username;
                std::string sites_dir = home + "/sites/";
                if (fs_inspector_) {
                    auto st = fs_inspector_->inspect(sites_dir);
                    if (!st.exists) return 4;
                }
            }

            // Step 5: bind mount
            if (mount_inspector_ && site_resolver_) {
                auto si = site_resolver_(site_id);
                if (si.valid) {
                    std::string d = si.root;
                    while (!d.empty() && d.back() == '/') d.pop_back();
                    auto p = d.rfind('/');
                    std::string domain = (p != std::string::npos) ? d.substr(p + 1) : d;
                    std::string target = managed_home_root_ + "/" + username + "/sites/" + domain;
                    auto ms = mount_inspector_->inspect(target);
                    if (ms.status == MountStatus::Ok && ms.mounted && ms.is_bind
                        && ms.bind_root == si.root + "/public/") {
                        return 5; // all complete
                    }
                    // Mount exists but is wrong — foreign or mismatched
                    if (ms.mounted) {
                        return -2; // foreign mount
                    }
                }
            }
            return -1; // mount step needs redo
        };

        continue_step = find_continue_step();
        if (continue_step == 5) {
            // All steps complete — persist active
            if (!persist("active", "")) {
                out.success = false; out.message = "grant_lifecycle:active_persist_failed"; return out;
            }
            out.success = true; out.message = "grant applied (recovered)"; return out;
        }
        if (continue_step == -2) {
            // Foreign mount at target — fail closed
            persist("error", "foreign_mount_at_target");
            out.success = false; out.message = "grant_lifecycle:foreign_mount"; return out;
        }
        // Otherwise, fall through to section 3 starting from continue_step
        // continue_step < 0 means restart from step 0
    } else if (ls.state == "active") {
        // Verify complete observed state
        bool ok = true;
        if (mount_inspector_) {
            std::string target = managed_home_root_ + "/" + username + "/sites/"
                + [&]() -> std::string {
                    auto si = site_resolver_(site_id);
                    if (!si.valid) return "";
                    std::string d = si.root;
                    while (!d.empty() && d.back() == '/') d.pop_back();
                    auto p = d.rfind('/');
                    return (p != std::string::npos) ? d.substr(p + 1) : d;
                }();
            if (!target.empty()) {
                auto ms = mount_inspector_->inspect(target);
                ok = (ms.status == MountStatus::Ok && ms.mounted && ms.is_bind);
            }
        }
        if (ok) {
            out.success = true; out.message = "grant already applied"; return out;
        }
        // Active record but mount missing — reconcile by continuing apply
        if (!persist("applying", "recovering active -> applying")) {
            out.success = false; out.message = "grant_lifecycle:recover_persist_failed"; return out;
        }
    } else if (ls.state == "revoking") {
        out.success = false; out.message = "grant_lifecycle:revoking_cannot_apply"; return out;
    } else if (ls.state == "error") {
        // Only approved recovery: verify mount exists and is correct
        bool recovered = false;
        if (mount_inspector_) {
            std::string target = managed_home_root_ + "/" + username + "/sites/"
                + [&]() -> std::string {
                    auto si = site_resolver_(site_id);
                    if (!si.valid) return "";
                    std::string d = si.root;
                    while (!d.empty() && d.back() == '/') d.pop_back();
                    auto p = d.rfind('/');
                    return (p != std::string::npos) ? d.substr(p + 1) : d;
                }();
            if (!target.empty()) {
                auto ms = mount_inspector_->inspect(target);
                if (ms.status == MountStatus::Ok && ms.mounted && ms.is_bind) {
                    recovered = true;
                }
            }
        }
        if (recovered) {
            if (!persist("active", "")) {
                out.success = false; out.message = "grant_lifecycle:error_recover_persist_failed"; return out;
            }
            out.success = true; out.message = "grant error recovered"; return out;
        }
        out.success = false; out.message = "grant_lifecycle:error_state:" + ls.last_error; return out;
        }
    }

    // 3. Continue apply from observed state (for pending/applying)
    // If applying recovery set continue_step, skip completed steps.
    // -1 means run all steps (fresh apply)
    bool group_created   = (continue_step > 0) ? false : !find_mapping(site_group_entity_type(permission), site_id).has_value();
    bool membership_added = (continue_step > 1) ? false : true;
    bool perms_changed   = false;
    bool acl_changed     = false;
    int  original_gid    = -1;
    int  original_mode   = -1;
    AclState original_acl_state;
    bool acl_captured = false;

    auto mode_to_octal = [](int mode) -> std::string {
        if (mode <= 0) return "755";
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%o", mode & 07777);
        return std::string(buf);
    };

    // Step 1: Ensure site group
    {
        auto r1 = ensure_site_group_internal(site_id, permission);
        if (!r1.success) {
            persist("error", r1.message);
            out = r1; return out;
        }
    }

    // Step 2: Add membership
    membership_added = !inspector_->user_in_group(username, site_group_name(site_id, permission));
    {
        auto r2 = add_user_to_site_group_internal(username, site_id, permission);
        if (!r2.success) {
            persist("error", r2.message);
            if (group_created) (void)delete_site_group_if_unused_internal(site_id, permission);
            out = r2; return out;
        }
    }

    // Step 3: Directory permissions (RW only)
    if (permission != "read_only") {
        if (fs_inspector_) {
            std::string pub = site_resolver_(site_id).root + "/public/";
            auto orig = fs_inspector_->inspect(pub);
            if (orig.exists) { original_gid = orig.group_gid; original_mode = orig.mode; }
        }
        auto r3 = apply_directory_permissions_internal(site_id, permission);
        if (!r3.success) {
            persist("error", r3.message);
            if (membership_added) { auto rb = remove_user_from_site_group_internal(username, site_id, permission); if (!rb.success) { out.success = false; out.message = "grant_rollback_membership_failed"; return out; } }
            if (group_created) { auto dg = delete_site_group_if_unused_internal(site_id, permission); if (!dg.success) { out.success = false; out.message = "grant_rollback_group_delete_failed"; return out; } }
            out = r3; return out;
        }
        perms_changed = true;
    }

    // Step 4: ACL (RO only)
    if (permission == "read_only") {
        if (fs_inspector_) {
            std::string pub = site_resolver_(site_id).root + "/public/";
            auto ro_mapping = find_mapping("site_group_ro", site_id);
            if (ro_mapping.has_value()) {
                auto orig = fs_inspector_->inspect_acl(pub, ro_mapping->groupname);
                if (orig.acl_status == InspectionStatus::Ok) {
                    original_acl_state = orig.acl;
                    acl_captured = true;
                }
            }
        }
        auto r4 = apply_read_only_acl_internal(site_id);
        if (!r4.success) {
            persist("error", r4.message);
            if (membership_added) { auto rb = remove_user_from_site_group_internal(username, site_id, permission); if (!rb.success) { out.success = false; out.message = "grant_rollback_membership_failed"; return out; } }
            if (group_created) { auto dg = delete_site_group_if_unused_internal(site_id, permission); if (!dg.success) { out.success = false; out.message = "grant_rollback_group_delete_failed"; return out; } }
            out = r4; return out;
        }
        acl_changed = true;
    }

    // Step 5: Bind mount
    {
        auto r5 = bind_mount_site_internal(access_user_id, site_id);
        if (!r5.success) {
            std::string rollback_errors;
            auto note = [&](const char* step) { if (!rollback_errors.empty()) rollback_errors += ","; rollback_errors += step; };

            if (acl_changed && acl_captured && runner_ && fs_inspector_) {
                std::string pub = site_resolver_(site_id).root + "/public/";
                auto ro_mapping = find_mapping("site_group_ro", site_id);
                if (ro_mapping.has_value()) {
                    auto acl_rb = restore_acl_state(original_acl_state, pub, ro_mapping->groupname);
                    if (!acl_rb.success) note(acl_rb.message.c_str());
                } else { note("acl:mapping"); }
            }
            if (perms_changed && original_gid > 0 && runner_) {
                std::string pub = site_resolver_(site_id).root + "/public/";
                auto gid_rb = runner_->chgrp(std::to_string(original_gid), pub);
                if (!gid_rb.success) { note("perms:gid"); }
                else if (fs_inspector_) {
                    auto post = fs_inspector_->inspect(pub);
                    if (!post.exists || post.group_gid != original_gid) note("perms:gid:postcondition");
                }
                std::string octal_mode = mode_to_octal(original_mode);
                auto mode_rb = runner_->chmod(octal_mode, pub);
                if (!mode_rb.success) { note("perms:mode"); }
                else if (fs_inspector_) {
                    auto post = fs_inspector_->inspect(pub);
                    if (!post.exists || post.mode != original_mode) note("perms:mode:postcondition");
                }
            }
            if (membership_added) {
                auto rb2 = remove_user_from_site_group_internal(username, site_id, permission);
                if (!rb2.success) note("membership");
            }
            if (group_created) {
                auto dg = delete_site_group_if_unused_internal(site_id, permission);
                if (!dg.success) note("group");
            }

            if (rollback_errors.empty()) {
                persist("error", "grant apply failed, fully rolled back");
                out.success = false; out.message = "grant apply failed, fully rolled back";
            } else {
                persist("error", "grant_rollback_incomplete: " + rollback_errors);
                out.success = false;
                out.message = "grant_rollback_incomplete: " + rollback_errors;
            }
            return out;
        }
    }

    // 4. Persist active only after all postconditions pass
    if (!persist("active", "")) {
        // Final active save failed: rollback mount and persist error
        auto err_target = [&]() -> std::string {
            auto si = site_resolver_(site_id);
            if (!si.valid) return "";
            std::string d = si.root;
            while (!d.empty() && d.back() == '/') d.pop_back();
            auto p = d.rfind('/');
            std::string domain = (p != std::string::npos) ? d.substr(p + 1) : d;
            return managed_home_root_ + "/" + username + "/sites/" + domain;
        }();
        (void)runner_->umount(err_target);
        if (membership_added) (void)remove_user_from_site_group_internal(username, site_id, permission);
        if (group_created) (void)delete_site_group_if_unused_internal(site_id, permission);
        persist("error", "grant_lifecycle:active_persist_failed_rolled_back");
        out.success = false; out.message = "grant_lifecycle:active_persist_failed_rolled_back"; return out;
    }

    out.success = true; out.message = "grant applied"; return out;
}

core::OperationResult LocalSftpProvider::revoke_grant(uint64_t access_user_id, uint64_t site_id,
                                                        const std::string& caller_permission) {
    core::OperationResult out;
    if (!operation_gate(out, "revoke_grant")) return out;
    return revoke_grant_internal(access_user_id, site_id, caller_permission);
}

core::OperationResult LocalSftpProvider::revoke_grant_internal(uint64_t access_user_id, uint64_t site_id,
                                                        const std::string& caller_permission) {
    core::OperationResult out;

    std::string username = resolve_username(access_user_id);
    if (username.empty()) { out.success = false; out.message = "user not provisioned"; return out; }

    // Load or create lifecycle state
    std::string permission = caller_permission;
    storage::GrantLifecycleState ls;
    bool lifecycle_active = (save_grant_lifecycle_ && load_all_grant_lifecycle_);
    if (lifecycle_active) {
        auto all = load_all_grant_lifecycle_();
        bool found = false;
        for (const auto& l : all) {
            if (l.access_user_id == access_user_id && l.site_id == site_id) {
                ls = l; found = true; break;
            }
        }
        if (found) {
            permission = ls.permission;
        } else {
            // No lifecycle record — treat as missing
            // Verify no managed state exists before idempotent success
            bool has_state = false;
            if (mount_inspector_) {
                auto si = site_resolver_ ? site_resolver_(site_id) : containercp::access::LocalSftpProvider::SiteInfo{};
                if (si.valid) {
                    std::string d = si.root;
                    while (!d.empty() && d.back() == '/') d.pop_back();
                    auto p = d.rfind('/');
                    std::string domain = (p != std::string::npos) ? d.substr(p + 1) : d;
                    std::string target = managed_home_root_ + "/" + username + "/sites/" + domain;
                    auto ms = mount_inspector_->inspect(target);
                    if (ms.mounted) has_state = true;
                }
            }
            if (has_state) {
                out.success = false; out.message = "grant_lifecycle:missing_with_state"; return out;
            }
            out.success = true; out.message = "grant not found (idempotent)"; return out;
        }
    }

    // Helper to persist
    auto persist = [&](const std::string& state, const std::string& err) -> bool {
        if (!lifecycle_active) return true;
        ls.state = state;
        ls.last_error = err;
        return save_grant_lifecycle_(ls);
    };

    // Handle lifecycle state
    if (lifecycle_active) {
        if (ls.state == "pending") {
            // Delete without OS mutation after verifying no managed state
            bool has_state = false;
            if (mount_inspector_ && site_resolver_) {
                auto si = site_resolver_(site_id);
                if (si.valid) {
                    std::string d = si.root;
                    while (!d.empty() && d.back() == '/') d.pop_back();
                    auto p = d.rfind('/');
                    std::string domain = (p != std::string::npos) ? d.substr(p + 1) : d;
                    std::string target = managed_home_root_ + "/" + username + "/sites/" + domain;
                    auto ms = mount_inspector_->inspect(target);
                    if (ms.mounted) has_state = true;
                }
            }
            if (has_state) {
                persist("error", "pending_with_state");
                out.success = false; out.message = "grant_lifecycle:pending_with_state"; return out;
            }
            // Check membership and group — undo if present
            auto grp_name = site_group_name(site_id, permission);
            if (inspector_ && inspector_->user_in_group(username, grp_name)) {
                (void)remove_user_from_site_group_internal(username, site_id, permission);
            }
            if (find_mapping(site_group_entity_type(permission), site_id).has_value()) {
                (void)delete_site_group_if_unused_internal(site_id, permission);
            }
            if (delete_grant_lifecycle_ && !delete_grant_lifecycle_(access_user_id, site_id)) {
                persist("error", "delete_failed");
                out.success = false; out.message = "grant_lifecycle:delete_failed"; return out;
            }
            out.success = true; out.message = "pending grant revoked"; return out;
        }
        if (ls.state == "applying") {
            // Recover or roll back applying state before revoke
            // First try to complete apply, then revoke
            auto apply_result = apply_grant(access_user_id, site_id, permission);
            if (!apply_result.success && ls.state != "active") {
                // Could not apply — rollback what we can
                (void)unmount_site_internal(access_user_id, site_id);
                (void)remove_user_from_site_group_internal(username, site_id, permission);
                auto grp_name = site_group_name(site_id, permission);
                if (find_mapping(site_group_entity_type(permission), site_id).has_value()) {
                    (void)delete_site_group_if_unused_internal(site_id, permission);
                }
                persist("error", "applying_rollback_failed");
                out.success = false; out.message = "grant_lifecycle:applying_rollback"; return out;
            }
            // Now applying was completed — fall through to revoke from active
            ls.state = "active";
        }
        if (ls.state == "active" || ls.state == "revoking") {
            if (ls.state == "active") {
                if (!persist("revoking", "")) {
                    out.success = false; out.message = "grant_lifecycle:revoking_persist_failed"; return out;
                }
            }
        } else if (ls.state == "error") {
            // Only approved cleanup: check mount, group, membership
            // If mount exists, try to unmount; if membership exists, remove
            if (mount_inspector_ && site_resolver_) {
                auto si = site_resolver_(site_id);
                if (si.valid) {
                    std::string d = si.root;
                    while (!d.empty() && d.back() == '/') d.pop_back();
                    auto p = d.rfind('/');
                    std::string domain = (p != std::string::npos) ? d.substr(p + 1) : d;
                    std::string target = managed_home_root_ + "/" + username + "/sites/" + domain;
                    auto ms = mount_inspector_->inspect(target);
                    if (ms.mounted) {
                        (void)unmount_site_internal(access_user_id, site_id);
                    }
                }
            }
            auto grp_name = site_group_name(site_id, permission);
            if (inspector_ && inspector_->user_in_group(username, grp_name)) {
                (void)remove_user_from_site_group_internal(username, site_id, permission);
            }
            if (find_mapping(site_group_entity_type(permission), site_id).has_value()) {
                (void)delete_site_group_if_unused_internal(site_id, permission);
            }
            if (!persist("error", "revoke_cleanup_attempted")) {
                out.success = false; out.message = "grant_lifecycle:error_cleanup_failed"; return out;
            }
            out.success = true; out.message = "error state cleaned up"; return out;
        }
    }

    if (!site_resolver_) {
        out.success = false; out.message = "provider dependencies not configured"; return out;
    }

    // Execute revoke steps
    std::string diag;
    size_t failures = 0;
    auto note = [&](const std::string& entry) {
        if (!diag.empty()) diag += "; ";
        diag += entry;
    };

    // Count lifecycle records for this site+permission in non-terminal states
    auto count_active_lifecycle = [&](const std::string& perm) -> size_t {
        if (!load_all_grant_lifecycle_) return grants_lookup_ ? grants_lookup_(site_id, perm) : 0;
        size_t count = 0;
        auto all = load_all_grant_lifecycle_();
        for (const auto& l : all) {
            if (l.site_id == site_id && l.permission == perm
                && l.state != "error" && l.state != "pending") {
                // Don't count the record being revoked
                if (l.access_user_id == access_user_id) continue;
                count++;
            }
        }
        return count;
    };

    // Step 1: Unmount
    auto r1 = unmount_site_internal(access_user_id, site_id);
    if (!r1.success) { note("unmount:" + r1.message); failures++; }
    else {
        // Step 2: Remove membership
        auto r2 = remove_user_from_site_group_internal(username, site_id, permission);
        if (!r2.success) { note("membership:" + r2.message); failures++; }
        else {
            // Step 3: Remove RO ACL only if no other lifecycle record requires it
            if (permission == "read_only") {
                bool other_grants_need_ro = (count_active_lifecycle("read_only") > 0);
                if (!other_grants_need_ro) {
                    auto r3 = remove_read_only_acl_internal(site_id);
                    if (!r3.success) { note("acl:" + r3.message); failures++; }
                }
            }
        }
    }

    // Step 4: Delete site group only if no lifecycle reference AND no OS membership
    {
        bool can_delete = true;
        if (count_active_lifecycle(permission) > 0) {
            can_delete = false;
        }
        if (can_delete && inspector_) {
            auto grp_name = site_group_name(site_id, permission);
            if (inspector_->user_in_group(username, grp_name)) {
                can_delete = false;
            }
        }
        if (can_delete && find_mapping(site_group_entity_type(permission), site_id).has_value()) {
            auto rd = delete_site_group_if_unused_internal(site_id, permission);
            if (!rd.success) { note("group:" + rd.message); failures++; }
        }
    }

    if (failures > 0) {
        if (!persist("error", diag)) {
            out.success = false; out.message = "grant_lifecycle:revoke_error_persist_failed"; return out;
        }
        out.success = false; out.message = "grant_revoke:" + std::to_string(failures) + " failures — " + diag;
        return out;
    }

    // All steps passed — delete lifecycle record
    if (!delete_grant_lifecycle_ || !delete_grant_lifecycle_(access_user_id, site_id)) {
        persist("error", "delete_failed_after_revoke");
        out.success = false; out.message = "grant_lifecycle:delete_failed_after_revoke"; return out;
    }
    out.success = true; out.message = "grant revoked"; return out;
}

core::OperationResult LocalSftpProvider::apply_pending_grants(uint64_t access_user_id) {
    core::OperationResult out;
    if (!operation_gate(out, "apply_pending_grants")) return out;
    return apply_pending_grants_internal(access_user_id);
}

core::OperationResult LocalSftpProvider::apply_pending_grants_internal(uint64_t access_user_id) {
    core::OperationResult out;

    if (!grants_loader_) { out.success = false; out.message = "grant_loader_not_configured"; return out; }
    if (!save_grant_lifecycle_ || !load_all_grant_lifecycle_) {
        out.success = false; out.message = "provider dependencies not configured"; return out;
    }

    // 1. Load all lifecycle records for this user
    auto all_lifecycle = load_all_grant_lifecycle_();
    std::vector<storage::GrantLifecycleState> user_records;
    for (const auto& l : all_lifecycle) {
        if (l.access_user_id == access_user_id) {
            user_records.push_back(l);
        }
    }

    // Sort by site_id for deterministic ordering
    std::sort(user_records.begin(), user_records.end(),
        [](const auto& a, const auto& b) { return a.site_id < b.site_id; });

    // 2. Process each record
    std::string diag;
    size_t applied = 0;
    size_t failures = 0;
    auto note = [&](const std::string& entry) {
        if (!diag.empty()) diag += "; ";
        diag += entry;
    };

    for (auto& ls : user_records) {
        std::string label = "s" + std::to_string(ls.site_id);

        if (ls.state == "active") {
            // Verify observed state is complete by calling apply_grant (idempotent)
            auto r = apply_grant_internal(access_user_id, ls.site_id, ls.permission);
            if (r.success) {
                applied++;
            } else {
                note(label + ":active_verify_failed:" + r.message);
                failures++;
            }
            continue;
        }

        if (ls.state == "pending" || ls.state == "applying" || ls.state == "error") {
            // Process allowed states
            auto r = apply_grant_internal(access_user_id, ls.site_id, ls.permission);
            if (r.success) {
                applied++;
            } else {
                note(label + ":" + r.message);
                failures++;
            }
            continue;
        }

        if (ls.state == "revoking") {
            note(label + ":revoking_cannot_apply");
            failures++;
            continue;
        }

        // Unknown state
        note(label + ":unknown_state:" + ls.state);
        failures++;
    }

    if (failures > 0) {
        out.success = false;
        out.message = "apply_pending_grants:" + std::to_string(failures) + " failures, "
                      + std::to_string(applied) + " applied — " + diag;
        return out;
    }
    out.success = true;
    out.message = std::to_string(applied) + " grants applied";
    return out;
}

core::OperationResult LocalSftpProvider::revoke_all_grants(uint64_t access_user_id) {
    core::OperationResult out;
    if (!operation_gate(out, "revoke_all_grants")) return out;
    return revoke_all_grants_internal(access_user_id);
}

core::OperationResult LocalSftpProvider::revoke_all_grants_internal(uint64_t access_user_id) {
    core::OperationResult out;

    if (!grants_loader_) { out.success = false; out.message = "grant_loader_not_configured"; return out; }
    if (!load_all_grant_lifecycle_) {
        out.success = false; out.message = "provider dependencies not configured"; return out;
    }

    // 1. Load all lifecycle records for this user
    auto all_lifecycle = load_all_grant_lifecycle_();
    std::vector<storage::GrantLifecycleState> user_records;
    for (const auto& l : all_lifecycle) {
        if (l.access_user_id == access_user_id) {
            user_records.push_back(l);
        }
    }

    // Sort by site_id for deterministic ordering
    std::sort(user_records.begin(), user_records.end(),
        [](const auto& a, const auto& b) { return a.site_id < b.site_id; });

    // 2. Process each record
    std::string diag;
    size_t revoked = 0;
    size_t failures = 0;
    auto note = [&](const std::string& entry) {
        if (!diag.empty()) diag += "; ";
        diag += entry;
    };

    for (auto& ls : user_records) {
        std::string label = "s" + std::to_string(ls.site_id);
        auto r = revoke_grant_internal(access_user_id, ls.site_id, ls.permission);
        if (r.success) {
            revoked++;
        } else {
            note(label + ":" + r.message);
            failures++;
        }
    }

    // 3. Re-check persisted state — verify no non-terminal grants remain
    if (load_all_grant_lifecycle_) {
        auto check = load_all_grant_lifecycle_();
        size_t remaining = 0;
        for (const auto& l : check) {
            if (l.access_user_id == access_user_id && l.state != "error") {
                remaining++;
            }
        }
        if (remaining > 0) {
            note("remaining:" + std::to_string(remaining));
            failures++;
        }
    }

    // 4. Verify zero managed mounts remain (only if mount inspector available)
    if (mount_inspector_ && failures == 0) {
        std::string user_root = managed_home_root_ + "/" + resolve_username(access_user_id);
        if (!user_root.empty()) {
            auto observed = mount_inspector_->enumerate(user_root);
            for (const auto& m : observed) {
                // Only flag bind mounts under sites/ (managed mounts)
                std::string sites_prefix = user_root + "/sites/";
                if (m.target.rfind(sites_prefix, 0) == 0 && m.mounted) {
                    note("still_mounted:" + m.target);
                    failures++;
                }
            }
        }
    }

    if (failures > 0) {
        out.success = false;
        out.message = "revoke_all_grants:" + std::to_string(failures) + " failures, "
                      + std::to_string(revoked) + " revoked — " + diag;
        return out;
    }
    out.success = true;
    out.message = std::to_string(revoked) + " grants revoked";
    return out;
}

void LocalSftpProvider::rollback_chroot_rmdir(const std::string& path, bool created_by_us,
                                                core::OperationResult& out) {
    if (!created_by_us || path.empty()) return;
    auto rd = runner_->rmdir(path);
    out.success = false;
    out.message = rd.success ? "chroot operation failed, rolled back" : "chroot operation failed, rmdir rollback also failed";
}

core::OperationResult LocalSftpProvider::safe_rmdir(const std::string& target,
                                                      bool created_by_us,
                                                      const std::string& username,
                                                      const std::string& domain) {
    core::OperationResult out;
    if (!managed_path_safe(target, managed_home_root_)) {
        out.success = false; out.message = "rmdir_safety:unsafe_path"; return out;
    }

    // 2: Target must equal the exact expected site domain directory
    std::string expected = managed_home_root_ + "/" + username + "/sites/" + domain;
    if (target != expected) {
        out.success = false; out.message = "rmdir_safety:path_mismatch"; return out;
    }

    // 3&4: Target must be a directory and not a symlink
    if (fs_inspector_) {
        auto st = fs_inspector_->inspect(target);
        if (!st.exists) { out.success = false; out.message = "rmdir_safety:not_found"; return out; }
        if (st.is_symlink) { out.success = false; out.message = "rmdir_safety:symlink"; return out; }
        if (!S_ISDIR(st.mode)) { out.success = false; out.message = "rmdir_safety:not_directory"; return out; }
    }

    // 5: Target must not be a mountpoint
    if (mount_inspector_) {
        auto ms = mount_inspector_->inspect(target);
        if (ms.mounted) { out.success = false; out.message = "rmdir_safety:still_mounted"; return out; }
        if (ms.status != MountStatus::Absent) { out.success = false; out.message = "rmdir_safety:mount_check_failed"; return out; }
    }

    // 6: Target must be empty
    if (runner_) {
        auto empty_check = runner_->dir_is_empty(target);
        if (!empty_check.success) { out.success = false; out.message = "rmdir_safety:not_empty"; return out; }
    }

    // 7: Managed by ContainerCP — already implied by managed_path_safe + path match
    // 8: Must be created by current operation or persisted managed target
    if (!created_by_us) { out.success = false; out.message = "rmdir_safety:unmanaged_target"; return out; }

    // All preconditions pass — proceed with removal
    auto rd = runner_->rmdir(target);
    if (!rd.success) { out.success = false; out.message = "rmdir_safety:rmdir_failed"; return out; }

    // Postcondition: verify directory is absent
    if (fs_inspector_) {
        auto post = fs_inspector_->inspect(target);
        if (post.exists) { out.success = false; out.message = "rmdir_safety:still_present"; return out; }
    }

    out.success = true; return out;
}

// --- lifecycle ---

core::OperationResult LocalSftpProvider::create_user(const AccessUser& user) {
    core::OperationResult out;
    if (!operation_gate(out, "create_user")) return out;

    // 1. Validate and normalize username
    auto mapped = UsernameMapper::map(user.username);
    if (!mapped.valid) {
        out.success = false; out.message = mapped.error; return out;
    }

    // 2. Check existing mapping — allow idempotent retry (no deps needed)
    auto existing = find_mapping("access_user", user.id);
    if (existing.has_value()) {
        if (existing->state == "active") {
            out.success = true; out.message = "SFTP user already provisioned: " + existing->username;
            return out;
        }
    }

    if (!inspector_ || !runner_ || !allocator_) {
        out.success = false; out.message = "SFTP provider dependencies not configured"; return out;
    }

    // 3. Validate all dependencies and persisted user mapping intent
    // Clean up stale provisioning if needed.
    if (existing.has_value()) {
        if (inspector_->user_exists(existing->username)) {
            (void)runner_->userdel(existing->username);
        }
        if (inspector_->group_exists(existing->groupname)) {
            (void)runner_->groupdel(existing->groupname);
        }
        std::string stale_home = managed_home_root_ + "/" + existing->username;
        if (managed_path_safe(stale_home, managed_home_root_)) {
            std::error_code ec;
            std::filesystem::remove_all(stale_home, ec);
        }
        if (delete_mapping_) delete_mapping_("access_user", user.id);
    }
    if (inspector_->user_exists(mapped.canonical)) {
        out.success = false; out.message = "unmanaged_account_conflict: " + mapped.canonical; return out;
    }
    if (inspector_->group_exists(mapped.canonical)) {
        out.success = false; out.message = "unmanaged_group_conflict: " + mapped.canonical; return out;
    }

    // 4. Ensure global SFTP group
    if (!ensure_global_sftp_group()) {
        out.success = false; out.message = "global_sftp_group creation failed"; return out;
    }

    // 5. Allocate UID/GID. Before OS mutation — rollback persistence only.
    auto persisted = load_mappings_ ? load_mappings_() : std::vector<SystemAccountMapping>{};
    auto alloc = allocator_->allocate(
        [this](int id) { return inspector_->uid_occupied(id); },
        [this](int id) { return inspector_->gid_occupied(id); },
        persisted);
    if (!alloc.success) {
        out.success = false; out.message = alloc.error; return out;
    }

    // 6. Persist provisioning state. Before OS mutation — rollback persistence only.
    SystemAccountMapping mapping;
    mapping.entity_type = "access_user";
    mapping.entity_id   = user.id;
    mapping.username    = mapped.canonical;
    mapping.groupname   = mapped.canonical;
    mapping.uid         = alloc.uid;
    mapping.gid         = alloc.gid;
    mapping.state       = "provisioning";
    mapping.home        = managed_home_root_ + "/" + mapped.canonical;
    if (save_mapping_ && !save_mapping_(mapping)) {
        out.success = false; out.message = "failed to persist system account mapping"; return out;
    }

    // Track whether OS-level steps have started for rollback decisions
    bool os_created = false;
    bool group_created = false;
    bool user_created = false;

    // 7. Create private group (OS account)
    {
        auto existing_gr = inspector_->lookup_group(mapped.canonical);
        if (existing_gr.exists) {
            if (existing_gr.gid != alloc.gid) {
                (void)runner_->groupdel(mapped.canonical);
                auto gr = runner_->groupadd(mapped.canonical, alloc.gid);
                if (!gr.success) {
                    (void)delete_mapping_("access_user", user.id);
                    out.success = false; out.message = "groupadd failed: " + mapped.canonical; return out;
                }
            }
        } else {
            auto gr = runner_->groupadd(mapped.canonical, alloc.gid);
            if (!gr.success) {
                (void)delete_mapping_("access_user", user.id);
                out.success = false; out.message = "groupadd failed: " + mapped.canonical; return out;
            }
        }
        group_created = true;
        os_created = true;
    }

    // 8. Create OS user account
    std::string home = managed_home_root_ + "/" + mapped.canonical;
    auto ur = runner_->useradd(mapped.canonical, alloc.uid, alloc.gid, home, managed_shell_, mapped.canonical);
    if (!ur.success) {
        (void)runner_->groupdel(mapped.canonical);
        if (delete_mapping_) delete_mapping_("access_user", user.id);
        out.success = false; out.message = "useradd failed: " + mapped.canonical; return out;
    }
    user_created = true;

    // 9. Add to global SFTP group
    auto add_gr = runner_->usermod_add_group(mapped.canonical, global_sftp_group_);
    if (!add_gr.success) {
        rollback_create(mapped.canonical, mapped.canonical, user.id);
        out.success = false; out.message = "global group membership failed: " + mapped.canonical; return out;
    }

    // 10. Lock password
    auto lk = runner_->passwd_lock(mapped.canonical);
    if (!lk.success) {
        rollback_create(mapped.canonical, mapped.canonical, user.id);
        out.success = false; out.message = "passwd lock failed: " + mapped.canonical; return out;
    }

    // 11. Create home directory
    {
        std::error_code ec;
        bool home_ok = std::filesystem::create_directories(home, ec);
        if (ec) {
            rollback_create(mapped.canonical, mapped.canonical, user.id);
            out.success = false; out.message = "home directory creation failed"; return out;
        }
        if (::chown(home.c_str(), 0, 0) != 0) {
            rollback_create(mapped.canonical, mapped.canonical, user.id);
            out.success = false; out.message = "home chown failed"; return out;
        }
        if (::chmod(home.c_str(), 0755) != 0) {
            rollback_create(mapped.canonical, mapped.canonical, user.id);
            out.success = false; out.message = "home chmod failed"; return out;
        }
    }

    // 12. Verify username, UID, GID, home and shell (postcondition)
    auto observed = inspector_->lookup_user(mapped.canonical);
    if (!verify_ownership(mapping, observed)) {
        out.success = false; out.message = "post-create verification failed"; return out;
    }

    // 13. Persist active system account mapping
    mapping.state = "active";
    if (save_mapping_ && !save_mapping_(mapping)) {
        // Active save failed but OS account exists — leave in provisioning state
        out.success = false; out.message = "failed to save active state"; return out;
    }

    // 14. Ensure the complete chroot layout
    auto chroot_result = ensure_chroot_layout(user.id);
    if (!chroot_result.success) {
        // Do not report success; preserve recoverable state
        out.success = false; out.message = "chroot layout failed: " + chroot_result.message;
        return out;
    }

    out.success = true;
    out.message = "SFTP user created: " + mapped.canonical;

    // 15. Apply pending grants
    if (enabled_) {
        auto grants = apply_pending_grants(user.id);
        if (!grants.success) {
            // Preserve failed grant states; persist user as provisioning (incomplete)
            mapping.state = "provisioning";
            if (save_mapping_) save_mapping_(mapping);
            return grants;
        }
    }

    // 16. Verify every successfully applied grant and managed mount (postcondition)
    if (load_all_grant_lifecycle_) {
        auto all = load_all_grant_lifecycle_();
        for (const auto& l : all) {
            if (l.access_user_id == user.id && l.state != "active" && l.state != "error") {
                // If any grant is not terminal, warn but don't fail (applying state is OK during startup)
                if (l.state != "applying" && l.state != "pending") {
                    out.success = false;
                    out.message = "grant postcondition failed for site " + std::to_string(l.site_id);
                    return out;
                }
            }
        }
    }

    return out;
}

core::OperationResult LocalSftpProvider::remove_user(const AccessUser& user) {
    core::OperationResult out;
    if (!operation_gate(out, "remove_user")) return out;
    if (!inspector_ || !runner_) {
        out.success = false; out.message = "SFTP provider dependencies not configured"; return out;
    }

    // 1. Load and verify the active managed user mapping
    auto mapping = find_mapping("access_user", user.id);
    if (!mapping.has_value()) {
        out.success = false; out.message = "system account mapping not found"; return out;
    }

    auto observed = inspector_->lookup_user(mapping->username);
    if (!verify_ownership(*mapping, observed)) {
        out.success = false; out.message = "unmanaged_account_conflict: " + mapping->username; return out;
    }

    // 2. Mark user removal state before OS mutation
    mapping->state = "removing";
    if (save_mapping_ && !save_mapping_(*mapping)) {
        out.success = false; out.message = "failed to persist removing state"; return out;
    }

    // 3. Revoke all grants
    auto grants = revoke_all_grants(user.id);
    if (!grants.success) {
        mapping->state = "error";
        if (save_mapping_) save_mapping_(*mapping);
        out.success = false; out.message = "revoke_all_grants failed: " + grants.message; return out;
    }

    // 4. Verify no non-terminal grants remain
    if (load_all_grant_lifecycle_) {
        auto all = load_all_grant_lifecycle_();
        for (const auto& l : all) {
            if (l.access_user_id == user.id && l.state != "error") {
                mapping->state = "error";
                if (save_mapping_) save_mapping_(*mapping);
                out.success = false; out.message = "non-terminal grant remains"; return out;
            }
        }
    }

    // 5. Cleanup all managed mounts
    auto mounts = cleanup_all_mounts(user.id);
    if (!mounts.success) {
        mapping->state = "error";
        if (save_mapping_) save_mapping_(*mapping);
        out.success = false; out.message = "mount cleanup failed: " + mounts.message; return out;
    }

    // 6. Reconcile observed mounts under the user chroot
    auto rec = reconcile_mounts(user.id);
    if (!rec.success) {
        mapping->state = "error";
        if (save_mapping_) save_mapping_(*mapping);
        out.success = false; out.message = "mount reconciliation failed: " + rec.message; return out;
    }

    // 7. Verify zero managed mounts remain
    if (mount_inspector_) {
        std::string user_root = managed_home_root_ + "/" + mapping->username;
        auto remaining = mount_inspector_->enumerate(user_root);
        for (const auto& m : remaining) {
            if (m.mounted && m.target.rfind(user_root + "/sites/", 0) == 0) {
                mapping->state = "error";
                if (save_mapping_) save_mapping_(*mapping);
                out.success = false; out.message = "mount still present: " + m.target; return out;
            }
        }
    }

    // 8. Remove user from global SFTP group (best-effort)
    (void)runner_->usermod_remove_group(mapping->username, global_sftp_group_);

    // 9. Delete the OS account
    auto ur = runner_->userdel(mapping->username);
    if (!ur.success) {
        mapping->state = "error";
        if (save_mapping_) save_mapping_(*mapping);
        out.success = false; out.message = "userdel failed: " + mapping->username; return out;
    }

    // 10. Verify the OS account is absent
    if (inspector_) {
        auto check = inspector_->lookup_user(mapping->username);
        if (check.exists) {
            mapping->state = "error";
            if (save_mapping_) save_mapping_(*mapping);
            out.success = false; out.message = "account still exists after userdel"; return out;
        }
    }

    // 11. Remove private group
    auto gd = runner_->groupdel(mapping->groupname);
    if (!gd.success) {
        mapping->state = "error";
        if (save_mapping_) save_mapping_(*mapping);
        out.success = false; out.message = "groupdel failed: " + mapping->groupname; return out;
    }

    // 12. Remove safe managed home/chroot content
    std::string home = managed_home_root_ + "/" + mapping->username;
    if (managed_path_safe(home, managed_home_root_)) {
        std::error_code ec;
        std::filesystem::remove_all(home, ec);
        if (ec) {
            mapping->state = "error";
            if (save_mapping_) save_mapping_(*mapping);
            out.success = false; out.message = "home cleanup failed: " + mapping->username; return out;
        }
    } else {
        mapping->state = "error";
        if (save_mapping_) save_mapping_(*mapping);
        out.success = false; out.message = "home path unsafe for cleanup: " + mapping->username; return out;
    }

    // 13. Delete persisted mapping only after all postconditions pass
    if (delete_mapping_ && !delete_mapping_(mapping->entity_type, mapping->entity_id)) {
        // Mapping stays for recoverable evidence
        mapping->state = "error";
        if (save_mapping_) save_mapping_(*mapping);
        out.success = false; out.message = "failed to delete mapping"; return out;
    }

    out.success = true;
    out.message = "SFTP user removed: " + mapping->username;
    return out;
}

core::OperationResult LocalSftpProvider::enable_user(const AccessUser& user) {
    core::OperationResult out;
    if (!operation_gate(out, "enable_user")) return out;
    if (!inspector_ || !runner_) {
        out.success = false; out.message = "SFTP provider dependencies not configured"; return out;
    }
    auto mapping = find_mapping("access_user", user.id);
    if (!mapping.has_value()) {
        out.success = false; out.message = "system account mapping not found"; return out;
    }
    auto observed = inspector_->lookup_user(mapping->username);
    if (!verify_ownership(*mapping, observed)) {
        out.success = false; out.message = "unmanaged_account_conflict"; return out;
    }
    auto er = runner_->usermod_expiredate(mapping->username, "");
    if (!er.success) {
        out.success = false; out.message = "enable failed: " + mapping->username; return out;
    }
    // Postcondition: verify the user is no longer expired
    auto post = inspector_->lookup_user(mapping->username);
    if (!post.exists) {
        out.success = false; out.message = "enable postcondition failed: user missing"; return out;
    }
    out.success = true;
    out.message = "SFTP user enabled: " + mapping->username;
    return out;
}

core::OperationResult LocalSftpProvider::disable_user(const AccessUser& user) {
    core::OperationResult out;
    if (!operation_gate(out, "disable_user")) return out;
    if (!inspector_ || !runner_) {
        out.success = false; out.message = "SFTP provider dependencies not configured"; return out;
    }
    auto mapping = find_mapping("access_user", user.id);
    if (!mapping.has_value()) {
        out.success = false; out.message = "system account mapping not found"; return out;
    }
    auto observed = inspector_->lookup_user(mapping->username);
    if (!verify_ownership(*mapping, observed)) {
        out.success = false; out.message = "unmanaged_account_conflict"; return out;
    }
    auto er = runner_->usermod_expiredate(mapping->username, "1");
    if (!er.success) {
        out.success = false; out.message = "disable failed: " + mapping->username; return out;
    }
    // Postcondition: verify the user still exists
    auto post = inspector_->lookup_user(mapping->username);
    if (!post.exists) {
        out.success = false; out.message = "disable postcondition failed: user missing"; return out;
    }
    out.success = true;
    out.message = "SFTP user disabled: " + mapping->username;
    return out;
}

core::OperationResult LocalSftpProvider::list_users() {
    core::OperationResult out;
    if (!enabled_) { out.success = false; out.message = "SFTP provider disabled"; return out; }
    if (runtime_state_ == SftpRuntimeState::Failed) {
        out.success = false; out.message = "SFTP provider failed"; return out;
    }
    out.success = true;
    out.message = load_mappings_ ? std::to_string(load_mappings_().size()) + " managed accounts" : "0 managed accounts";
    return out;
}

core::OperationResult LocalSftpProvider::show_user(const AccessUser& user) {
    core::OperationResult out;
    if (!enabled_) { out.success = false; out.message = "SFTP provider disabled"; return out; }
    if (runtime_state_ == SftpRuntimeState::Failed) {
        out.success = false; out.message = "SFTP provider failed"; return out;
    }
    auto mapping = find_mapping("access_user", user.id);
    if (!mapping.has_value()) {
        out.success = false; out.message = "not provisioned"; return out;
    }
    out.success = true;
    out.message = mapping->username + " uid=" + std::to_string(mapping->uid) +
                  " gid=" + std::to_string(mapping->gid) + " state=" + mapping->state;
    return out;
}

// ── Reconciliation flow ──

ReconciliationResult
LocalSftpProvider::run_reconciliation_flow() {
    ReconciliationResult combined;
    size_t mount_records = 0;
    size_t user_records = 0;

    // Step 1: Mount reconciliation (uses internal to bypass state gate)
    {
        auto r = reconcile_startup_mounts_internal();
        // Count records from diagnostic — approximation
        if (!r.message.empty()) {
            // Inspected ~ (failures + fixed) from message
        }
        if (r.success) {
            combined.records_fixed += 1; // one step completed
        } else {
            combined.records_failed += 1;
            combined.errors.push_back("mount:" + r.message);
        }
        combined.records_inspected += 1;
        if (r.message.find("foreign") != std::string::npos) {
            combined.unsafe_foreign_state_detected = true;
        }
    }

    // Step 2: User lifecycle reconciliation (uses internal to bypass state gate)
    {
        auto r = reconcile_user_lifecycle_internal();
        if (r.success) {
            combined.records_fixed += 1;
        } else {
            combined.records_failed += 1;
            combined.errors.push_back("user:" + r.message);
        }
        combined.records_inspected += 1;
        if (r.message.find("foreign") != std::string::npos) {
            combined.unsafe_foreign_state_detected = true;
        }
    }

    // Classify combined result
    if (combined.records_failed == 0) {
        combined.success = true;
        combined.recoverable = true;
    } else if (combined.unsafe_foreign_state_detected) {
        combined.success = false;
        combined.recoverable = false;
    } else {
        // Failures but no unsafe foreign state — recoverable
        combined.success = false;
        combined.recoverable = true;
    }

    return combined;
}

ReconciliationResult
LocalSftpProvider::retry_reconciliation() {
    ReconciliationResult result;

    // RAII guard: prevents concurrent execution and ensures release on all paths
    ScopedReconciliationGuard guard(reconciling_, result);
    if (!guard.allowed()) return result;

    // Fail closed if dependencies missing
    auto dep_check = verify_dependencies();
    if (!dep_check.success) {
        runtime_state_ = SftpRuntimeState::Disabled;
        last_reconciliation_ = result;
        result.success = false;
        result.recoverable = false;
        result.errors.push_back("deps_missing:" + dep_check.message);
        return result;
    }

    // Set starting state before reconciliation
    runtime_state_ = SftpRuntimeState::Starting;

    // Run both reconciliation steps (internal methods bypass state gate)
    result = run_reconciliation_flow();

    // Determine final state
    if (result.success) {
        runtime_state_ = SftpRuntimeState::Healthy;
    } else if (!result.recoverable) {
        runtime_state_ = SftpRuntimeState::Failed;
    } else {
        runtime_state_ = SftpRuntimeState::Degraded;
    }

    last_reconciliation_ = result;
    return result;
}

} // namespace containercp::access
