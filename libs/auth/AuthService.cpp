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
    std::string password_path = services_.config().config_root() + "/ui-password";

    auto users = services_.auth_users().list();

    if (users.empty()) {
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

        // Write plaintext password to file so operator can discover it
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

    // Subsequent starts: if admin has must_change_password=true and the
    // password file exists, re-hash the file's content and sync the
    // stored hash. This prevents desync between the file and the DB.
    for (auto& u : users) {
        if (u.username == "admin" && u.must_change_password) {
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
                    u.password_hash = expected_hash;
                    services_.auth_users().set_users(users);
                    services_.storage().save_auth_users(users);
                    services_.logger().info("Web UI: Synced password hash from " + password_path);
                }
            }
            break;
        }
    }
}

std::string AuthService::authenticate(const std::string& username, const std::string& password) {
    auto users = services_.auth_users().list();
    for (const auto& u : users) {
        if (u.username == username && u.enabled) {
            if (u.password_hash == hash_password(password)) {
                std::string token = generate_token();
                sessions_[token] = {u.username, u.role, std::chrono::steady_clock::now()};
                return token;
            }
        }
    }
    return "";
}

bool AuthService::change_password(const std::string& token, const std::string& old_password, const std::string& new_password) {
    auto it = sessions_.find(token);
    if (it == sessions_.end()) return false;

    auto users = services_.auth_users().list();
    for (auto& u : users) {
        if (u.username == it->second.username && u.password_hash == hash_password(old_password)) {
            u.password_hash = hash_password(new_password);
            u.must_change_password = false;
            services_.auth_users().set_users(users);
            services_.storage().save_auth_users(users);
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
