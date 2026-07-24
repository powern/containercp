#include "access/LocalSftpProvider.h"

#include "access/UsernameMapper.h"

#include <algorithm>
#include <filesystem>
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

// --- constructor / configuration ---

LocalSftpProvider::LocalSftpProvider(logger::Logger& logger)
    : logger_(logger) {}

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

void LocalSftpProvider::set_site_root_resolver(SiteRootFn fn) {
    site_root_resolver_ = std::move(fn);
}

void LocalSftpProvider::set_grants_lookup(GrantsForSiteFn fn) {
    grants_lookup_ = std::move(fn);
}

// --- helpers ---

bool LocalSftpProvider::disabled_result(core::OperationResult& out, const char* op) const {
    out.success = false;
    out.message = std::string("SFTP provider disabled: ") + op;
    return false;
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
    std::string expected_home = managed_home_root_ + "/" + mapping.username;
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
    if (!enabled_) return disabled_result(out, "ensure_site_group"), out;
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
    if (!enabled_) return disabled_result(out, "add_user_to_site_group"), out;
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
    if (!enabled_) return disabled_result(out, "remove_user_from_site_group"), out;
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
    if (!enabled_) return disabled_result(out, "delete_site_group"), out;
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
    if (!enabled_) return disabled_result(out, "apply_directory_permissions"), out;
    if (!runner_ || !fs_inspector_) {
        out.success = false; out.message = "provider dependencies not configured"; return out;
    }
    if (site_id == 0) { out.success = false; out.message = "admin_panel_sftp_access_forbidden"; return out; }
    if (!site_root_resolver_) { out.success = false; out.message = "site root resolver not configured"; return out; }
    if (!valid_permission_for_site_dir(permission)) {
        out.success = false; out.message = "invalid permission"; return out;
    }

    std::string site_root = site_root_resolver_(site_id);
    if (site_root.empty()) { out.success = false; out.message = "site not found"; return out; }
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
    if (!enabled_) return disabled_result(out, "apply_read_only_acl"), out;
    if (!runner_ || !fs_inspector_) {
        out.success = false; out.message = "provider dependencies not configured"; return out;
    }
    if (site_id == 0) { out.success = false; out.message = "admin_panel_sftp_access_forbidden"; return out; }
    if (!site_root_resolver_) { out.success = false; out.message = "site root resolver not configured"; return out; }

    std::string site_root = site_root_resolver_(site_id);
    if (site_root.empty()) { out.success = false; out.message = "site not found"; return out; }
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
    if (!enabled_) return disabled_result(out, "remove_read_only_acl"), out;
    if (!runner_ || !fs_inspector_) {
        out.success = false; out.message = "provider dependencies not configured"; return out;
    }
    if (site_id == 0) { out.success = false; out.message = "admin_panel_sftp_access_forbidden"; return out; }
    if (!site_root_resolver_) { out.success = false; out.message = "site root resolver not configured"; return out; }

    std::string site_root = site_root_resolver_(site_id);
    if (site_root.empty()) { out.success = false; out.message = "site not found"; return out; }
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
    out.success = false; out.message = "ACL operation failed, rolled back";
}


// --- Phase 3c: Chroot Layout & Bind Mounts ---

void LocalSftpProvider::set_mount_inspector(std::shared_ptr<MountInspector> inspector) {
    mount_inspector_ = std::move(inspector);
}

core::OperationResult LocalSftpProvider::ensure_chroot_layout(uint64_t access_user_id) {
    core::OperationResult out;
    if (!enabled_) return disabled_result(out, "ensure_chroot_layout"), out;
    if (!runner_) { out.success = false; out.message = "provider dependencies not configured"; return out; }

    std::string username = resolve_username(access_user_id);
    if (username.empty()) { out.success = false; out.message = "user not provisioned"; return out; }

    auto mapping = find_mapping("access_user", access_user_id);
    if (!mapping.has_value() || mapping->state != "active") {
        out.success = false; out.message = "user mapping not active"; return out;
    }

    std::string sites_dir = managed_home_root_ + "/" + username + "/sites/";
    auto r = runner_->mkdir_p(sites_dir);
    if (!r.success) { out.success = false; out.message = "mkdir sites/ failed"; return out; }

    // Postcondition: verify directory exists
    if (fs_inspector_) {
        auto post = fs_inspector_->inspect(sites_dir);
        if (!post.exists) { out.success = false; out.message = "sites/ verification failed"; return out; }
    }

    out.success = true; out.message = "chroot layout created"; return out;
}

