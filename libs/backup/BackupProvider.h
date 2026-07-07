#ifndef CONTAINERCP_BACKUP_BACKUP_PROVIDER_H
#define CONTAINERCP_BACKUP_BACKUP_PROVIDER_H

#include "core/OperationResult.h"

#include <string>

namespace containercp::backup {

class BackupProvider {
public:
    virtual ~BackupProvider() = default;

    virtual core::OperationResult create_backup(const std::string& site_dir,
                                                 const std::string& output_path) = 0;
    virtual core::OperationResult restore_backup(const std::string& backup_path,
                                                  const std::string& site_dir) = 0;
    virtual core::OperationResult remove_backup(const std::string& backup_path) = 0;
};

} // namespace containercp::backup

#endif // CONTAINERCP_BACKUP_BACKUP_PROVIDER_H
