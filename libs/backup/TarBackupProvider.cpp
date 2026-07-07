#include "TarBackupProvider.h"

#include <cstdlib>
#include <sstream>

namespace containercp::backup {

TarBackupProvider::TarBackupProvider(logger::Logger& logger)
    : logger_(logger)
{
}

core::OperationResult TarBackupProvider::create_backup(const std::string& site_dir,
                                                        const std::string& output_path) {
    std::string cmd = "tar -czf " + output_path + " -C " + site_dir + " . 2>&1";
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        logger_.error("TarBackupProvider: create failed with exit code " + std::to_string(rc));
        return {false, "Backup creation failed"};
    }
    logger_.info("TarBackupProvider: Created backup at " + output_path);
    return {true, ""};
}

core::OperationResult TarBackupProvider::restore_backup(const std::string& backup_path,
                                                         const std::string& site_dir) {
    std::string cmd = "tar -xzf " + backup_path + " -C " + site_dir + " 2>&1";
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        logger_.error("TarBackupProvider: restore failed with exit code " + std::to_string(rc));
        return {false, "Backup restoration failed"};
    }
    logger_.info("TarBackupProvider: Restored from " + backup_path);
    return {true, ""};
}

core::OperationResult TarBackupProvider::remove_backup(const std::string& backup_path) {
    std::string cmd = "rm -f " + backup_path + " 2>&1";
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        logger_.error("TarBackupProvider: remove failed with exit code " + std::to_string(rc));
        return {false, "Backup removal failed"};
    }
    logger_.info("TarBackupProvider: Removed backup at " + backup_path);
    return {true, ""};
}

} // namespace containercp::backup
