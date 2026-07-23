#ifndef CONTAINERCP_ACCESS_LOCAL_SFTP_PROVIDER_H
#define CONTAINERCP_ACCESS_LOCAL_SFTP_PROVIDER_H

#include "access/AccessProvider.h"
#include "access/SystemAccountAllocator.h"
#include "access/SystemAccountCommandRunner.h"
#include "access/SystemAccountMapping.h"
#include "access/SystemIdentityInspector.h"
#include "logger/Logger.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace containercp::access {

class LocalSftpProvider : public AccessProvider {
public:
    explicit LocalSftpProvider(logger::Logger& logger);

    // Inject system dependencies. Provider stays disabled-safe until called.
    void set_identity_inspector(std::shared_ptr<SystemIdentityInspector> inspector);
    void set_command_runner(std::unique_ptr<SystemAccountCommandRunner> runner);
    void set_allocator(std::unique_ptr<SystemAccountAllocator> allocator);
    void set_enabled(bool enabled);
    void set_managed_home_root(const std::string& root_path);
    void set_managed_shell(const std::string& shell);
    void set_global_sftp_group(const std::string& groupname);

    // Callback to read persisted mappings from storage.
    using LoadMappingsFn = std::function<std::vector<SystemAccountMapping>()>;
    using SaveMappingFn = std::function<bool(const SystemAccountMapping&)>;
    using DeleteMappingFn = std::function<bool(const std::string& entity_type, uint64_t entity_id)>;
    void set_mapping_persistence(LoadMappingsFn load, SaveMappingFn save,
                                 DeleteMappingFn remove);

    core::OperationResult create_user(const AccessUser& user) override;
    core::OperationResult remove_user(const AccessUser& user) override;
    core::OperationResult enable_user(const AccessUser& user) override;
    core::OperationResult disable_user(const AccessUser& user) override;
    core::OperationResult list_users() override;
    core::OperationResult show_user(const AccessUser& user) override;

private:
    bool disabled_result(core::OperationResult& out, const char* op) const;
    const SystemAccountMapping* find_mapping(const std::string& entity_type,
                                             uint64_t entity_id) const;
    bool verify_ownership(const SystemAccountMapping& mapping,
                          const ObservedUser& observed) const;

    logger::Logger& logger_;
    std::shared_ptr<SystemIdentityInspector> inspector_;
    std::unique_ptr<SystemAccountCommandRunner> runner_;
    std::unique_ptr<SystemAccountAllocator> allocator_;
    LoadMappingsFn load_mappings_;
    SaveMappingFn save_mapping_;
    DeleteMappingFn delete_mapping_;

    bool enabled_ = false;
    std::string managed_home_root_ = "/srv/containercp/users";
    std::string managed_shell_ = "/usr/sbin/nologin";
    std::string global_sftp_group_ = "containercp-sftp";
};

} // namespace containercp::access

#endif
