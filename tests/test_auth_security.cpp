#include "auth/SessionManager.h"
#include "security/PasswordHasher.h"
#include "security/SecureRandom.h"

#include <chrono>
#include <fstream>
#include <string>
#include <unordered_set>

#include "doctest/doctest.h"

TEST_CASE("PasswordHasher hashes and verifies using only supported secure format") {
    using containercp::security::PasswordHasher;

    std::string hash = PasswordHasher::hash("correct horse battery staple");
    REQUIRE(!hash.empty());
    CHECK(PasswordHasher::is_supported_hash(hash));
    CHECK(PasswordHasher::verify("correct horse battery staple", hash));
    CHECK_FALSE(PasswordHasher::verify("wrong password", hash));
    CHECK_FALSE(PasswordHasher::verify("correct horse battery staple",
        "0000000000000000000000000000000000000000000000000000000000000000"));
}

TEST_CASE("PasswordHasher uses random salts and deterministic verification") {
    using containercp::security::PasswordHasher;

    std::string first = PasswordHasher::hash("same password");
    std::string second = PasswordHasher::hash("same password");
    REQUIRE(!first.empty());
    REQUIRE(!second.empty());
    CHECK(first != second);
    CHECK(PasswordHasher::verify("same password", first));
    CHECK(PasswordHasher::verify("same password", first));
    CHECK(PasswordHasher::verify("same password", second));
}

TEST_CASE("SecureRandom returns requested sizes and unique tokens") {
    auto bytes = containercp::security::SecureRandom::bytes(32);
    REQUIRE(bytes.has_value());
    CHECK(bytes->size() == 32);

    auto token = containercp::security::SecureRandom::hex(32);
    REQUIRE(token.has_value());
    CHECK(token->size() == 64);

    std::unordered_set<std::string> seen;
    for (int i = 0; i < 32; ++i) {
        auto t = containercp::security::SecureRandom::hex(16);
        REQUIRE(t.has_value());
        CHECK(t->size() == 32);
        CHECK(seen.insert(*t).second);
    }
}

TEST_CASE("SecureRandom string honors alphabet") {
    const std::string alphabet = "abc123";
    auto value = containercp::security::SecureRandom::string(64, alphabet);
    REQUIRE(value.has_value());
    CHECK(value->size() == 64);
    for (char c : *value) {
        CHECK(alphabet.find(c) != std::string::npos);
    }
    CHECK_FALSE(containercp::security::SecureRandom::string(4, "").has_value());
}

TEST_CASE("SessionManager creates validates revokes and expires sessions") {
    using containercp::auth::SessionManager;
    using ClockPoint = std::chrono::steady_clock::time_point;

    ClockPoint now = std::chrono::steady_clock::now();
    SessionManager sessions(std::chrono::seconds(60));
    sessions.set_clock_for_tests([&] { return now; });

    auto token = sessions.create("admin", "admin");
    REQUIRE(token.has_value());
    CHECK(token->size() == 64);

    auto* session = sessions.validate(*token);
    REQUIRE(session != nullptr);
    CHECK(session->username == "admin");
    CHECK(session->role == "admin");

    now += std::chrono::seconds(61);
    CHECK(sessions.validate(*token) == nullptr);

    now = std::chrono::steady_clock::now();
    auto second = sessions.create("admin", "admin");
    REQUIRE(second.has_value());
    CHECK(sessions.validate(*second) != nullptr);
    CHECK(sessions.revoke(*second));
    CHECK(sessions.validate(*second) == nullptr);
}

TEST_CASE("Authentication source no longer contains legacy SHA-256 or mt19937 paths") {
    std::ifstream auth(std::string(TEST_SOURCE_DIR) + "/libs/auth/AuthService.cpp");
    REQUIRE(auth.is_open());
    std::string auth_source((std::istreambuf_iterator<char>(auth)), std::istreambuf_iterator<char>());
    CHECK(auth_source.find("sha256") == std::string::npos);
    CHECK(auth_source.find("mt19937") == std::string::npos);
    CHECK(auth_source.find("Temporary password:") == std::string::npos);

    std::ifstream password_generator(std::string(TEST_SOURCE_DIR) + "/libs/utils/PasswordGenerator.cpp");
    REQUIRE(password_generator.is_open());
    std::string pg_source((std::istreambuf_iterator<char>(password_generator)), std::istreambuf_iterator<char>());
    CHECK(pg_source.find("mt19937") == std::string::npos);
    CHECK(pg_source.find("SecureRandom") != std::string::npos);
}
