#include "access/AccessUser.h"
#include "access/FilesystemPermissionInspector.h"
#include "access/LocalSftpProvider.h"
#include "access/MountInspector.h"
#include "access/SystemAccountAllocator.h"
#include "access/SystemAccountCommandRunner.h"
#include "access/SystemAccountMapping.h"
#include "access/SystemIdentityInspector.h"
#include "core/OperationResult.h"
#include "logger/Logger.h"
#include "runtime/CommandExecutor.h"
#include "storage/GrantLifecycleState.h"
#include "storage/ManagedMountState.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr int kSkip = 77;
constexpr const char* kEnableEnv = "CONTAINERCP_ARCH009_LINUX_INTEGRATION";
constexpr const char* kDisposableEnv = "CONTAINERCP_DISPOSABLE_TEST_HOST";
constexpr const char* kMarker = "/tmp/containercp-allow-arch009-linux-integration";
constexpr const char* kManagedRoot = "/srv/containercp/arch009-linux-integration/users";
constexpr const char* kCleanupRoot = "/srv/containercp/arch009-linux-integration";
constexpr const char* kAccessUsername = "arch46it";
constexpr const char* kSystemUsername = "au-arch46it";
constexpr const char* kGlobalGroup = "ccp-arch009-it";
constexpr const char* kShell = "/usr/sbin/nologin";

bool env_is_one(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && std::string(value) == "1";
}

bool file_exists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}

int skip(const std::string& reason) {
    std::cout << "SKIP: " << reason << '\n';
    return kSkip;
}

int fail(const std::string& reason) {
    std::cerr << "FAIL: " << reason << '\n';
    return 1;
}

containercp::core::OperationResult run_safe_command(
    containercp::runtime::CommandExecutor& executor,
    const std::vector<std::string>& args) {
    auto result = executor.run_safe(args);
    std::string message = result.err.empty() ? result.out : result.err;
    return {result.exit_code == 0, message, result.out};
}

void cleanup_identity(containercp::runtime::CommandExecutor& executor) {
    using containercp::access::SystemAccountCommandRunner;
    (void)run_safe_command(executor, {SystemAccountCommandRunner::canonical_path("userdel"), kSystemUsername});
    (void)run_safe_command(executor, {SystemAccountCommandRunner::canonical_path("groupdel"), kSystemUsername});
    (void)run_safe_command(executor, {SystemAccountCommandRunner::canonical_path("groupdel"), kGlobalGroup});

    std::error_code ec;
    std::filesystem::remove_all(kCleanupRoot, ec);
}

} // namespace

