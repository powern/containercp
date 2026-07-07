#ifndef CONTAINERCP_AUTH_AUTH_SERVICE_H
#define CONTAINERCP_AUTH_AUTH_SERVICE_H

#include "AuthUser.h"

namespace containercp::core { class ServiceRegistry; }

#include <chrono>
#include <string>
#include <unordered_map>

namespace containercp::auth {

struct Session {
    std::string username;
    std::string role;
    std::chrono::steady_clock::time_point created;
};

class AuthService {
public:
    explicit AuthService(core::ServiceRegistry& services);

    void initialize();

    std::string authenticate(const std::string& username, const std::string& password);
    bool change_password(const std::string& token, const std::string& old_password, const std::string& new_password);
    void logout(const std::string& token);
    Session* validate_session(const std::string& token);
    std::vector<AuthUser> list_users() const;

    static std::string hash_password(const std::string& password);

private:
    std::string generate_token() const;
    std::string find_username_by_token(const std::string& token) const;

    core::ServiceRegistry& services_;
    std::unordered_map<std::string, Session> sessions_;
};

} // namespace containercp::auth

#endif // CONTAINERCP_AUTH_AUTH_SERVICE_H
