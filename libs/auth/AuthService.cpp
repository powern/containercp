#include "AuthService.h"

#include "core/ServiceRegistry.h"
#include "security/PasswordHasher.h"
#include "security/SecureRandom.h"

#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace containercp::auth {

namespace {

const std::string kTemporaryPasswordFile = "admin-temporary-password";
const std::string kPasswordAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

bool write_secret_file(const std::string& path, const std::string& value) {
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    std::string content = value + "\n";
    const char* data = content.data();
    std::size_t remaining = content.size();
    while (remaining > 0) {
        ssize_t written = ::write(fd, data, remaining);
        if (written <= 0) {
            ::close(fd);
            return false;
        }
        data += written;
        remaining -= static_cast<std::size_t>(written);
    }
    ::fsync(fd);
    ::close(fd);
    ::chmod(path.c_str(), 0600);
    return true;
}

} // namespace

AuthService::AuthService(core::ServiceRegistry& services)
    : services_(services)
    , sessions_(session_ttl_from_environment())
{
}

std::chrono::seconds AuthService::session_ttl_from_environment() {
    constexpr long kDefaultSeconds = 12 * 60 * 60;
    const char* raw = std::getenv("CONTAINERCP_SESSION_TTL_SECONDS");
    if (raw == nullptr || raw[0] == '\0') return std::chrono::seconds(kDefaultSeconds);
    char* end = nullptr;
    long value = std::strtol(raw, &end, 10);
    if (end == raw || *end != '\0' || value <= 0) return std::chrono::seconds(kDefaultSeconds);
    return std::chrono::seconds(value);
}

std::string AuthService::temporary_password_path() const {
    return services_.config().data_root() + "/secrets/" + kTemporaryPasswordFile;
}

bool AuthService::auth_store_supported(const std::vector<AuthUser>& users) const {
    if (users.empty()) return false;
    bool admin_found = false;
    for (const auto& u : users) {
        if (u.username == "admin") admin_found = true;
        if (u.password_hash.empty() || !security::PasswordHasher::is_supported_hash(u.password_hash)) {
            return false;
        }
    }
    return admin_found;
}

void AuthService::recreate_admin_account() {
    auto temp_password = security::SecureRandom::string(24, kPasswordAlphabet);
    if (!temp_password) {
        services_.logger().error("Auth: failed to generate temporary admin password");
        services_.auth_users().set_users({});
        services_.storage().save_auth_users({});
        return;
    }

    std::string hash = security::PasswordHasher::hash(*temp_password);
    if (hash.empty()) {
        services_.logger().error("Auth: failed to hash temporary admin password");
        services_.auth_users().set_users({});
        services_.storage().save_auth_users({});
        return;
    }

    AuthUser admin;
    admin.id = 1;
    admin.name = "admin";
    admin.username = "admin";
    admin.password_hash = hash;
    admin.must_change_password = true;
    admin.enabled = true;
    admin.role = "admin";

    services_.auth_users().set_users({admin});
    services_.storage().save_auth_users(services_.auth_users().list());

    std::string secrets_dir = services_.config().data_root() + "/secrets";
    ::mkdir(secrets_dir.c_str(), 0700);
    ::chmod(secrets_dir.c_str(), 0700);

    const std::string password_path = temporary_password_path();
    if (!write_secret_file(password_path, *temp_password)) {
        services_.logger().error("Auth: failed to write temporary admin password file at " + password_path);
        return;
    }

    services_.logger().info("Web UI: Admin account recreated");
    services_.logger().info("Web UI: Temporary admin password file: " + password_path);
    services_.logger().info("Auth: password hasher backend = " + std::string(security::PasswordHasher::backend_name()));
}

void AuthService::remove_temporary_password_file() const {
    ::unlink(temporary_password_path().c_str());
}

void AuthService::initialize() {
    auto users = services_.auth_users().list();
    services_.logger().info("Auth: users loaded = " + std::to_string(users.size()));
    services_.logger().info("Auth: password hasher backend = " + std::string(security::PasswordHasher::backend_name()));

    if (!auth_store_supported(users)) {
        services_.logger().warning("Auth: authentication store is empty or incompatible; recreating admin account");
        sessions_.revoke_user("admin");
        recreate_admin_account();
        return;
    }

    for (const auto& u : users) {
        services_.logger().info("Auth: user '" + u.username + "'"
            + " enabled=" + (u.enabled ? "1" : "0")
            + " must_change=" + (u.must_change_password ? "1" : "0")
            + " hash_supported=" + (security::PasswordHasher::is_supported_hash(u.password_hash) ? "yes" : "no")
            + " role=" + u.role);
    }
}

std::string AuthService::authenticate(const std::string& username, const std::string& password) {
    static const std::string dummy_hash = security::PasswordHasher::hash("containercp-dummy-password");
    auto* user = services_.auth_users().find(username);
    if (user == nullptr || !user->enabled || !security::PasswordHasher::is_supported_hash(user->password_hash)) {
        if (!dummy_hash.empty()) (void)security::PasswordHasher::verify(password, dummy_hash);
        services_.logger().info("Auth: login failed");
        return "";
    }

    if (!security::PasswordHasher::verify(password, user->password_hash)) {
        services_.logger().info("Auth: login failed");
        return "";
    }

    auto token = sessions_.create(user->username, user->role);
    if (!token) {
        services_.logger().error("Auth: failed to create session token");
        return "";
    }
    return *token;
}

bool AuthService::change_password(const std::string& token, const std::string& old_password, const std::string& new_password) {
    auto* session = sessions_.validate(token);
    if (session == nullptr) return false;

    auto users = services_.auth_users().list();
    for (auto& u : users) {
        if (u.username != session->username) continue;
        if (!security::PasswordHasher::is_supported_hash(u.password_hash)) return false;
        if (!security::PasswordHasher::verify(old_password, u.password_hash)) return false;

        std::string new_hash = security::PasswordHasher::hash(new_password);
        if (new_hash.empty()) return false;
        u.password_hash = new_hash;
        u.must_change_password = false;

        services_.auth_users().set_users(users);
        services_.storage().save_auth_users(users);
        remove_temporary_password_file();
        services_.logger().info("Auth: password changed for '" + u.username + "'");
        return true;
    }
    return false;
}

void AuthService::logout(const std::string& token) {
    sessions_.revoke(token);
}

Session* AuthService::validate_session(const std::string& token) {
    return sessions_.validate(token);
}

std::vector<AuthUser> AuthService::list_users() const {
    return services_.auth_users().list();
}

} // namespace containercp::auth
