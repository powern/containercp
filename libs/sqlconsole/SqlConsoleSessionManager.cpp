#include "sqlconsole/SqlConsoleSessionManager.h"

#include "security/SecureRandom.h"

#include <openssl/evp.h>

#include <iomanip>
#include <sstream>

namespace containercp::sqlconsole {
namespace {

bool request_valid(const SqlConsoleCreateRequest& request) {
    return request.database_id > 0 && request.site_id > 0 && !request.admin_username.empty() && !request.provider.empty();
}

std::chrono::system_clock::time_point min_time(std::chrono::system_clock::time_point a,
                                               std::chrono::system_clock::time_point b) {
    return a < b ? a : b;
}

} // namespace

SqlConsoleSessionManager::SqlConsoleSessionManager(SqlConsoleSessionPolicy policy)
    : policy_(policy)
    , clock_([] { return std::chrono::system_clock::now(); })
{
    if (policy_.absolute_ttl <= std::chrono::seconds(0)) {
        policy_.absolute_ttl = std::chrono::minutes(30);
    }
    if (policy_.idle_ttl <= std::chrono::seconds(0) || policy_.idle_ttl > policy_.absolute_ttl) {
        policy_.idle_ttl = policy_.absolute_ttl;
    }
}

std::string SqlConsoleSessionManager::secret_digest(const std::string& launch_secret) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (ctx == nullptr) return {};
    const bool ok = EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1
        && EVP_DigestUpdate(ctx, launch_secret.data(), launch_secret.size()) == 1
        && EVP_DigestFinal_ex(ctx, digest, &digest_len) == 1;
    EVP_MD_CTX_free(ctx);
    if (!ok) return {};
    std::ostringstream hex;
    for (unsigned int i = 0; i < digest_len; ++i) {
        hex << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    }
    return hex.str();
}

SqlConsoleCreateResult SqlConsoleSessionManager::create(const SqlConsoleCreateRequest& request) {
    SqlConsoleCreateResult result;
    if (!request_valid(request)) {
        result.code = "invalid_request";
        result.message = "SQL Console launch request is incomplete";
        return result;
    }

    std::optional<std::string> launch_id;
    for (int attempt = 0; attempt < 8; ++attempt) {
        auto candidate = security::SecureRandom::hex(16);
        if (!candidate) break;
        if (sessions_.find(*candidate) == sessions_.end()) {
            launch_id = std::move(candidate);
            break;
        }
    }
    auto launch_secret = security::SecureRandom::hex(32);
    if (!launch_id || !launch_secret) {
        result.code = "secure_random_failed";
        result.message = "SQL Console launch secret could not be generated";
        return result;
    }

    auto digest = secret_digest(*launch_secret);
    if (digest.empty()) {
        result.code = "secret_digest_failed";
        result.message = "SQL Console launch secret could not be protected";
        return result;
    }

    const auto now = clock_();
    SqlConsoleSession session;
    session.launch_id = *launch_id;
    session.secret_digest = std::move(digest);
    session.database_id = request.database_id;
    session.site_id = request.site_id;
    session.admin_username = request.admin_username;
    session.admin_role = request.admin_role;
    session.provider = request.provider;
    session.status = SqlConsoleSessionStatus::Created;
    session.created_at = now;
    session.expires_at = now + policy_.absolute_ttl;
    session.idle_expires_at = min_time(now + policy_.idle_ttl, session.expires_at);
    session.last_seen_at = now;

    const auto inserted = sessions_.emplace(session.launch_id, std::move(session));
    result.success = true;
    result.code = "created";
    result.message = "SQL Console launch session created";
    result.launch_id = inserted.first->second.launch_id;
    result.launch_secret = *launch_secret;
    result.session = sql_console_public_session(inserted.first->second);
    return result;
}

const SqlConsoleSession* SqlConsoleSessionManager::find(const std::string& launch_id) {
    auto it = sessions_.find(launch_id);
    if (it == sessions_.end()) return nullptr;
    expire_if_needed(it->second, clock_());
    return &it->second;
}