int main() {
#ifndef __linux__
    return skip("ARCH-009 privileged integration requires Linux");
#else
    if (!env_is_one(kEnableEnv)) {
        return skip(std::string(kEnableEnv) + "=1 not set");
    }
    if (!env_is_one(kDisposableEnv)) {
        return skip(std::string(kDisposableEnv) + "=1 not set");
    }
    if (!file_exists(kMarker)) {
        return skip(std::string("marker file missing: ") + kMarker);
    }
    if (::geteuid() != 0) {
        return skip("root privileges unavailable");
    }
    if (!file_exists(kShell)) {
        return skip(std::string("managed shell missing: ") + kShell);
    }

    auto identity_check = containercp::access::SystemAccountCommandRunner::validate_canonical_executable_identities();
    if (!identity_check.success) {
        return skip("canonical executable prerequisite failed: " + identity_check.message);
    }

    auto inspector = std::make_shared<containercp::access::RealSystemIdentityInspector>();
    if (inspector->user_exists(kSystemUsername)) {
        return skip(std::string("test user already exists: ") + kSystemUsername);
    }
    if (inspector->group_exists(kSystemUsername)) {
        return skip(std::string("test private group already exists: ") + kSystemUsername);
    }
    if (inspector->group_exists(kGlobalGroup)) {
        return skip(std::string("test global group already exists: ") + kGlobalGroup);
    }

    containercp::runtime::CommandExecutor executor;
    cleanup_identity(executor);

    std::vector<containercp::access::SystemAccountMapping> mappings;
    std::vector<containercp::storage::ManagedMountState> mounts;
    std::vector<containercp::storage::GrantLifecycleState> grant_lifecycle;

    containercp::access::LocalSftpProvider provider(containercp::logger::Logger::instance());
    provider.set_enabled(true);
    provider.set_identity_inspector(inspector);
    provider.set_filesystem_inspector(containercp::access::make_real_filesystem_inspector(executor));
    provider.set_mount_inspector(containercp::access::make_real_mount_inspector());
    provider.set_allocator(std::make_unique<containercp::access::SystemAccountAllocator>(
        containercp::access::SystemAccountAllocator::Range{10000, 19999},
        containercp::access::SystemAccountAllocator::Range{20000, 29999}));
    provider.set_managed_home_root(kManagedRoot);
    provider.set_managed_shell(kShell);
    provider.set_global_sftp_group(kGlobalGroup);
    auto runner = std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&executor](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return run_safe_command(executor, cmd.args);
        });
    runner->set_uid_range(containercp::access::SystemAccountCommandRunner::Range{10000, 19999});
    runner->set_gid_range(containercp::access::SystemAccountCommandRunner::Range{20000, 29999});
    provider.set_command_runner(std::move(runner));
    provider.set_mapping_persistence(
        [&mappings]() { return mappings; },
        [&mappings](const containercp::access::SystemAccountMapping& mapping) {
            auto it = std::find_if(mappings.begin(), mappings.end(), [&](const auto& existing) {
                return existing.entity_type == mapping.entity_type && existing.entity_id == mapping.entity_id;
            });
            if (it == mappings.end()) mappings.push_back(mapping);
            else *it = mapping;
            return true;
        },
        [&mappings](const std::string& entity_type, uint64_t entity_id) {
            auto old_size = mappings.size();
            mappings.erase(std::remove_if(mappings.begin(), mappings.end(), [&](const auto& mapping) {
                return mapping.entity_type == entity_type && mapping.entity_id == entity_id;
            }), mappings.end());
            return mappings.size() != old_size;
        });
    provider.set_grants_lookup([](uint64_t, const std::string&) { return 0; });
    provider.set_site_resolver([](uint64_t) { return containercp::access::LocalSftpProvider::SiteInfo{}; });
    provider.set_grants_loader([](uint64_t) { return std::vector<containercp::access::LocalSftpProvider::GrantInfo>{}; });
    provider.set_managed_mount_storage(
        [&mounts]() { return mounts; },
        [&mounts](const containercp::storage::ManagedMountState& mount) {
            auto it = std::find_if(mounts.begin(), mounts.end(), [&](const auto& existing) {
                return existing.access_user_id == mount.access_user_id && existing.site_id == mount.site_id;
            });
            if (it == mounts.end()) mounts.push_back(mount);
            else *it = mount;
            return true;
        },
        [&mounts](uint64_t access_user_id, uint64_t site_id) {
            auto old_size = mounts.size();
            mounts.erase(std::remove_if(mounts.begin(), mounts.end(), [&](const auto& mount) {
                return mount.access_user_id == access_user_id && mount.site_id == site_id;
            }), mounts.end());
            return mounts.size() != old_size;
        });
    provider.set_grant_lifecycle_storage(
        [&grant_lifecycle]() { return grant_lifecycle; },
        [&grant_lifecycle](const containercp::storage::GrantLifecycleState& lifecycle) {
            auto it = std::find_if(grant_lifecycle.begin(), grant_lifecycle.end(), [&](const auto& existing) {
                return existing.access_user_id == lifecycle.access_user_id && existing.site_id == lifecycle.site_id;
            });
            if (it == grant_lifecycle.end()) grant_lifecycle.push_back(lifecycle);
            else *it = lifecycle;
            return true;
        },
        [&grant_lifecycle](uint64_t access_user_id, uint64_t site_id) {
            auto old_size = grant_lifecycle.size();
            grant_lifecycle.erase(std::remove_if(grant_lifecycle.begin(), grant_lifecycle.end(), [&](const auto& lifecycle) {
                return lifecycle.access_user_id == access_user_id && lifecycle.site_id == site_id;
            }), grant_lifecycle.end());
            return grant_lifecycle.size() != old_size;
        });

    containercp::access::AccessUser user;
    user.id = 46046;
    user.name = kAccessUsername;
    user.username = kAccessUsername;
    user.enabled = true;

    auto create = provider.create_user(user);
    if (!create.success) {
        cleanup_identity(executor);
        return fail("create_user failed: " + create.message);
    }

    auto observed = inspector->lookup_user(kSystemUsername);
    if (!observed.exists || observed.uid < 10000 || observed.uid > 19999 ||
        observed.gid < 20000 || observed.gid > 29999 || observed.home != std::string(kManagedRoot) + "/" + kSystemUsername ||
        observed.shell != kShell) {
        cleanup_identity(executor);
        return fail("post-create OS identity verification failed");
    }
    if (!inspector->user_in_group(kSystemUsername, kGlobalGroup)) {
        cleanup_identity(executor);
        return fail("post-create global group membership missing");
    }
    if (!std::filesystem::is_directory(std::string(kManagedRoot) + "/" + kSystemUsername + "/sites")) {
        cleanup_identity(executor);
        return fail("post-create chroot sites directory missing");
    }

    auto remove = provider.remove_user(user);
    if (!remove.success) {
        cleanup_identity(executor);
        return fail("remove_user failed: " + remove.message);
    }

    auto post = inspector->lookup_user(kSystemUsername);
    if (post.exists || inspector->group_exists(kSystemUsername)) {
        cleanup_identity(executor);
        return fail("identity still exists after remove_user");
    }

    auto global_cleanup = run_safe_command(executor, {
        containercp::access::SystemAccountCommandRunner::canonical_path("groupdel"),
        kGlobalGroup,
    });
    if (!global_cleanup.success) {
        cleanup_identity(executor);
        return fail("global group cleanup failed: " + global_cleanup.message);
    }

    std::error_code ec;
    std::filesystem::remove_all(kCleanupRoot, ec);
    if (ec || std::filesystem::exists(kCleanupRoot)) {
        return fail("managed test root cleanup failed");
    }

    std::cout << "PASS: ARCH-009 privileged Linux integration exercised LocalSftpProvider -> "
              << "SystemAccountCommandRunner -> CommandExecutor::run_safe with real system tools\n";
    return 0;
#endif
}