core::OperationResult LocalSftpProvider::bind_mount_site(uint64_t access_user_id, uint64_t site_id) {
    core::OperationResult out;
    if (!enabled_) return disabled_result(out, "bind_mount_site"), out;
    if (!runner_ || !mount_inspector_ || !site_root_resolver_) {
        out.success = false; out.message = "provider dependencies not configured"; return out;
    }
    if (site_id == 0) { out.success = false; out.message = "admin_panel_sftp_access_forbidden"; return out; }

    std::string username = resolve_username(access_user_id);
    if (username.empty()) { out.success = false; out.message = "user not provisioned"; return out; }

    std::string site_root = site_root_resolver_(site_id);
    if (site_root.empty()) { out.success = false; out.message = "site not found"; return out; }

    // Derive domain from site root (last path component)
    std::string domain = site_root;
    while (!domain.empty() && domain.back() == '/') domain.pop_back();
    auto pos = domain.rfind('/');
    domain = (pos != std::string::npos) ? domain.substr(pos + 1) : domain;

    std::string source = site_root + "/public/";
    std::string target = managed_home_root_ + "/" + username + "/sites/" + domain;

    // Check if exact expected bind already exists (idempotent)
    auto existing = mount_inspector_->inspect(target);
    if (existing.mounted && existing.is_bind && existing.source == source) {
        out.success = true; out.message = "mount already exists"; return out;
    }
    if (existing.mounted && (!existing.is_bind || existing.source != source)) {
        out.success = false; out.message = "foreign mount at target"; return out;
    }

    // Create target
    auto r1 = runner_->mkdir_p(target);
    if (!r1.success) { out.success = false; out.message = "mkdir failed"; return out; }

    // Mount
    auto r2 = runner_->mount_bind(source, target);
    if (!r2.success) {
        auto rb = runner_->rmdir(target);
        out.success = false;
        out.message = rb.success ? "mount failed" : "mount failed, rmdir also failed";
        return out;
    }

    // Verify exact identity
    auto post = mount_inspector_->inspect(target);
    if (!post.mounted || post.source != source) {
        auto um = runner_->umount(target);
        if (!um.success) { out.success = false; out.message = "mount verify fail, umount rollback fail"; return out; }
        auto rd = runner_->rmdir(target);
        out.success = false;
        out.message = rd.success ? "mount verification failed" : "mount verify fail, rmdir also failed";
        return out;
    }

    out.success = true; out.message = "mounted: " + domain; return out;
}

