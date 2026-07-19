#include "database/DatabaseCredentialRotationService.h"
#include "database/DatabaseCredentialRotationAdapter.h"
#include "database/DatabaseCredentialRotationJobService.h"
#include "database/MariaDBCredentialProvider.h"
#include "logger/Logger.h"
#include "wordpress/WordPressConfigService.h"
#include "wordpress/WordPressConfigUpdater.h"
#include "wordpress/WordPressRuntimeVerifier.h"

#include "doctest/doctest.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

using namespace containercp::database;
using namespace containercp;

namespace {

namespace fs = std::filesystem;

struct FakeRotationDependencies : DatabaseCredentialRotationDependencies {
    std::vector<std::string> calls;
    std::string generated = "new-secret";
    std::string fail_call;
    std::string compensation_fail_call;
    std::string seen_password;
    MariaDBSharedCredentialAssessmentState shared_state = MariaDBSharedCredentialAssessmentState::NotShared;

    DatabaseCredentialRotationStepResult ok(const std::string& call) {
        calls.push_back(call);
        DatabaseCredentialRotationStepResult result;
        if (call == fail_call || call == compensation_fail_call) {
            result.success = false;
            result.code = call + "_failed";
            result.message = "failure with secret new-secret";
            return result;
        }
        result.success = true;
        result.code = "ok";
        result.message = "ok";
        return result;
    }

