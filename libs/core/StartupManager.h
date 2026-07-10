#ifndef CONTAINERCP_CORE_STARTUP_MANAGER_H
#define CONTAINERCP_CORE_STARTUP_MANAGER_H

#include <string>

namespace containercp::core {

class StartupManager {
public:
    // Check if setup has been completed
    static bool is_setup_completed(const std::string& data_root);

    // Mark setup as completed
    static void mark_setup_completed(const std::string& data_root);

    // Mark setup as incomplete (recovery mode)
    static void mark_setup_incomplete(const std::string& data_root);

    // Should the daemon start in bootstrap mode?
    static bool needs_bootstrap(const std::string& data_root, const std::string& hostname);

    // Detect and fix legacy setup_completed corruption.
    // If hostname is configured but the flag is "0", check whether the
    // system was previously initialized (auth database exists).  If so,
    // the flag was corrupted by a buggy version and needs restoration.
    static void migrate_legacy_setup_flag(const std::string& data_root, const std::string& hostname);

    // Bootstrap mode: runs lightweight HTTP server for Setup Wizard
    static int run_bootstrap(const std::string& data_root);

    // Normal mode: runs the full ContainerCP daemon
    static int run_normal(int argc, char* argv[]);
};

} // namespace containercp::core

#endif // CONTAINERCP_CORE_STARTUP_MANAGER_H