core::OperationResult LocalSftpProvider::unmount_site(uint64_t access_user_id, uint64_t site_id) {
    core::OperationResult out;
    if (!enabled_) return disabled_result(out, "unmount_site"), out;
    if (!runner_ || !mount_inspector_) {
        out.success = false; out.message = "provider dependencies not configured"; return out;
    }
    if (site_id == 0) { out.success = false; out.message = "admin_panel_sftp_access_forbidden"; return out; }

    std::string username = resolve_username(access_user_id);
    if (username.empty()) { out.success = false; out.message = "user not provisioned"; return out; }

    std::string site_root = site_root_resolver_(site_id);
    if (site_root.empty()) { out.success = false; out.message = "site not found"; return out; }

    std::string domain = site_root;
    while (!domain.empty() && domain.back() == '/') domain.pop_back();
    auto pos = domain.rfind('/');
    domain = (pos != std::string::npos) ? domain.substr(pos + 1) : domain;

    std::string target = managed_home_root_ + "/" + username + "/sites/" + domain;

    auto existing = mount_inspector_->inspect(target);
    if (!existing.mounted) {
        out.success = true; out.message = "already unmounted"; return out;
    }
    // Only unmount managed bind mounts pointing to expected source
    std::string expected_source = site_root + "/public/";
    if (!existing.is_bind || existing.source != expected_source) {
        out.success = false; out.message = "foreign mount — refusing to unmount"; return out;
    }

    auto r1 = runner_->umount(target);
    if (!r1.success) { out.success = false; out.message = "umount failed"; return out; }

    auto post = mount_inspector_->inspect(target);
    if (post.mounted) { out.success = false; out.message = "umount verification failed"; return out; }

    auto r2 = runner_->rmdir(target);
    if (!r2.success) { out.success = false; out.message = "rmdir failed"; return out; }

    out.success = true; out.message = "unmounted: " + domain; return out;
}

core::OperationResult LocalSftpProvider::cleanup_all_mounts(uint64_t access_user_id) {
    core::OperationResult out;
    if (!enabled_) return disabled_result(out, "cleanup_all_mounts"), out;
    if (!runner_) { out.success = false; out.message = "provider dependencies not configured"; return out; }

    std::string username = resolve_username(access_user_id);
    if (username.empty()) { out.success = false; out.message = "user not provisioned"; return out; }


    // Enumerate all mounts within the user's sites/ using mount inspector via known domains.
    // We iterate persisted grants to know which domains to check.
    size_t cleaned = 0;
    size_t failed = 0;
    if (grants_loader_) {
        auto grants = grants_loader_(access_user_id);
        for (const auto& g : grants) {
            auto r = unmount_site(access_user_id, g.site_id);
            if (r.success) cleaned++; else failed++;
        }
    }

    if (failed > 0) { out.success = false; out.message = std::to_string(cleaned) + " cleaned, " + std::to_string(failed) + " failed"; return out; }
    out.success = true;
    out.message = std::to_string(cleaned) + " mounts cleaned";
    return out;
}

// --- Phase 3d: Grant Lifecycle Integration ---

void LocalSftpProvider::set_grants_loader(LoadGrantsFn fn) { grants_loader_ = std::move(fn); }

std::string LocalSftpProvider::resolve_username(uint64_t access_user_id) {
    if (!load_mappings_) return {};
    auto mappings = load_mappings_();
    for (const auto& m : mappings) {
        if (m.entity_type == "access_user" && m.entity_id == access_user_id && m.state == "active")
            return m.username;
    }
    return {};
}

core::OperationResult LocalSftpProvider::apply_grant(uint64_t access_user_id, uint64_t site_id,
                                                       const std::string& permission) {
    core::OperationResult out;
    if (!enabled_) return disabled_result(out, "apply_grant"), out;
    if (site_id == 0) { out.success = false; out.message = "admin_panel_sftp_access_forbidden"; return out; }

    std::string username = resolve_username(access_user_id);
    if (username.empty()) { out.success = false; out.message = "user not provisioned"; return out; }

    // Step 1: Ensure site group — no rollback (idempotent, re-apply safe)
    auto r1 = ensure_site_group(site_id, permission);
    if (!r1.success) return r1;

    // Step 2: Add membership
    auto r2 = add_user_to_site_group(username, site_id, permission);
    if (!r2.success) return r2;

    // Step 3: Directory permissions (RW only) — rollback: remove membership
    if (permission != "read_only") {
        auto r3 = apply_directory_permissions(site_id, permission);
        if (!r3.success) {
            auto rb = remove_user_from_site_group(username, site_id, permission);
            if (!rb.success) { out.success = false; out.message = "grant_rollback_membership_failed"; return out; }
            return r3;
        }
    }

    // Step 4: ACL (RO only) — rollback: remove ACL + membership
    if (permission == "read_only") {
        auto r4 = apply_read_only_acl(site_id);
        if (!r4.success) {
            auto rb = remove_user_from_site_group(username, site_id, permission);
            if (!rb.success) { out.success = false; out.message = "grant_rollback_membership_failed"; return out; }
            return r4;
        }
    }

    // Step 5: Bind mount — rollback: reverse steps 2-4
    auto r5 = bind_mount_site(access_user_id, site_id);
    if (!r5.success) {
        if (permission == "read_only") {
            auto rb = remove_read_only_acl(site_id);
            if (!rb.success) { out.success = false; out.message = "grant_rollback_acl_failed"; return out; }
        }
        auto rb2 = remove_user_from_site_group(username, site_id, permission);
        if (!rb2.success) { out.success = false; out.message = "grant_rollback_membership_failed"; return out; }
        out.success = false; out.message = "grant apply failed, rolled back"; return out;
    }

    out.success = true; out.message = "grant applied"; return out;
}