    DatabaseCredentialRotationStepResult inspect_wordpress(const DatabaseCredentialRotationRequest&) override {
        return ok("inspect_wordpress");
    }
    DatabaseCredentialRotationStepResult verify_old_credential(const DatabaseCredentialRotationRequest&) override {
        return ok("verify_old_credential");
    }
    DatabaseCredentialRotationStepResult assess_shared_user(const DatabaseCredentialRotationRequest&) override {
        calls.push_back("assess_shared_user");
        if (fail_call == "assess_shared_user") {
            DatabaseCredentialRotationStepResult result;
            result.success = false;
            result.code = "assess_shared_user_failed";
            result.message = "failure with secret new-secret";
            return result;
        }
        DatabaseCredentialRotationStepResult result;
        result.success = true;
        result.code = "ok";
        result.message = "ok";
        result.shared_assessment.state = shared_state;
        result.shared_assessment.identity = {"wp_user", "%"};
        return result;
    }
    DatabaseCredentialRotationStepResult generate_password(const DatabaseCredentialRotationRequest&) override {
        calls.push_back("generate_password");
        if (fail_call == "generate_password") {
            DatabaseCredentialRotationStepResult result;
            result.success = false;
            result.code = "generate_password_failed";
            result.message = "failure with secret new-secret";
            return result;
        }
        DatabaseCredentialRotationStepResult result;
        result.success = true;
        result.code = "ok";
        result.message = "ok";
        result.generated_password = generated;
        return result;
    }
    DatabaseCredentialRotationStepResult change_mariadb_password(const DatabaseCredentialRotationRequest&, const std::string& new_password) override {
        seen_password = new_password;
        return ok("change_mariadb_password");
    }
    DatabaseCredentialRotationStepResult update_wordpress_config(const DatabaseCredentialRotationRequest&, const std::string& new_password) override {
        seen_password = new_password;
        return ok("update_wordpress_config");
    }
    DatabaseCredentialRotationStepResult apply_runtime(const DatabaseCredentialRotationRequest&) override {
        return ok("apply_runtime");
    }
    DatabaseCredentialRotationStepResult verify_new_credential(const DatabaseCredentialRotationRequest&, const std::string& new_password) override {
        seen_password = new_password;
        return ok("verify_new_credential");
    }
    DatabaseCredentialRotationStepResult verify_wordpress(const DatabaseCredentialRotationRequest&) override {
        return ok("verify_wordpress");
    }
    DatabaseCredentialRotationStepResult verify_site_health(const DatabaseCredentialRotationRequest&) override {
        return ok("verify_site_health");
    }
    DatabaseCredentialRotationStepResult persist_metadata(const DatabaseCredentialRotationRequest&, const std::string& new_password) override {
        seen_password = new_password;
        return ok("persist_metadata");
    }
    DatabaseCredentialRotationStepResult restore_mariadb_password(const DatabaseCredentialRotationRequest&, const std::string& new_password) override {
        seen_password = new_password;
        return ok("restore_mariadb_password");
    }
    DatabaseCredentialRotationStepResult restore_wordpress_config(const DatabaseCredentialRotationRequest&) override {
        return ok("restore_wordpress_config");
    }
    DatabaseCredentialRotationStepResult restore_runtime(const DatabaseCredentialRotationRequest&) override {
        return ok("restore_runtime");
    }
};

struct ParsedMariaDBBundle {
    std::string defaults;
    std::string sql;
};

ParsedMariaDBBundle parse_mariadb_bundle(const std::string& content) {
    std::istringstream stream(content);
    std::string magic;
    std::string conf_len_text;
    std::string sql_len_text;
    std::getline(stream, magic);
    std::getline(stream, conf_len_text);
    std::getline(stream, sql_len_text);
    const auto conf_len = static_cast<std::size_t>(std::stoull(conf_len_text));
    const auto sql_len = static_cast<std::size_t>(std::stoull(sql_len_text));
    ParsedMariaDBBundle parsed;
    parsed.defaults.resize(conf_len);
    stream.read(parsed.defaults.data(), static_cast<std::streamsize>(conf_len));
    parsed.sql.resize(sql_len);
    stream.read(parsed.sql.data(), static_cast<std::streamsize>(sql_len));
    return parsed;
}

std::string read_test_file(const fs::path& path) {
    std::ifstream in(path);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void write_test_file(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
}

fs::path rotation_temp_root(const std::string& name) {
    static int unique = 0;
    return fs::temp_directory_path() / ("containercp_rotation_adapter_" + name + "_" + std::to_string(++unique));
}

std::string wp_config_content(const std::string& password) {
    return "<?php\n"
           "define('DB_NAME', 'wp_db');\n"
           "define('DB_USER', 'wp_user');\n"
           "define('DB_PASSWORD', '" + password + "');\n"
           "define('DB_HOST', 'mariadb');\n";
}

struct FakeMariaDBAdapterRunner : MariaDBProcessRunner {
    mutable std::vector<std::string> sql_statements;
    mutable std::vector<std::string> defaults_files;
    mutable std::string active_password = "oldpass";
    int schema_grants = 1;

    runtime::CommandResult run_with_stdin_file(const std::vector<std::string>&,
                                               const std::string& stdin_file,
                                               const std::string&) const override {
        const auto parsed = parse_mariadb_bundle(read_test_file(stdin_file));
        sql_statements.push_back(parsed.sql);
        defaults_files.push_back(parsed.defaults);
        runtime::CommandResult result;
        result.exit_code = 0;
        if (parsed.sql.find("mysql.user") != std::string::npos) {
            result.out = "exact_identity\t1\nusername_identities\t1\nother_host_identities\t0\nschema_grants\t" +
                         std::to_string(schema_grants) + "\n";
        }
        if (parsed.sql.find("ALTER USER") != std::string::npos) {
            if (parsed.sql.find("newpass") != std::string::npos) {
                active_password = "newpass";
            } else if (parsed.sql.find("oldpass") != std::string::npos) {
                active_password = "oldpass";
            }
        }
        return result;
    }
};

struct FakeWordPressAdapterRunner : wordpress::WordPressRuntimeCommandRunner {
    mutable int calls = 0;

    runtime::CommandResult run(const std::vector<std::string>&,
                               const std::string&) const override {
        ++calls;
        runtime::CommandResult result;
        result.exit_code = 0;
        result.out = "ok";
        return result;
    }
};

struct AdapterFixture {
    fs::path root;
    site::SiteManager sites;
    DatabaseManager databases;
    wordpress::WordPressConfigService wordpress_service;
    wordpress::WordPressConfigUpdater updater;
    FakeMariaDBAdapterRunner mariadb_runner;
    MariaDBCredentialProvider mariadb_provider;
    FakeWordPressAdapterRunner wordpress_runner;
    wordpress::WordPressRuntimeVerifier wordpress_verifier;
    bool metadata_persisted = false;
    bool runtime_applied = false;
    bool health_checked = false;

    explicit AdapterFixture(const std::string& name)
        : root(rotation_temp_root(name))
        , wordpress_service(sites, root)
        , mariadb_provider(mariadb_runner)
        , wordpress_verifier(wordpress_runner) {
        fs::remove_all(root);
        const auto site_id = sites.create("example.test", "admin", 1, "nginx");
        databases.create("wp_db", "wp_user", "oldpass", 1, site_id);
        write_test_file(root / "example.test" / "public" / "wp-config.php", wp_config_content("oldpass"));
        write_test_file(root / "example.test" / ".env", "MYSQL_ROOT_PASSWORD=rootpass\n");
    }

    DatabaseCredentialRotationAdapter make_adapter() {
        return DatabaseCredentialRotationAdapter(
            sites,
            databases,
            wordpress_service,
            updater,
            mariadb_provider,
            wordpress_verifier,
            logger::Logger::instance(),
            []() { return std::string("newpass"); },
            [this]() {
                metadata_persisted = true;
                return true;
            },
            [this](const site::Site&) {
                runtime_applied = true;
                return true;
            },
            [this](const site::Site&) {
                health_checked = true;
                return true;
            });
    }
};

} // namespace

TEST_CASE("DatabaseCredentialRotationService state strings are stable") {
    CHECK(database_credential_rotation_state_to_string(DatabaseCredentialRotationState::NotStarted) == "not_started");
    CHECK(database_credential_rotation_state_to_string(DatabaseCredentialRotationState::LockAcquired) == "lock_acquired");
    CHECK(database_credential_rotation_state_to_string(DatabaseCredentialRotationState::AssessingSharedUser) == "assessing_shared_user");
    CHECK(database_credential_rotation_state_to_string(DatabaseCredentialRotationState::Compensating) == "compensating");
    CHECK(database_credential_rotation_state_to_string(DatabaseCredentialRotationState::ManualRecoveryRequired) == "manual_recovery_required");
    CHECK(database_credential_rotation_state_to_string(DatabaseCredentialRotationState::Completed) == "completed");
}

TEST_CASE("DatabaseCredentialRotationService preserves site_id zero as a valid operation identity") {
    DatabaseCredentialRotationService service;
    const auto result = service.rotate({0, 1, "example.com"});

    CHECK_FALSE(result.success);
    CHECK(result.final_state == DatabaseCredentialRotationState::Failed);
    CHECK(result.code == "rotation_dependencies_missing");
    CHECK_FALSE(service.is_locked(0, 1));
}

TEST_CASE("DatabaseCredentialRotationService rejects missing database id") {
    DatabaseCredentialRotationService service;
    const auto result = service.rotate({1, 0, "example.com"});

    CHECK_FALSE(result.success);
    CHECK(result.code == "database_required");
    CHECK_FALSE(service.is_locked(1, 0));
}

TEST_CASE("DatabaseCredentialRotationService releases lock after failure") {
    DatabaseCredentialRotationService service;
    const auto result = service.rotate({1, 2, "example.com"});

    CHECK_FALSE(result.success);
    CHECK(result.code == "rotation_dependencies_missing");
    CHECK_FALSE(service.is_locked(1, 2));
    REQUIRE(result.events.size() >= 3);
    CHECK(result.events[0].state == DatabaseCredentialRotationState::LockAcquired);
    CHECK(result.events[1].state == DatabaseCredentialRotationState::InspectingWordPress);
    CHECK(result.events.back().state == DatabaseCredentialRotationState::Failed);
}

TEST_CASE("DatabaseCredentialRotationService events do not expose confirmation text") {
    DatabaseCredentialRotationService service;
    const auto result = service.rotate({1, 2, "secret-domain-confirmation"});

    for (const auto& e : result.events) {
        CHECK(e.message.find("secret-domain-confirmation") == std::string::npos);
        CHECK(e.code.find("secret-domain-confirmation") == std::string::npos);
    }
    CHECK(result.message.find("secret-domain-confirmation") == std::string::npos);
}

TEST_CASE("DatabaseCredentialRotationService executes happy path in order") {
    FakeRotationDependencies deps;
    deps.generated = "generated-secret";
    DatabaseCredentialRotationService service(deps);

    const auto result = service.rotate({10, 20, "example.com"});

    CHECK(result.success);
    CHECK(result.final_state == DatabaseCredentialRotationState::Completed);
    CHECK(result.code == "completed");
    CHECK(deps.seen_password == "generated-secret");
    CHECK(deps.calls == std::vector<std::string>{
        "inspect_wordpress",
        "verify_old_credential",
        "assess_shared_user",
        "generate_password",
        "change_mariadb_password",
        "update_wordpress_config",
        "apply_runtime",
        "verify_new_credential",
        "verify_wordpress",
        "verify_site_health",
        "persist_metadata",
    });
    CHECK_FALSE(service.is_locked(10, 20));
}

TEST_CASE("DatabaseCredentialRotationService rejects unsupported inspection before mutation") {
    FakeRotationDependencies deps;
    deps.fail_call = "inspect_wordpress";
    DatabaseCredentialRotationService service(deps);

    const auto result = service.rotate({10, 20, "example.com"});

    CHECK_FALSE(result.success);
    CHECK(result.code == "inspect_wordpress_failed");
    CHECK(result.message.find("new-secret") == std::string::npos);
    for (const auto& event : result.events) {
        CHECK(event.message.find("new-secret") == std::string::npos);
    }
    CHECK(deps.calls == std::vector<std::string>{"inspect_wordpress"});
    CHECK_FALSE(service.is_locked(10, 20));
}

TEST_CASE("DatabaseCredentialRotationService blocks shared credential before password generation") {
    FakeRotationDependencies deps;
    deps.shared_state = MariaDBSharedCredentialAssessmentState::Shared;
    DatabaseCredentialRotationService service(deps);

    const auto result = service.rotate({10, 20, "example.com"});

    CHECK_FALSE(result.success);
    CHECK(result.code == "shared_user_assessment_shared");
    CHECK(result.message.find("new-secret") == std::string::npos);
    CHECK(deps.calls == std::vector<std::string>{
        "inspect_wordpress",
        "verify_old_credential",
        "assess_shared_user",
    });
}

TEST_CASE("DatabaseCredentialRotationService blocks unknown shared credential state") {
    FakeRotationDependencies deps;
    deps.shared_state = MariaDBSharedCredentialAssessmentState::Unknown;
    DatabaseCredentialRotationService service(deps);

    const auto result = service.rotate({10, 20, "example.com"});

    CHECK_FALSE(result.success);
    CHECK(result.code == "shared_user_assessment_unknown");
    CHECK(deps.calls == std::vector<std::string>{
        "inspect_wordpress",
        "verify_old_credential",
        "assess_shared_user",
    });
}

TEST_CASE("DatabaseCredentialRotationService blocks multiple host identities") {
    FakeRotationDependencies deps;
    deps.shared_state = MariaDBSharedCredentialAssessmentState::MultipleHostIdentities;
    DatabaseCredentialRotationService service(deps);

    const auto result = service.rotate({10, 20, "example.com"});

    CHECK_FALSE(result.success);
    CHECK(result.code == "shared_user_assessment_multiple_host_identities");
    CHECK(deps.calls == std::vector<std::string>{
        "inspect_wordpress",
        "verify_old_credential",
        "assess_shared_user",
    });
}

TEST_CASE("DatabaseCredentialRotationService blocks metadata conflict shared assessment") {
    FakeRotationDependencies deps;
    deps.shared_state = MariaDBSharedCredentialAssessmentState::MetadataConflict;
    DatabaseCredentialRotationService service(deps);

    const auto result = service.rotate({10, 20, "example.com"});

    CHECK_FALSE(result.success);
    CHECK(result.code == "shared_user_assessment_metadata_conflict");
    CHECK(deps.calls == std::vector<std::string>{
        "inspect_wordpress",
        "verify_old_credential",
        "assess_shared_user",
    });
}

TEST_CASE("DatabaseCredentialRotationService passes generated password only to dependency calls") {
    FakeRotationDependencies deps;
    deps.generated = "super-secret-generated";
    DatabaseCredentialRotationService service(deps);

    const auto result = service.rotate({10, 20, "example.com"});

    CHECK(result.success);
    CHECK(deps.seen_password == "super-secret-generated");
    for (const auto& event : result.events) {
        CHECK(event.code.find("super-secret-generated") == std::string::npos);
        CHECK(event.message.find("super-secret-generated") == std::string::npos);
    }
    CHECK(result.message.find("super-secret-generated") == std::string::npos);
}

TEST_CASE("DatabaseCredentialRotationService compensates when config update fails after MariaDB mutation") {
    FakeRotationDependencies deps;
    deps.fail_call = "update_wordpress_config";
    DatabaseCredentialRotationService service(deps);

    const auto result = service.rotate({10, 20, "example.com"});

    CHECK_FALSE(result.success);
    CHECK(result.final_state == DatabaseCredentialRotationState::Compensated);
    CHECK(result.code == "rotation_compensated");
    CHECK(deps.calls == std::vector<std::string>{
        "inspect_wordpress",
        "verify_old_credential",
        "assess_shared_user",
        "generate_password",
        "change_mariadb_password",
        "update_wordpress_config",
        "restore_mariadb_password",
        "verify_old_credential",
    });
}

TEST_CASE("DatabaseCredentialRotationService compensates before metadata when verification fails") {
    FakeRotationDependencies deps;
    deps.fail_call = "verify_new_credential";
    DatabaseCredentialRotationService service(deps);

    const auto result = service.rotate({10, 20, "example.com"});

    CHECK_FALSE(result.success);
    CHECK(result.final_state == DatabaseCredentialRotationState::Compensated);
    CHECK(result.code == "rotation_compensated");
    CHECK(deps.calls == std::vector<std::string>{
        "inspect_wordpress",
        "verify_old_credential",
        "assess_shared_user",
        "generate_password",
        "change_mariadb_password",
        "update_wordpress_config",
        "apply_runtime",
        "verify_new_credential",
        "restore_mariadb_password",
        "restore_wordpress_config",
        "restore_runtime",
        "verify_old_credential",
    });
}

TEST_CASE("DatabaseCredentialRotationService requires manual recovery when compensation fails") {
    FakeRotationDependencies deps;
    deps.fail_call = "verify_new_credential";
    deps.compensation_fail_call = "restore_wordpress_config";
    DatabaseCredentialRotationService service(deps);

    const auto result = service.rotate({10, 20, "example.com"});

    CHECK_FALSE(result.success);
    CHECK(result.final_state == DatabaseCredentialRotationState::ManualRecoveryRequired);
    CHECK(result.code == "manual_recovery_required");
    CHECK(result.message.find("new-secret") == std::string::npos);
    for (const auto& event : result.events) {
        CHECK(event.code.find("new-secret") == std::string::npos);
        CHECK(event.message.find("new-secret") == std::string::npos);
    }
    CHECK(deps.calls == std::vector<std::string>{
        "inspect_wordpress",
        "verify_old_credential",
        "assess_shared_user",
        "generate_password",
        "change_mariadb_password",
        "update_wordpress_config",
        "apply_runtime",
        "verify_new_credential",
        "restore_mariadb_password",
        "restore_wordpress_config",
    });
}

TEST_CASE("DatabaseCredentialRotationService requires manual recovery when database restore fails") {
    FakeRotationDependencies deps;
    deps.fail_call = "verify_new_credential";
    deps.compensation_fail_call = "restore_mariadb_password";
    DatabaseCredentialRotationService service(deps);

    const auto result = service.rotate({10, 20, "example.com"});

    CHECK_FALSE(result.success);
    CHECK(result.final_state == DatabaseCredentialRotationState::ManualRecoveryRequired);
    CHECK(result.code == "manual_recovery_required");
    CHECK(deps.calls == std::vector<std::string>{
        "inspect_wordpress",
        "verify_old_credential",
        "assess_shared_user",
        "generate_password",
        "change_mariadb_password",
        "update_wordpress_config",
        "apply_runtime",
        "verify_new_credential",
        "restore_mariadb_password",
    });
}

TEST_CASE("DatabaseCredentialRotationService requires manual recovery when runtime restore fails") {
    FakeRotationDependencies deps;
    deps.fail_call = "verify_new_credential";
    deps.compensation_fail_call = "restore_runtime";
    DatabaseCredentialRotationService service(deps);

    const auto result = service.rotate({10, 20, "example.com"});

    CHECK_FALSE(result.success);
    CHECK(result.final_state == DatabaseCredentialRotationState::ManualRecoveryRequired);
    CHECK(result.code == "manual_recovery_required");
    CHECK(deps.calls == std::vector<std::string>{
        "inspect_wordpress",
        "verify_old_credential",
        "assess_shared_user",
        "generate_password",
        "change_mariadb_password",
        "update_wordpress_config",
        "apply_runtime",
        "verify_new_credential",
        "restore_mariadb_password",
        "restore_wordpress_config",
        "restore_runtime",
    });
}

TEST_CASE("DatabaseCredentialRotationAdapter runs production-shaped happy path without exposing secrets") {
    AdapterFixture fixture("happy");
    auto adapter = fixture.make_adapter();
    DatabaseCredentialRotationService service(adapter);

    const auto result = service.rotate({1, 1, "example.test"});

    CHECK(result.success);
    CHECK(result.final_state == DatabaseCredentialRotationState::Completed);
    CHECK(fixture.runtime_applied);
    CHECK(fixture.health_checked);
    CHECK(fixture.metadata_persisted);
    REQUIRE(fixture.databases.find(1) != nullptr);
    CHECK(fixture.databases.find(1)->db_password == "newpass");
    const auto config = read_test_file(fixture.root / "example.test" / "public" / "wp-config.php");
    CHECK(config.find("define('DB_PASSWORD', 'newpass');") != std::string::npos);
    CHECK(fixture.mariadb_runner.sql_statements.size() == 4);
    CHECK(fixture.wordpress_runner.calls == 1);
    for (const auto& event : result.events) {
        CHECK(event.code.find("oldpass") == std::string::npos);
        CHECK(event.code.find("newpass") == std::string::npos);
        CHECK(event.code.find("rootpass") == std::string::npos);
        CHECK(event.message.find("oldpass") == std::string::npos);
        CHECK(event.message.find("newpass") == std::string::npos);
        CHECK(event.message.find("rootpass") == std::string::npos);
    }
}

TEST_CASE("DatabaseCredentialRotationAdapter fails closed when admin credential source is missing") {
    AdapterFixture fixture("missing_admin");
    fs::remove(fixture.root / "example.test" / ".env");
    auto adapter = fixture.make_adapter();
    DatabaseCredentialRotationService service(adapter);

    const auto result = service.rotate({1, 1, "example.test"});

    CHECK_FALSE(result.success);
    CHECK(result.final_state == DatabaseCredentialRotationState::Failed);
    CHECK(result.code == "credential_source_unavailable");
    CHECK(fixture.mariadb_runner.sql_statements.empty());
    CHECK_FALSE(fixture.runtime_applied);
    CHECK_FALSE(fixture.metadata_persisted);
    CHECK(read_test_file(fixture.root / "example.test" / "public" / "wp-config.php").find("oldpass") != std::string::npos);
}

TEST_CASE("DatabaseCredentialRotationAdapter fails closed on WordPress/database metadata mismatch") {
    AdapterFixture fixture("metadata_mismatch");
    auto* database = fixture.databases.find(1);
    REQUIRE(database != nullptr);
    database->db_user = "different_user";
    auto adapter = fixture.make_adapter();
    DatabaseCredentialRotationService service(adapter);

    const auto result = service.rotate({1, 1, "example.test"});

    CHECK_FALSE(result.success);
    CHECK(result.code == "metadata_conflict");
    CHECK(fixture.mariadb_runner.sql_statements.empty());
    CHECK_FALSE(fixture.runtime_applied);
    CHECK_FALSE(fixture.metadata_persisted);
}

TEST_CASE("DatabaseCredentialRotationAdapter clears pre-mutation context when shared user blocks rotation") {
    AdapterFixture fixture("shared_block");
    fixture.mariadb_runner.schema_grants = 2;
    auto adapter = fixture.make_adapter();
    DatabaseCredentialRotationService service(adapter);

    const DatabaseCredentialRotationRequest request{1, 1, "example.test"};
    const auto result = service.rotate(request);

    CHECK_FALSE(result.success);
    CHECK(result.code == "shared_user_assessment_shared");
    CHECK_FALSE(fixture.runtime_applied);
    CHECK_FALSE(fixture.metadata_persisted);
    const auto stale_context_check = adapter.verify_old_credential(request);
    CHECK_FALSE(stale_context_check.success);
    CHECK(stale_context_check.code == "rotation_context_missing");
}

TEST_CASE("DatabaseCredentialRotationJobService queues pending job without exposing secrets") {
    site::SiteManager sites;
    DatabaseManager databases;
    jobs::JobManager jobs;
    jobs::JobExecutor executor(jobs, 0, 4);
    executor.start();
    DatabaseCredentialRotationService rotation;
    DatabaseCredentialRotationJobService queue(sites, databases, jobs, executor, rotation);

    const uint64_t site_id = sites.create("example.com", "admin", 1);
    const uint64_t database_id = databases.create("wp_example", "wp_user", "stored-secret", 1, site_id);

    const auto result = queue.enqueue({site_id, database_id, "example.com"});

    CHECK(result.success);
    CHECK(result.job_id == 1);
    CHECK(result.message.find("stored-secret") == std::string::npos);
    auto* job = jobs.find(result.job_id);
    REQUIRE(job != nullptr);
    CHECK(job->type == "wordpress-db-credential-rotation");
    CHECK(job->status == "pending");
    CHECK(job->message.find("stored-secret") == std::string::npos);
}

TEST_CASE("DatabaseCredentialRotationJobService rejects confirmation mismatch before job creation") {
    site::SiteManager sites;
    DatabaseManager databases;
    jobs::JobManager jobs;
    jobs::JobExecutor executor(jobs, 0, 4);
    executor.start();
    DatabaseCredentialRotationService rotation;
    DatabaseCredentialRotationJobService queue(sites, databases, jobs, executor, rotation);

    const uint64_t site_id = sites.create("example.com", "admin", 1);
    const uint64_t database_id = databases.create("wp_example", "wp_user", "stored-secret", 1, site_id);

    const auto result = queue.enqueue({site_id, database_id, "wrong.example"});

    CHECK_FALSE(result.success);
    CHECK(result.code == "confirmation_mismatch");
    CHECK(jobs.list().empty());
}

TEST_CASE("DatabaseCredentialRotationJobService rejects database from another site") {
    site::SiteManager sites;
    DatabaseManager databases;
    jobs::JobManager jobs;
    jobs::JobExecutor executor(jobs, 0, 4);
    executor.start();
    DatabaseCredentialRotationService rotation;
    DatabaseCredentialRotationJobService queue(sites, databases, jobs, executor, rotation);

    const uint64_t site_id = sites.create("example.com", "admin", 1);
    const uint64_t other_site_id = sites.create("other.example", "admin", 1);
    const uint64_t database_id = databases.create("wp_other", "wp_user", "stored-secret", 1, other_site_id);

    const auto result = queue.enqueue({site_id, database_id, "example.com"});

    CHECK_FALSE(result.success);
    CHECK(result.code == "database_not_found");
    CHECK(jobs.list().empty());
}

TEST_CASE("DatabaseCredentialRotationJobService resolves site_id zero before capability checks") {
    site::SiteManager sites;
    DatabaseManager databases;
    jobs::JobManager jobs;
    jobs::JobExecutor executor(jobs, 0, 4);
    executor.start();
    DatabaseCredentialRotationService rotation;
    DatabaseCredentialRotationJobService queue(sites, databases, jobs, executor, rotation);

    site::Site system_site;
    system_site.id = 0;
    system_site.name = "ContainerCP Admin";
    system_site.domain = "admin.test";
    system_site.owner = "system";
    system_site.node_id = 0;
    sites.set_sites({system_site});

    const auto result = queue.enqueue({0, 99, "admin.test"});

    CHECK_FALSE(result.success);
    CHECK(result.code == "database_not_found");
    CHECK(jobs.list().empty());
}

TEST_CASE("DatabaseCredentialRotationJobService rejects duplicate queued rotation") {
    site::SiteManager sites;
    DatabaseManager databases;
    jobs::JobManager jobs;
    jobs::JobExecutor executor(jobs, 0, 4);
    executor.start();
    DatabaseCredentialRotationService rotation;
    DatabaseCredentialRotationJobService queue(sites, databases, jobs, executor, rotation);

    const uint64_t site_id = sites.create("example.com", "admin", 1);
    const uint64_t database_id = databases.create("wp_example", "wp_user", "stored-secret", 1, site_id);

    const auto first = queue.enqueue({site_id, database_id, "example.com"});
    const auto second = queue.enqueue({site_id, database_id, "example.com"});

    CHECK(first.success);
    CHECK_FALSE(second.success);
    CHECK(second.code == "rotation_already_running");
    CHECK(jobs.list().size() == 1);
}
