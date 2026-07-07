#ifndef CONTAINERCP_BACKUP_TAR_BACKUP_PROVIDER_H
#define CONTAINERCP_BACKUP_TAR_BACKUP_PROVIDER_H

#include "backup/BackupProvider.h"
#include "logger/Logger.h"

namespace containercp::backup {

class TarBackupProvider : public BackupProvider {
public:
    explicit TarBackupProvider(logger::Logger& logger);

    core::OperationResult create_backup(const std::string& site_dir,
                                         const std::string& output_path) override;
    core::OperationResult restore_backup(const std::string& backup_path,
                                          const std::string& site_dir) override;
    core::OperationResult remove_backup(const std::string& backup_path) override;

private:
    logger::Logger& logger_;
};

} // namespace containercp::backup

#endif // CONTAINERCP_BACKUP_TAR_BACKUP_PROVIDER_H
