#include "sqlconsole/DatabaseSqlConsoleService.h"

#include "security/SecureRandom.h"

namespace containercp::sqlconsole {

DatabaseSqlConsoleService::DatabaseSqlConsoleService(SqlConsoleSessionPolicy policy)
    : sessions_(policy)
{
}

DatabaseSqlConsoleService::DatabaseSqlConsoleService(const database::DatabaseProvider& provider, SqlConsoleSessionPolicy policy)
    : provider_(&provider)
    , sessions_(policy)
{
}

std::string DatabaseSqlConsoleService::temporary_user_name(const std::string& launch_id) const {
    if (launch_id.size() < 24) return {};
    return "ccp_sql_" + launch_id.substr(0, 24);
}

SqlConsoleCreateResult DatabaseSqlConsoleService::create_launch_session(const SqlConsoleCreateRequest& request) {
    return sessions_.create(request);
}

SqlConsoleProvisionResult DatabaseSqlConsoleService::create_temporary_launch_session(const SqlConsoleProvisionRequest& request) {
    SqlConsoleProvisionResult result;
    if (provider_ == nullptr) {
        result.code = "provider_unavailable";
        result.message = "SQL Console database provider is unavailable";
        return result;
    }
    if (request.database_name.empty()) {
        result.code = "database_name_required";
        result.message = "SQL Console database name is required";
        return result;
    }

    const auto created = sessions_.create(request.launch);
    result.success = created.success;
    result.code = created.code;
    result.message = created.message;
    result.launch_id = created.launch_id;
    result.launch_secret = created.launch_secret;
    result.session = created.session;
    if (!created.success) return result;

    const std::string user_name = temporary_user_name(created.launch_id);
    const auto password = security::SecureRandom::hex(32);
    if (user_name.empty() || !password) {
        (void)sessions_.revoke(created.launch_id);
        result.success = false;
        result.code = "temporary_credential_failed";
        result.message = "SQL Console temporary database credential could not be generated";
        result.launch_secret.clear();
        return result;
    }

    const auto provisioned = provider_->create_temporary_sql_console_user(request.target,
                                                                         request.service_account,
                                                                         request.database_name,
                                                                         user_name,
                                                                         *password);
    if (!provisioned.success) {
        (void)sessions_.revoke(created.launch_id);
        result.success = false;
        result.code = provisioned.code;
        result.message = provisioned.message;
        result.launch_secret.clear();
        return result;
    }

    const auto attached = sessions_.attach_temporary_database_user(created.launch_id, request.database_name, user_name, *password);
    if (!attached.success) {
        (void)provider_->drop_temporary_sql_console_user(request.target, request.service_account, request.database_name, user_name);
        (void)sessions_.revoke(created.launch_id);
        result.success = false;
        result.code = attached.code;
        result.message = attached.message;
        result.launch_secret.clear();
        return result;
    }

    result.session = attached.session;
    result.temporary_credential = {request.database_name, user_name, *password};
    return result;
}

SqlConsoleOperationResult DatabaseSqlConsoleService::redeem_launch_session(const std::string& launch_id, const std::string& launch_secret) {
    return sessions_.redeem(launch_id, launch_secret);
}

SqlConsoleOperationResult DatabaseSqlConsoleService::touch_launch_session(const std::string& launch_id, const std::string& launch_secret) {
    return sessions_.touch(launch_id, launch_secret);
}

SqlConsoleOperationResult DatabaseSqlConsoleService::revoke_launch_session(const std::string& launch_id) {
    return sessions_.revoke(launch_id);
}

SqlConsoleOperationResult DatabaseSqlConsoleService::revoke_temporary_launch_session(const SqlConsoleCleanupRequest& request) {
    const auto* session = sessions_.find(request.launch_id);
    if (session == nullptr) {
        return sessions_.revoke(request.launch_id);
    }
    if (provider_ != nullptr && !session->temporary_user_name.empty() && !session->database_name.empty()) {
        const auto dropped = provider_->drop_temporary_sql_console_user(request.target,
                                                                       request.service_account,
                                                                       session->database_name,
                                                                       session->temporary_user_name);
        if (!dropped.success) {
            SqlConsoleOperationResult result;
            result.success = false;
            result.code = dropped.code;
            result.message = dropped.message;
            result.session = sql_console_public_session(*session);
            return result;
        }
        (void)sessions_.clear_temporary_database_user(request.launch_id);
    }
    return sessions_.revoke(request.launch_id);
}

std::size_t DatabaseSqlConsoleService::sweep_expired_sessions() {
    return sessions_.sweep_expired();
}

std::vector<SqlConsolePublicSession> DatabaseSqlConsoleService::list_sessions(uint64_t database_id) {
    return sessions_.list_public(database_id);
}

SqlConsoleSessionManager& DatabaseSqlConsoleService::sessions() {
    return sessions_;
}

const SqlConsoleSessionManager& DatabaseSqlConsoleService::sessions() const {
    return sessions_;
}

} // namespace containercp::sqlconsole