core::OperationResult LocalSftpProvider::revoke_grant(uint64_t access_user_id, uint64_t site_id,
                                                        const std::string& permission) {
    core::OperationResult out;
    if (!enabled_) return disabled_result(out, "revoke_grant"), out;

    std::string username = resolve_username(access_user_id);
    if (username.empty()) { out.success = false; out.message = "user not provisioned"; return out; }

    auto r1 = unmount_site(access_user_id, site_id);
    if (!r1.success) return r1;
    auto r2 = remove_user_from_site_group(username, site_id, permission);
    if (!r2.success) return r2;
    if (permission == "read_only") {
        auto r3 = remove_read_only_acl(site_id);
        if (!r3.success) return r3;
    }

    out.success = true; out.message = "grant revoked"; return out;
}

core::OperationResult LocalSftpProvider::apply_pending_grants(uint64_t access_user_id) {
    core::OperationResult out;
    if (!enabled_) return disabled_result(out, "apply_pending_grants"), out;
    if (!grants_loader_) { out.success = true; out.message = "no grants loader"; return out; }

    auto grants = grants_loader_(access_user_id);
    for (const auto& g : grants) {
        auto r = apply_grant(access_user_id, g.site_id, g.permission);
        if (!r.success) return r;
    }
    out.success = true; out.message = std::to_string(grants.size()) + " grants applied"; return out;
}

core::OperationResult LocalSftpProvider::revoke_all_grants(uint64_t access_user_id) {
    core::OperationResult out;
    if (!enabled_) return disabled_result(out, "revoke_all_grants"), out;
    if (!grants_loader_) { out.success = true; out.message = "no grants loader"; return out; }

    auto grants = grants_loader_(access_user_id);
    size_t failed = 0;
    for (const auto& g : grants) {
        auto r = revoke_grant(access_user_id, g.site_id, g.permission);
        if (!r.success) failed++;
    }
    if (failed > 0) { out.success = false; out.message = std::to_string(failed) + " grants failed to revoke"; return out; }
    out.success = true; out.message = std::to_string(grants.size()) + " grants revoked"; return out;
}

// --- lifecycle ---