bool SqlConsoleSessionManager::expired(const SqlConsoleSession& session, std::chrono::system_clock::time_point now) const {
    if (session.status == SqlConsoleSessionStatus::Expired || session.status == SqlConsoleSessionStatus::Revoked) {
        return false;
    }
    if (now >= session.expires_at) return true;
    if (session.status == SqlConsoleSessionStatus::Redeemed && now >= session.idle_expires_at) return true;
    return false;
}

bool SqlConsoleSessionManager::expire_if_needed(SqlConsoleSession& session, std::chrono::system_clock::time_point now) {
    if (!expired(session, now)) return false;
    session.status = SqlConsoleSessionStatus::Expired;
    return true;
}

SqlConsoleOperationResult SqlConsoleSessionManager::failure(const std::string& code,
                                                            const std::string& message,
                                                            const SqlConsoleSession* session) const {
    SqlConsoleOperationResult result;
    result.success = false;
    result.code = code;
    result.message = message;
    if (session != nullptr) {
        result.session = sql_console_public_session(*session);
    }
    return result;
}

SqlConsoleOperationResult SqlConsoleSessionManager::success(const std::string& code,
                                                            const std::string& message,
                                                            const SqlConsoleSession& session) const {
    SqlConsoleOperationResult result;
    result.success = true;
    result.code = code;
    result.message = message;
    result.session = sql_console_public_session(session);
    return result;
}

SqlConsoleOperationResult SqlConsoleSessionManager::authorize(const std::string& launch_id, const std::string& launch_secret) {
    auto it = sessions_.find(launch_id);
    if (it == sessions_.end()) return failure("session_not_found", "SQL Console launch session was not found");
    auto& session = it->second;
    const auto now = clock_();
    expire_if_needed(session, now);
    if (session.status == SqlConsoleSessionStatus::Expired) return failure("session_expired", "SQL Console launch session is expired", &session);
    if (session.status == SqlConsoleSessionStatus::Revoked) return failure("session_revoked", "SQL Console launch session is revoked", &session);
    if (secret_digest(launch_secret) != session.secret_digest) {
        return failure("invalid_secret", "SQL Console launch secret is invalid", &session);
    }
    if (session.status == SqlConsoleSessionStatus::Redeemed) {
        session.last_seen_at = now;
        session.idle_expires_at = min_time(now + policy_.idle_ttl, session.expires_at);
        return success("touched", "SQL Console launch session activity recorded", session);
    }
    return success("authorized", "SQL Console launch session authorized", session);
}

SqlConsoleOperationResult SqlConsoleSessionManager::redeem(const std::string& launch_id, const std::string& launch_secret) {
    auto it = sessions_.find(launch_id);
    if (it == sessions_.end()) return failure("session_not_found", "SQL Console launch session was not found");
    auto& session = it->second;
    const auto now = clock_();
    expire_if_needed(session, now);
    if (session.status == SqlConsoleSessionStatus::Expired) return failure("session_expired", "SQL Console launch session is expired", &session);
    if (session.status == SqlConsoleSessionStatus::Revoked) return failure("session_revoked", "SQL Console launch session is revoked", &session);
    if (policy_.single_use_redemption && session.status == SqlConsoleSessionStatus::Redeemed) {
        return failure("session_already_redeemed", "SQL Console launch session was already redeemed", &session);
    }
    if (secret_digest(launch_secret) != session.secret_digest) {
        return failure("invalid_secret", "SQL Console launch secret is invalid", &session);
    }

    session.status = SqlConsoleSessionStatus::Redeemed;
    session.redeemed_at = now;
    session.last_seen_at = now;
    session.idle_expires_at = min_time(now + policy_.idle_ttl, session.expires_at);
    return success("redeemed", "SQL Console launch session redeemed", session);
}

