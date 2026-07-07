#include "AuthService.h"
#include "sha256.h"
#include "core/ServiceRegistry.h"

#include <fstream>
#include <random>
#include <sys/stat.h>

namespace containercp::auth {

AuthService::AuthService(core::ServiceRegistry& services)
    : services_(services)
{
}

std::string AuthService::hash_password(const std::string& password) {
    return sha256(password);
}

void AuthService::initialize() {
    std::string auth_db_path = services_.config().database_dir() + "auth_users.db";
    std::string password_path = services_.config().config_root() + "/ui-password";

    // Diagnose: does the file exist?
    std::ifstream db_check(auth_db_path);
    bool db_exists = db_check.is_open();
    db_check.close();

    services_.logger().info("Auth: db path = " + auth_db_path);
    services_.logger().info("Auth: db exists = " + std::string(db_exists ? "yes" : "no"));

    auto users = services_.auth_users().list();
    services_.logger().info("Auth: users loaded = " + std::to_string(users.size()));

    if (users.empty()) {
        services_.logger().info("Auth: DECISION: create admin (no users found)");
        std::string chars = "abcdefghijklmnopqrstuvwxyz0123456789";
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(0, chars.size() - 1);
        std::string temp_password;
        temp_password.reserve(16);
        for (int i = 0; i < 16; ++i) {
            temp_password += chars[dist(gen)];
        }

        AuthUser admin;
        admin.id = 1;
        admin.name = "admin";
        admin.username = "admin";
        admin.password_hash = hash_password(temp_password);
        admin.must_change_password = true;
        admin.enabled = true;
        admin.role = "admin";

        services_.auth_users().create(admin);
        services_.storage().save_auth_users(services_.auth_users().list());

        // Verify the file was actually written
        {
            std::ifstream verify_db(auth_db_path);
            if (!verify_db.is_open()) {
                services_.logger().error("Auth: FATAL — failed to save " + auth_db_path
                    + ". Check that " + services_.config().database_dir() + " exists and is writable.");
            }
            verify_db.close();
        }

        std::string dir = services_.config().config_root();
        ::mkdir(dir.c_str(), 0755);
        std::ofstream of(password_path);
        if (of.is_open()) {
            of << temp_password << std::endl;
            of.close();
        }

        services_.logger().info("Web UI: Admin account created");
        services_.logger().info("Web UI: Temporary password: " + temp_password);
        services_.logger().info("Web UI: Password file: " + password_path);
        return;
    }

    // Diagnose each loaded user
    for (const auto& u : users) {
        services_.logger().info("Auth: user '" + u.username + "'"
            + " enabled=" + (u.enabled ? "1" : "0")
            + " must_change=" + (u.must_change_password ? "1" : "0")
            + " hash_present=" + (u.password_hash.empty() ? "no" : "yes")
            + " role=" + u.role);
    }

    // Find admin
    bool admin_found = false;
    for (const auto& u : users) {
        if (u.username == "admin") {
            admin_found = true;
            break;
        }
    }

    if (!admin_found) {
        // Storage has users but no admin — should not happen in normal flow.
        // Log warning and skip reseeding to avoid overwriting data.
        services_.logger().info("Auth: WARNING: no admin user in storage, but other users exist. Skipping reseed.");
        return;
    }

    // Only sync from password file if admin has must_change_password=true
    bool synced = false;
    for (auto& u : users) {
        if (u.username == "admin" && u.must_change_password) {
            services_.logger().info("Auth: DECISION: sync hash from password file (must_change=true)");
            std::ifstream f(password_path);
            if (f.is_open()) {
                std::string file_password;
                std::getline(f, file_password);
                f.close();

                if (!file_password.empty() && file_password.back() == '\r') {
                    file_password.pop_back();
                }

                std::string expected_hash = hash_password(file_password);
                if (u.password_hash != expected_hash) {
                    services_.logger().info("Auth: hash mismatch, syncing from password file");
                    u.password_hash = expected_hash;
                    services_.auth_users().set_users(users);
                    services_.storage().save_auth_users(users);
                    services_.logger().info("Web UI: Synced password hash from " + password_path);
                } else {
                    services_.logger().info("Auth: hash already matches password file, no sync needed");
                }
            } else {
                services_.logger().info("Auth: password file not found at " + password_path + ", cannot sync");
            }
            synced = true;
            break;
        }
    }

    if (!synced) {
        services_.logger().info("Auth: DECISION: skip — admin loaded from storage, must_change=false");
    }
}

std::string AuthService::authenticate(const std::string& username, const std::string& password) {
    auto users = services_.auth_users().list();
    for (const auto& u : users) {
        if (u.username == username) {
            if (!u.enabled) {
                services_.logger().info("Auth: user '" + username + "' is disabled");
                return "";
            }
            if (u.password_hash == hash_password(password)) {
                std::string token = generate_token();
                sessions_[token] = {u.username, u.role, std::chrono::steady_clock::now()};
                return token;
            }
            services_.logger().info("Auth: password mismatch for '" + username + "'");
            return "";
        }
    }
    services_.logger().info("Auth: unknown user '" + username + "'");
    return "";
}

bool AuthService::change_password(const std::string& token, const std::string& old_password, const std::string& new_password) {
    auto it = sessions_.find(token);
    if (it == sessions_.end()) return false;

    auto users = services_.auth_users().list();
    for (auto& u : users) {
        if (u.username == it->second.username && u.password_hash == hash_password(old_password)) {
            services_.logger().info("Auth: change_password for '" + u.username + "'"
                + " must_change before=" + (u.must_change_password ? "1" : "0"));

            u.password_hash = hash_password(new_password);
            u.must_change_password = false;

            services_.auth_users().set_users(users);
            services_.storage().save_auth_users(users);

            // Verify the file was actually written to disk
            {
                std::string db_path = services_.config().database_dir() + "auth_users.db";
                std::ifstream verify_file(db_path);
                if (!verify_file.is_open()) {
                    services_.logger().error("Auth: FATAL — password change could not write to " + db_path);
                }
                verify_file.close();
            }

            // Verify the save worked by reloading
            auto verify = services_.storage().load_auth_users();
            bool found = false;
            bool saved_ok = false;
            for (const auto& v : verify) {
                if (v.username == u.username) {
                    found = true;
                    saved_ok = !v.must_change_password;
                    services_.logger().info("Auth: change_password saved to "
                        + services_.config().database_dir() + "auth_users.db"
                        + " must_change after=" + (v.must_change_password ? "1" : "0"));
                    break;
                }
            }
            if (!found) {
                services_.logger().info("Auth: change_password SAVE FAILED — user not found after reload");
            } else if (!saved_ok) {
                services_.logger().info("Auth: change_password SAVE FAILED — must_change still true after reload");
            }

            return true;
        }
    }
    return false;
}

void AuthService::logout(const std::string& token) {
    sessions_.erase(token);
}

Session* AuthService::validate_session(const std::string& token) {
    auto it = sessions_.find(token);
    if (it != sessions_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<AuthUser> AuthService::list_users() const {
    return services_.auth_users().list();
}

std::string AuthService::generate_token() const {
    std::string chars = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, chars.size() - 1);
    std::string token;
    token.reserve(32);
    for (int i = 0; i < 32; ++i) {
        token += chars[dist(gen)];
    }
    return token;
}

} // namespace containercp::auth
