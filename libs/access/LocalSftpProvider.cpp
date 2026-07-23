#include "access/LocalSftpProvider.h"

#include "access/UsernameMapper.h"

#include <filesystem>
#include <set>
#include <sys/stat.h>

namespace containercp::access {

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

// --- helpers ---

bool LocalSftpProvider::disabled_result(core::OperationResult& out, const char* op) const {
    out.success = false;
    out.message = std::string("SFTP provider disabled: ") + op;
    return false;
}

const SystemAccountMapping* LocalSftpProvider::find_mapping(const std::string& entity_type,
                                                             uint64_t entity_id) const {
    if (!load_mappings_) return nullptr;
    auto mappings = load_mappings_();
    for (const auto& m : mappings) {
        if (m.entity_type == entity_type && m.entity_id == entity_id) return nullptr; // placeholder return
    }
    static std::vector<SystemAccountMapping> cache; // simplified
    cache = std::move(mappings);
    for (const auto& m : cache) {
        if (m.entity_type == entity_type && m.entity_id == entity_id) return &m;
    }
    return nullptr;
}

bool LocalSftpProvider::verify_ownership(const SystemAccountMapping& mapping,
                                         const ObservedUser& observed) const {
    if (!observed.exists) return false;
    if (observed.username != mapping.username) return false;
    if (mapping.uid > 0 && observed.uid != mapping.uid) return false;
    if (observed.gid != mapping.gid) return false;
    std::string expected_home = managed_home_root_ + "/" + mapping.username;
    if (observed.home != expected_home) return false;
    if (observed.shell != managed_shell_) return false;
    return true;
}

// --- lifecycle ---

core::OperationResult LocalSftpProvider::create_user(const AccessUser& user) {
    core::OperationResult out;
    if (!enabled_) return disabled_result(out, "create_user"), out;
    if (!inspector_ || !runner_ || !allocator_) {
        out.success = false; out.message = "SFTP provider dependencies not configured"; return out;
    }

    // 1. Validate and normalize username
    auto mapped = UsernameMapper::map(user.username);
    if (!mapped.valid) {
        out.success = false; out.message = mapped.error; return out;
    }

    // 2. Check existing mapping
    auto* existing = find_mapping("access_user", user.id);
    if (existing != nullptr) {
        out.success = false; out.message = "system account mapping already exists"; return out;
    }
    if (inspector_->user_exists(mapped.canonical)) {
        out.success = false; out.message = "unmanaged_account_conflict: " + mapped.canonical; return out;
    }

    // 3. Allocate UID/GID
    auto persisted = load_mappings_ ? load_mappings_() : std::vector<SystemAccountMapping>{};
    auto alloc = allocator_->allocate(
        [this](int id) { return inspector_->uid_occupied(id); },
        [this](int id) { return inspector_->gid_occupied(id); },
        persisted);
    if (!alloc.success) {
        out.success = false; out.message = alloc.error; return out;
    }

    // 4. Persist mapping in provisioning state
    SystemAccountMapping mapping;
    mapping.entity_type = "access_user";
    mapping.entity_id   = user.id;
    mapping.username    = mapped.canonical;
    mapping.groupname   = mapped.canonical; // primary group matches username
    mapping.uid         = alloc.uid;
    mapping.gid         = alloc.gid;
    mapping.state       = "provisioning";
    if (save_mapping_ && !save_mapping_(mapping)) {
        out.success = false; out.message = "failed to persist system account mapping"; return out;
    }

    // 5. Create private group
    auto gr = runner_->groupadd(mapped.canonical, alloc.gid);
    if (!gr.success) {
        out.success = false; out.message = "groupadd failed: " + mapped.canonical; return out;
    }

    // 6. Create Linux user
    std::string home = managed_home_root_ + "/" + mapped.canonical;
    auto ur = runner_->useradd(mapped.canonical, alloc.uid, alloc.gid, home, managed_shell_, mapped.canonical);
    if (!ur.success) {
        runner_->groupdel(mapped.canonical);
        out.success = false; out.message = "useradd failed: " + mapped.canonical; return out;
    }

    // 7. Create home directory
    std::error_code ec;
    std::filesystem::create_directory(home, ec);
    if (!ec) {
        ::chown(home.c_str(), static_cast<uid_t>(alloc.uid), static_cast<gid_t>(alloc.gid));
        ::chmod(home.c_str(), 0750);
    }

    // 8. Lock password
    runner_->passwd_lock(mapped.canonical);

    // 9. Verify observed state
    auto observed = inspector_->lookup_user(mapped.canonical);
    if (!verify_ownership(mapping, observed)) {
        out.success = false; out.message = "post-create verification failed"; return out;
    }

    // 10. Mark active
    mapping.state = "active";
    if (save_mapping_) save_mapping_(mapping);

    out.success = true;
    out.message = "SFTP user created: " + mapped.canonical;
    return out;
}

core::OperationResult LocalSftpProvider::remove_user(const AccessUser& user) {
    core::OperationResult out;
    if (!enabled_) return disabled_result(out, "remove_user"), out;
    if (!inspector_ || !runner_) {
        out.success = false; out.message = "SFTP provider dependencies not configured"; return out;
    }

    auto* mapping = find_mapping("access_user", user.id);
    if (mapping == nullptr) {
        out.success = false; out.message = "system account mapping not found"; return out;
    }

    auto observed = inspector_->lookup_user(mapping->username);
    if (!verify_ownership(*mapping, observed)) {
        out.success = false; out.message = "unmanaged_account_conflict: " + mapping->username; return out;
    }

    // Remove user (without -r to avoid recursive home delete)
    auto ur = runner_->userdel(mapping->username);
    if (!ur.success) {
        out.success = false; out.message = "userdel failed: " + mapping->username; return out;
    }

    // Remove private group
    runner_->groupdel(mapping->groupname);

    // Remove home directory safely
    std::string home = managed_home_root_ + "/" + mapping->username;
    std::error_code ec;
    std::filesystem::remove_all(home, ec);

    // Delete mapping
    if (delete_mapping_) delete_mapping_(mapping->entity_type, mapping->entity_id);

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
    auto* mapping = find_mapping("access_user", user.id);
    if (mapping == nullptr) {
        out.success = false; out.message = "system account mapping not found"; return out;
    }
    auto observed = inspector_->lookup_user(mapping->username);
    if (!verify_ownership(*mapping, observed)) {
        out.success = false; out.message = "unmanaged_account_conflict"; return out;
    }
    // Clear account expiration to re-enable login
    runner_->usermod_expiredate(mapping->username, "");
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
    auto* mapping = find_mapping("access_user", user.id);
    if (mapping == nullptr) {
        out.success = false; out.message = "system account mapping not found"; return out;
    }
    auto observed = inspector_->lookup_user(mapping->username);
    if (!verify_ownership(*mapping, observed)) {
        out.success = false; out.message = "unmanaged_account_conflict"; return out;
    }
    // Set account expiration to epoch (1970-01-01) to disable login
    runner_->usermod_expiredate(mapping->username, "1");
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
    auto* mapping = find_mapping("access_user", user.id);
    if (mapping == nullptr) {
        out.success = false; out.message = "not provisioned"; return out;
    }
    out.success = true;
    out.message = mapping->username + " uid=" + std::to_string(mapping->uid) +
                  " gid=" + std::to_string(mapping->gid) + " state=" + mapping->state;
    return out;
}

} // namespace containercp::access
