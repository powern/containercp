#include "TarBackupProvider.h"

#include <filesystem>
#include <sstream>
#include <sys/stat.h>

namespace containercp::backup {
namespace {

namespace fs = std::filesystem;

std::string trim_line(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ')) value.pop_back();
    while (!value.empty() && value.front() == ' ') value.erase(value.begin());
    return value;
}

bool safe_relative_tar_path(const std::string& path) {
    if (path.empty() || path[0] == '/' || path.find('\0') != std::string::npos) return false;
    fs::path p(path);
    for (const auto& part : p) {
        const auto s = part.string();
        if (s == ".." || s == ".") return false;
    }
    return true;
}

core::OperationResult validate_tar_listing(const runtime::CommandResult& result) {
    if (result.exit_code != 0) return {false, "Backup archive listing failed"};
    std::istringstream lines(result.out);
    std::string line;
    while (std::getline(lines, line)) {
        line = trim_line(line);
        if (line.empty()) continue;
        if (line[0] != '-' && line[0] != 'd') {
            return {false, "Backup archive contains unsupported entry type"};
        }
        const auto pos = line.find(" ./");
        std::string path = pos == std::string::npos ? line : line.substr(pos + 1);
        if (path.rfind("./", 0) == 0) path = path.substr(2);
        if (path.empty() || path == ".") continue;
        if (!safe_relative_tar_path(path)) {
            return {false, "Backup archive contains unsafe path"};
        }
    }
    return {true, ""};
}

} // namespace

TarBackupProvider::TarBackupProvider(logger::Logger& logger)
    : logger_(logger)
{
}

core::OperationResult TarBackupProvider::create_backup(const std::string& site_dir,
                                                         const std::string& output_path) {
    auto result = executor_.run({"tar", "-czf", output_path, "-C", site_dir, "."});
    if (result.exit_code != 0) {
        logger_.error("TarBackupProvider: create failed");
        return {false, "Backup creation failed"};
    }
    (void)::chmod(output_path.c_str(), S_IRUSR | S_IWUSR);
    logger_.info("TarBackupProvider: Created backup at " + output_path);
    return {true, ""};
}

core::OperationResult TarBackupProvider::restore_backup(const std::string& backup_path,
                                                          const std::string& site_dir) {
    auto listed = executor_.run({"tar", "-tzvf", backup_path});
    auto valid = validate_tar_listing(listed);
    if (!valid.success) return valid;
    std::error_code ec;
    fs::create_directories(site_dir, ec);
    auto result = executor_.run({"tar", "-xzf", backup_path, "-C", site_dir});
    if (result.exit_code != 0) {
        logger_.error("TarBackupProvider: restore failed");
        return {false, "Backup restoration failed"};
    }
    logger_.info("TarBackupProvider: Restored from " + backup_path);
    return {true, ""};
}

core::OperationResult TarBackupProvider::remove_backup(const std::string& backup_path) {
    std::error_code ec;
    fs::remove(backup_path, ec);
    if (ec) {
        logger_.error("TarBackupProvider: remove failed");
        return {false, "Backup removal failed"};
    }
    logger_.info("TarBackupProvider: Removed backup at " + backup_path);
    return {true, ""};
}

} // namespace containercp::backup
