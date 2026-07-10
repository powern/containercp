#include "StartupManager.h"

#include <fstream>
#include <iostream>
#include <sys/stat.h>

namespace containercp::core {

static std::string flag_path(const std::string& data_root) {
    return data_root + "/setup_completed";
}

bool StartupManager::is_setup_completed(const std::string& data_root) {
    std::ifstream f(flag_path(data_root));
    bool completed = false;
    if (f.is_open()) {
        std::string line;
        std::getline(f, line);
        completed = (line == "1");
    }
    return completed;
}

void StartupManager::mark_setup_completed(const std::string& data_root) {
    std::ofstream f(flag_path(data_root));
    if (f.is_open()) {
        f << "1\n";
    }
}

void StartupManager::mark_setup_incomplete(const std::string& data_root) {
    std::ofstream f(flag_path(data_root));
    if (f.is_open()) {
        f << "0\n";
    }
}

void StartupManager::migrate_legacy_setup_flag(const std::string& data_root, const std::string& hostname) {
    if (hostname.empty()) return;                  // not configured — no migration needed
    if (is_setup_completed(data_root)) return;     // flag is already correct

    // Check for production indicators that prove the system was initialized.
    // The auth users database is created during initial setup and persists.
    std::string auth_db = data_root + "/database/auth_users.db";
    struct stat st;
    bool has_auth_db = (::stat(auth_db.c_str(), &st) == 0 && st.st_size > 0);

    if (!has_auth_db) return;  // no evidence of prior initialization — leave for bootstrap

    // System was previously initialized but flag is corrupted — restore it
    mark_setup_completed(data_root);
    std::cerr << "[MIGRATION] Detected legacy setup_completed corruption."
              << std::endl;
    std::cerr << "[MIGRATION] Production installation detected (hostname configured,"
              << " auth database exists)." << std::endl;
    std::cerr << "[MIGRATION] Restoring setup_completed=1." << std::endl;
    std::cerr << "[MIGRATION] Migration completed successfully." << std::endl;
}

bool StartupManager::needs_bootstrap(const std::string& data_root, const std::string& hostname) {
    if (hostname.empty()) return true;
    if (!is_setup_completed(data_root)) {
        // Flag is missing or "0" — check for legacy corruption before deciding
        migrate_legacy_setup_flag(data_root, hostname);
        // Re-check after potential migration
        if (!is_setup_completed(data_root)) return true;
    }
    return false;
}

} // namespace containercp::core