SqlConsoleOperationResult SqlConsoleSessionManager::touch(const std::string& launch_id, const std::string& launch_secret) {
    auto it = sessions_.find(launch_id);
    if (it == sessions_.end()) return failure("session_not_found", "SQL Console launch session was not found");
    auto& session = it->second;
    const auto now = clock_();
    expire_if_needed(session, now);
    if (session.status == SqlConsoleSessionStatus::Expired) return failure("session_expired", "SQL Console launch session is expired", &session);
    if (session.status == SqlConsoleSessionStatus::Revoked) return failure("session_revoked", "SQL Console launch session is revoked", &session);
    if (session.status != SqlConsoleSessionStatus::Redeemed) return failure("session_not_redeemed", "SQL Console launch session is not redeemed", &session);
    if (secret_digest(launch_secret) != session.secret_digest) {
        return failure("invalid_secret", "SQL Console launch secret is invalid", &session);
    }
    session.last_seen_at = now;
    session.idle_expires_at = min_time(now + policy_.idle_ttl, session.expires_at);
    return success("touched", "SQL Console launch session activity recorded", session);
}

SqlConsoleOperationResult SqlConsoleSessionManager::revoke(const std::string& launch_id) {
    auto it = sessions_.find(launch_id);
    if (it == sessions_.end()) return failure("session_not_found", "SQL Console launch session was not found");
    auto& session = it->second;
    expire_if_needed(session, clock_());
    if (session.status == SqlConsoleSessionStatus::Revoked) return failure("session_revoked", "SQL Console launch session is already revoked", &session);
    if (session.status == SqlConsoleSessionStatus::Expired) return failure("session_expired", "SQL Console launch session is already expired", &session);
    session.status = SqlConsoleSessionStatus::Revoked;
    session.revoked_at = clock_();
    return success("revoked", "SQL Console launch session revoked", session);
}

SqlConsoleOperationResult SqlConsoleSessionManager::attach_temporary_database_user(const std::string& launch_id,
                                                                                   const std::string& database_name,
                                                                                   const std::string& user_name,
                                                                                   const std::string& password) {
    auto it = sessions_.find(launch_id);
    if (it == sessions_.end()) return failure("session_not_found", "SQL Console launch session was not found");
    auto& session = it->second;
    expire_if_needed(session, clock_());
    if (session.status != SqlConsoleSessionStatus::Created) {
        return failure("session_not_attachable", "SQL Console launch session cannot accept database credentials", &session);
    }
    if (database_name.empty() || user_name.empty() || password.empty()) {
        return failure("temporary_user_invalid", "SQL Console temporary database credential is incomplete", &session);
    }
    session.database_name = database_name;
    session.temporary_user_name = user_name;
    session.temporary_user_password = password;
    return success("temporary_user_attached", "SQL Console temporary database credential attached", session);
}

SqlConsoleOperationResult SqlConsoleSessionManager::clear_temporary_database_user(const std::string& launch_id) {
    auto it = sessions_.find(launch_id);
    if (it == sessions_.end()) return failure("session_not_found", "SQL Console launch session was not found");
    auto& session = it->second;
    session.database_name.clear();
    session.temporary_user_name.clear();
    session.temporary_user_password.clear();
    return success("temporary_user_cleared", "SQL Console temporary database credential cleared", session);
}

std::size_t SqlConsoleSessionManager::sweep_expired() {
    const auto now = clock_();
    std::size_t count = 0;
    for (auto& item : sessions_) {
        if (expire_if_needed(item.second, now)) {
            ++count;
        }
    }
    return count;
}

std::vector<SqlConsolePublicSession> SqlConsoleSessionManager::list_public(uint64_t database_id) {
    (void)sweep_expired();
    std::vector<SqlConsolePublicSession> result;
    for (const auto& item : sessions_) {
        if (database_id == 0 || item.second.database_id == database_id) {
            result.push_back(sql_console_public_session(item.second));
        }
    }
    return result;
}

std::vector<SqlConsoleSession> SqlConsoleSessionManager::list_internal() const {
    std::vector<SqlConsoleSession> result;
    for (const auto& item : sessions_) {
        result.push_back(item.second);
    }
    return result;
}

void SqlConsoleSessionManager::set_clock_for_tests(Clock clock) {
    if (clock) clock_ = std::move(clock);
}

std::string sql_console_public_sessions_json(const std::vector<SqlConsolePublicSession>& sessions) {
    std::ostringstream json;
    json << "[";
    for (std::size_t i = 0; i < sessions.size(); ++i) {
        if (i > 0) json << ",";
        json << sql_console_public_session_json(sessions[i]);
    }
    json << "]";
    return json.str();
}

} // namespace containercp::sqlconsole
