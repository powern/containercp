#ifndef CONTAINERCP_AUTH_SESSION_MANAGER_H
#define CONTAINERCP_AUTH_SESSION_MANAGER_H

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

namespace containercp::auth {

struct Session {
    std::string username;
    std::string role;
    std::chrono::steady_clock::time_point created_at;
    std::chrono::steady_clock::time_point expires_at;
};

class SessionManager {
public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    explicit SessionManager(std::chrono::seconds ttl = std::chrono::hours(12));

    std::optional<std::string> create(const std::string& username, const std::string& role);
    Session* validate(const std::string& token);
    bool revoke(const std::string& token);
    void revoke_user(const std::string& username);
    void cleanup_expired();
    void set_clock_for_tests(Clock clock);

private:
    std::chrono::seconds ttl_;
    Clock clock_;
    std::unordered_map<std::string, Session> sessions_;
};

} // namespace containercp::auth

#endif // CONTAINERCP_AUTH_SESSION_MANAGER_H
