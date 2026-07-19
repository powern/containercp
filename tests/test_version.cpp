#include "api/JsonFormatter.h"
#include "core/Version.h"

#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "doctest/doctest.h"

namespace {

std::string read_command(const std::string& command) {
    std::array<char, 256> buffer{};
    std::string output;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) throw std::runtime_error("popen failed");
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }
    pclose(pipe);
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }
    return output;
}

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    REQUIRE(in.is_open());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

TEST_CASE("Version metadata is consistent for v0.7.0") {
    CHECK(std::string(containercp::core::VERSION) == "0.7.0");
    CHECK(std::string(CMAKE_PROJECT_VERSION_TEXT) == "0.7.0");

    std::string cmake = read_file(std::string(TEST_SOURCE_DIR) + "/CMakeLists.txt");
    CHECK(cmake.find("project(ContainerCP VERSION 0.7.0 LANGUAGES CXX)") != std::string::npos);

    std::string version_header = read_file(std::string(TEST_SOURCE_DIR) + "/libs/core/Version.h");
    CHECK(version_header.find("VERSION = \"0.7.0\"") != std::string::npos);
}

TEST_CASE("Version output uses the authoritative version") {
    CHECK(read_command(std::string(CONTAINERCP_CLI_BINARY) + " --version") == "containercp 0.7.0");
    CHECK(read_command(std::string(CONTAINERCP_DAEMON_BINARY) + " --version") == "containercpd 0.7.0");

    auto json = containercp::api::JsonFormatter::version(containercp::core::VERSION);
    CHECK(json.find("\"version\":\"0.7.0\"") != std::string::npos);
}

TEST_CASE("Release history and migration fixture versions remain intact") {
    std::string changelog = read_file(std::string(TEST_SOURCE_DIR) + "/CHANGELOG.md");
    CHECK(changelog.find("## v0.6.0") != std::string::npos);
    CHECK(changelog.find("## v0.6.0-rc1") != std::string::npos);

    std::string migration_tests = read_file(std::string(TEST_SOURCE_DIR) + "/tests/test_migrate_sqlite.cpp");
    CHECK(migration_tests.find("/v0.6.0") != std::string::npos);
    CHECK(migration_tests.find("\"v0.6.0\", \"v0.7.0\"") != std::string::npos);
}
