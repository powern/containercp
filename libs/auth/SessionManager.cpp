#include "SessionManager.h"

#include "security/SecureRandom.h"

namespace containercp::auth {

SessionManager::SessionManager(std::chrono::seconds ttl)
    : ttl_(ttl)
    , clock_([] { return std::chrono::steady_clock::now(); })
{
}

std::optional<std::string> SessionManager::create(const std::string& username, const std::string& role) {
    auto token = security::SecureRandom::hex(32);
    if (!token) return std::nullopt;
    auto now = clock_();
    sessions_[*token] = Session{username, role, now, now + ttl_};
    return token;
}

Session* SessionManager::validate(const std::string& token) {
    if (token.empty()) return nullptr;
    auto it = sessions_.find(token);
    if (it == sessions_.end()) return nullptr;
    if (clock_() >= it->second.expires_at) {
        sessions_.erase(it);
        return nullptr;
    }
    return &it->second;
}

bool SessionManager::revoke(const std::string& token) {
    return sessions_.erase(token) > 0;
}

void SessionManager::revoke_user(const std::string& username) {
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (it->second.username == username) {
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

void SessionManager::cleanup_expired() {
    auto now = clock_();
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (now >= it->second.expires_at) {
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

void SessionManager::set_clock_for_tests(Clock clock) {
    if (clock) clock_ = std::move(clock);
}

} // namespace containercp::auth
