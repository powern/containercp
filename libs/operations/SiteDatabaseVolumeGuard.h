#ifndef CONTAINERCP_OPERATIONS_SITE_DATABASE_VOLUME_GUARD_H
#define CONTAINERCP_OPERATIONS_SITE_DATABASE_VOLUME_GUARD_H

#include "core/OperationResult.h"
#include "runtime/CommandExecutor.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::operations {

struct SiteDatabaseVolumePlan {
    bool exists = false;
    bool removable = false;
    bool legacy_mount_proven = false;
    std::string volume_name;
    std::string reason;
};

class DockerCommandRunner {
public:
    virtual ~DockerCommandRunner() = default;
    virtual runtime::CommandResult run(const std::vector<std::string>& args) const = 0;
};

class CommandExecutorDockerRunner : public DockerCommandRunner {
public:
    runtime::CommandResult run(const std::vector<std::string>& args) const override;
};

std::string site_compose_project_name(const std::string& domain);
std::string site_database_volume_name(const std::string& domain);

SiteDatabaseVolumePlan inspect_site_database_volume(const DockerCommandRunner& runner,
                                                    const std::string& domain,
                                                    uint64_t site_id);
core::OperationResult ensure_database_volume_absent_for_create(const DockerCommandRunner& runner,
                                                               const std::string& domain,
                                                               uint64_t site_id);
core::OperationResult remove_site_database_volume(const DockerCommandRunner& runner,
                                                  const SiteDatabaseVolumePlan& plan);

} // namespace containercp::operations

#endif // CONTAINERCP_OPERATIONS_SITE_DATABASE_VOLUME_GUARD_H