core::OperationResult LocalSftpProvider::create_user(const AccessUser& user) {
    core::OperationResult out;
    if (!enabled_) return disabled_result(out, "create_user"), out;

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
        // provisioning/error/removing — requires deps to clean up
    }

    if (!inspector_ || !runner_ || !allocator_) {
        out.success = false; out.message = "SFTP provider dependencies not configured"; return out;
    }

    // Clean up stale provisioning if needed.
    // The SQLite mapping is the ownership proof — "au-*" names belong
    // exclusively to ContainerCP. No OS verification needed for deletion.
    if (existing.has_value()) {
        if (inspector_->user_exists(existing->username)) {
            (void)runner_->userdel(existing->username);
        }
        if (inspector_->group_exists(existing->groupname)) {
            (void)runner_->groupdel(existing->groupname);
        }
        std::string home = managed_home_root_ + "/" + existing->username;
        if (managed_path_safe(home, managed_home_root_)) {
            std::error_code ec;
            std::filesystem::remove_all(home, ec);
        }
        if (delete_mapping_) delete_mapping_("access_user", user.id);
    }
    // Check for unmanaged conflicts BEFORE creating anything new
    if (inspector_->user_exists(mapped.canonical)) {
        out.success = false; out.message = "unmanaged_account_conflict: " + mapped.canonical; return out;
    }
    if (inspector_->group_exists(mapped.canonical)) {
        out.success = false; out.message = "unmanaged_group_conflict: " + mapped.canonical; return out;
    }

    // 3. Ensure global group exists
    if (!ensure_global_sftp_group()) {
        out.success = false; out.message = "global_sftp_group creation failed"; return out;
    }

    // 4. Allocate UID/GID
    auto persisted = load_mappings_ ? load_mappings_() : std::vector<SystemAccountMapping>{};
    auto alloc = allocator_->allocate(
        [this](int id) { return inspector_->uid_occupied(id); },
        [this](int id) { return inspector_->gid_occupied(id); },
        persisted);
    if (!alloc.success) {
        out.success = false; out.message = alloc.error; return out;
    }

    // 5. Persist mapping in provisioning state
    SystemAccountMapping mapping;
    mapping.entity_type = "access_user";
    mapping.entity_id   = user.id;
    mapping.username    = mapped.canonical;
    mapping.groupname   = mapped.canonical;
    mapping.uid         = alloc.uid;
    mapping.gid         = alloc.gid;
    mapping.state       = "provisioning";
    if (save_mapping_ && !save_mapping_(mapping)) {
        out.success = false; out.message = "failed to persist system account mapping"; return out;
    }

    // 6. Create private group — idempotent: handle stale group from previous attempts
    {
        auto existing_gr = inspector_->lookup_group(mapped.canonical);
        if (existing_gr.exists) {
            if (existing_gr.gid != alloc.gid) {
                // Wrong GID from a previous failed attempt — remove and recreate
                (void)runner_->groupdel(mapped.canonical);
                auto gr = runner_->groupadd(mapped.canonical, alloc.gid);
                if (!gr.success) {
                    if (delete_mapping_) delete_mapping_("access_user", user.id);
                    out.success = false; out.message = "groupadd failed: " + mapped.canonical; return out;
                }
            }
            // else: group already exists with correct GID — skip
        } else {
            auto gr = runner_->groupadd(mapped.canonical, alloc.gid);
            if (!gr.success) {
                if (delete_mapping_) delete_mapping_("access_user", user.id);
                out.success = false; out.message = "groupadd failed: " + mapped.canonical; return out;
            }
        }
    }

    // 7. Create Linux user
    std::string home = managed_home_root_ + "/" + mapped.canonical;
    auto ur = runner_->useradd(mapped.canonical, alloc.uid, alloc.gid, home, managed_shell_, mapped.canonical);
    if (!ur.success) {
        (void)runner_->groupdel(mapped.canonical);
        if (delete_mapping_) delete_mapping_("access_user", user.id);
        out.success = false; out.message = "useradd failed: " + mapped.canonical; return out;
    }

    // 8. Add to global SFTP group
    auto add_gr = runner_->usermod_add_group(mapped.canonical, global_sftp_group_);
    if (!add_gr.success) {
        rollback_create(mapped.canonical, mapped.canonical, user.id);
        out.success = false; out.message = "global group membership failed: " + mapped.canonical; return out;
    }

    // 9. Create home directory — owned by root for OpenSSH chroot compatibility
    {
        std::error_code ec;
        bool home_ok = std::filesystem::create_directory(home, ec);
        if (!home_ok || ec) {
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

    // 10. Lock password
    auto lk = runner_->passwd_lock(mapped.canonical);
    if (!lk.success) {
        rollback_create(mapped.canonical, mapped.canonical, user.id);
        out.success = false; out.message = "passwd lock failed: " + mapped.canonical; return out;
    }

    // 11. Verify observed state
    auto observed = inspector_->lookup_user(mapped.canonical);
    if (!verify_ownership(mapping, observed)) {
        // Leave mapping in "provisioning" for recovery
        out.success = false; out.message = "post-create verification failed"; return out;
    }

    // 12. Mark active
    mapping.state = "active";
    if (save_mapping_ && !save_mapping_(mapping)) {
        // Active save failed but provisioning is complete — leave mapping in provisioning
        out.success = false; out.message = "failed to save active state"; return out;
    }

    out.success = true;
    out.message = "SFTP user created: " + mapped.canonical;
    // Phase 3d: apply pending grants — fail if grants cannot be applied
    if (enabled_) {
        auto grants = apply_pending_grants(user.id);
        if (!grants.success) return grants;
    }
    return out;
}

core::OperationResult LocalSftpProvider::remove_user(const AccessUser& user) {
    core::OperationResult out;
    if (!enabled_) return disabled_result(out, "remove_user"), out;
    if (!inspector_ || !runner_) {
        out.success = false; out.message = "SFTP provider dependencies not configured"; return out;
    }

    auto mapping = find_mapping("access_user", user.id);
    if (!mapping.has_value()) {
        out.success = false; out.message = "system account mapping not found"; return out;
    }

    auto observed = inspector_->lookup_user(mapping->username);
    if (!verify_ownership(*mapping, observed)) {
        out.success = false; out.message = "unmanaged_account_conflict: " + mapping->username; return out;
    }

    // Phase 3d: revoke all grants before removing user
    auto grants = revoke_all_grants(user.id);
    if (!grants.success) return grants;

    // Remove user (without -r to avoid recursive home delete)
    auto ur = runner_->userdel(mapping->username);
    if (!ur.success) {
        out.success = false; out.message = "userdel failed: " + mapping->username; return out;
    }

    // Remove private group
    auto gd = runner_->groupdel(mapping->groupname);
    if (!gd.success) {
        out.success = false; out.message = "groupdel failed: " + mapping->groupname; return out;
    }

    // Remove home directory — only if path is safe
    std::string home = managed_home_root_ + "/" + mapping->username;
    if (managed_path_safe(home, managed_home_root_)) {
        std::error_code ec;
        std::filesystem::remove_all(home, ec);
        if (ec) {
            out.success = false; out.message = "home cleanup failed: " + mapping->username; return out;
        }
    } else {
        // Path is unsafe (symlink, outside root) — fail closed, preserve mapping
        out.success = false;
        out.message = "home path unsafe for cleanup: " + mapping->username; return out;
    }

    // Delete mapping — only if all cleanup succeeded
    if (delete_mapping_ && !delete_mapping_(mapping->entity_type, mapping->entity_id)) {
        out.success = false; out.message = "failed to delete mapping"; return out;
    }

    out.success = true;
    out.message = "SFTP user removed: " + mapping->username;
    return out;
}

core::OperationResult LocalSftpProvider::enable_user(const AccessUser& user) {
    core::OperationResult out;
    if (!enabled_) return disabled_result(out, "enable_user"), out;
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
    if (!enabled_) return disabled_result(out, "disable_user"), out;
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
    out.success = true;
    out.message = load_mappings_ ? std::to_string(load_mappings_().size()) + " managed accounts" : "0 managed accounts";
    return out;
}

core::OperationResult LocalSftpProvider::show_user(const AccessUser& user) {
    core::OperationResult out;
    if (!enabled_) { out.success = false; out.message = "SFTP provider disabled"; return out; }
    auto mapping = find_mapping("access_user", user.id);
    if (!mapping.has_value()) {
        out.success = false; out.message = "not provisioned"; return out;
    }
    out.success = true;
    out.message = mapping->username + " uid=" + std::to_string(mapping->uid) +
                  " gid=" + std::to_string(mapping->gid) + " state=" + mapping->state;
    return out;
}

} // namespace containercp::access
