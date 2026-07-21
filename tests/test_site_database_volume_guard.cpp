#include "doctest/doctest.h"

#include "operations/SiteDatabaseVolumeGuard.h"

#include <sstream>
#include <string>

using namespace containercp;

namespace {

std::string join_args(const std::vector<std::string>& args) {
    std::ostringstream out;
    for (const auto& arg : args) out << arg << '\n';
    return out.str();
}

class FakeDockerRunner : public operations::DockerCommandRunner {
public:
    int volume_inspect_exit = 0;
    int container_inspect_exit = 0;
    int volume_rm_exit = 0;
    std::string volume_inspect_out;
    std::string container_inspect_out;
    mutable std::vector<std::vector<std::string>> commands;

    runtime::CommandResult run(const std::vector<std::string>& args) const override {
        commands.push_back(args);
        const auto joined = join_args(args);
        if (joined.find("volume\ninspect\n") != std::string::npos) {
            return {volume_inspect_exit, volume_inspect_out, volume_inspect_exit == 0 ? "" : "not found"};
        }
        if (joined.find("inspect\nsite-") != std::string::npos) {
            return {container_inspect_exit, container_inspect_out, container_inspect_exit == 0 ? "" : "not found"};
        }
        if (joined.find("volume\nrm\n") != std::string::npos) {
            return {volume_rm_exit, "", volume_rm_exit == 0 ? "" : "in use"};
        }
        return {1, "", "unexpected command"};
    }
};

bool command_contains(const std::vector<std::vector<std::string>>& commands, const std::string& a, const std::string& b) {
    for (const auto& cmd : commands) {
        bool found_a = false;
        bool found_b = false;
        for (const auto& part : cmd) {
            found_a = found_a || part == a;
            found_b = found_b || part == b;
        }
        if (found_a && found_b) return true;
    }
    return false;
}

} // namespace

TEST_CASE("Site database volume naming matches Docker Compose domain project") {
    CHECK(operations::site_compose_project_name("test-gui-apache.local") == "test-gui-apachelocal");
    CHECK(operations::site_database_volume_name("test-gui-apache.local") == "test-gui-apachelocal_db-data");
}

TEST_CASE("Site removal identifies exact owned database volume by labels") {
    FakeDockerRunner runner;
    runner.volume_inspect_out = "12\ntest-gui-apache.local\ntest-gui-apachelocal\ndb-data";
    const auto plan = operations::inspect_site_database_volume(runner, "test-gui-apache.local", 12);
    CHECK(plan.exists);
    CHECK(plan.removable);
    CHECK(plan.reason == "database_volume_owned_by_labels");
    CHECK(plan.volume_name == "test-gui-apachelocal_db-data");
}

TEST_CASE("Confirmed Site removal deletes exclusive database volume") {
    FakeDockerRunner runner;
    runner.volume_inspect_out = "12\ntest-gui-apache.local\ntest-gui-apachelocal\ndb-data";
    const auto plan = operations::inspect_site_database_volume(runner, "test-gui-apache.local", 12);
    const auto result = operations::remove_site_database_volume(runner, plan);
    CHECK(result.success);
    CHECK(command_contains(runner.commands, "rm", "test-gui-apachelocal_db-data"));
}

TEST_CASE("Site removal refuses database volume with mismatched ownership labels") {
    FakeDockerRunner runner;
    runner.volume_inspect_out = "99\nother.local\ntest-gui-apachelocal\ndb-data";
    const auto plan = operations::inspect_site_database_volume(runner, "test-gui-apache.local", 12);
    CHECK(plan.exists);
    CHECK_FALSE(plan.removable);
    CHECK(plan.reason == "database_volume_ownership_mismatch");
    CHECK_FALSE(operations::remove_site_database_volume(runner, plan).success);
    CHECK_FALSE(command_contains(runner.commands, "rm", "test-gui-apachelocal_db-data"));
}

TEST_CASE("Site removal refuses shared or unknown database volume") {
    FakeDockerRunner runner;
    runner.volume_inspect_out = "<no value>\n<no value>\notherproject\ndb-data";
    const auto plan = operations::inspect_site_database_volume(runner, "test-gui-apache.local", 12);
    CHECK(plan.exists);
    CHECK_FALSE(plan.removable);
    CHECK(plan.reason == "database_volume_not_owned_by_site");
}

TEST_CASE("Site creation fails closed when expected database volume already exists") {
    FakeDockerRunner runner;
    runner.volume_inspect_out = "<no value>\n<no value>\ntest-gui-apachelocal\ndb-data";
    runner.container_inspect_exit = 1;
    const auto result = operations::ensure_database_volume_absent_for_create(runner, "test-gui-apache.local", 12);
    CHECK_FALSE(result.success);
    CHECK(result.message.find("refusing to reuse existing MariaDB data") != std::string::npos);
    CHECK(result.message.find("password") == std::string::npos);
}

TEST_CASE("Recreating same domain cannot inherit stale database volume silently") {
    FakeDockerRunner runner;
    runner.volume_inspect_out = "1\ntest-gui-apache.local\ntest-gui-apachelocal\ndb-data";
    const auto result = operations::ensure_database_volume_absent_for_create(runner, "test-gui-apache.local", 12);
    CHECK_FALSE(result.success);
    CHECK_FALSE(command_contains(runner.commands, "rm", "test-gui-apachelocal_db-data"));
}

TEST_CASE("Legacy unlabeled volume is removable only when mounted by target MariaDB container") {
    FakeDockerRunner runner;
    runner.volume_inspect_out = "<no value>\n<no value>\ntest-gui-apachelocal\ndb-data";
    runner.container_inspect_out = "12\ntest-gui-apachelocal\nmariadb\nvolume|test-gui-apachelocal_db-data|/var/lib/mysql|true\n";
    const auto plan = operations::inspect_site_database_volume(runner, "test-gui-apache.local", 12);
    CHECK(plan.exists);
    CHECK(plan.removable);
    CHECK(plan.legacy_mount_proven);
    CHECK(plan.reason == "database_volume_owned_by_legacy_mount");
}

TEST_CASE("Database volume cleanup failure is visible") {
    FakeDockerRunner runner;
    runner.volume_inspect_out = "12\ntest-gui-apache.local\ntest-gui-apachelocal\ndb-data";
    runner.volume_rm_exit = 1;
    const auto plan = operations::inspect_site_database_volume(runner, "test-gui-apache.local", 12);
    const auto result = operations::remove_site_database_volume(runner, plan);
    CHECK_FALSE(result.success);
    CHECK(result.message.find("Database volume cleanup failed") != std::string::npos);
    CHECK(result.message.find("password") == std::string::npos);
}

TEST_CASE("Database volume guard does not broaden production removal by name alone") {
    FakeDockerRunner runner;
    runner.volume_inspect_out = "<no value>\n<no value>\ntest-gui-apachelocal\ndb-data";
    runner.container_inspect_out = "12\ntest-gui-apachelocal\nphp\nvolume|test-gui-apachelocal_db-data|/var/lib/mysql|true\n";
    const auto plan = operations::inspect_site_database_volume(runner, "test-gui-apache.local", 12);
    CHECK(plan.exists);
    CHECK_FALSE(plan.removable);
    CHECK_FALSE(command_contains(runner.commands, "rm", "test-gui-apachelocal_db-data"));
}
