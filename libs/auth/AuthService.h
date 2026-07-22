#ifndef CONTAINERCP_AUTH_AUTH_SERVICE_H
#define CONTAINERCP_AUTH_AUTH_SERVICE_H

#include "AuthUser.h"
#include "auth/SessionManager.h"

namespace containercp::core { class ServiceRegistry; }

#include <chrono>
#include <string>
#include <vector>

namespace containercp::auth {

class AuthService {
public:
    explicit AuthService(core::ServiceRegistry& services);

    void initialize();

    std::string authenticate(const std::string& username, const std::string& password);
    bool change_password(const std::string& token, const std::string& old_password, const std::string& new_password);
    void logout(const std::string& token);
    Session* validate_session(const std::string& token);
    std::vector<AuthUser> list_users() const;

private:
    bool auth_store_supported(const std::vector<AuthUser>& users) const;
    void recreate_admin_account();
    void remove_temporary_password_file() const;
    std::string temporary_password_path() const;
    static std::chrono::seconds session_ttl_from_environment();

    core::ServiceRegistry& services_;
    SessionManager sessions_;
};

} // namespace containercp::auth

#endif // CONTAINERCP_AUTH_AUTH_SERVICE_H
