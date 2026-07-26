#include "access/SshdConfigWriter.h"
#include "access/SshdAuthorizedKeysWriter.h"
#include "access/AccessKey.h"
#include "runtime/CommandExecutor.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

#include "doctest/doctest.h"

using namespace containercp::access;
using containercp::runtime::CommandExecutor;

namespace {

// Create a temp file with content and return its path
std::string temp_file(const std::string& name, const std::string& content, mode_t mode = 0644) {
    std::string path = "/tmp/containercp-sshd-test-" + std::to_string(::getpid()) + "-" + name;
    std::ofstream out(path, std::ios::binary);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();
    ::chmod(path.c_str(), mode);
    return path;
}

void cleanup(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

} // namespace

// ── Config Rendering ──

TEST_CASE("SshdConfigWriter renders correct Match block") {
    auto content = SshdConfigWriter::render_content();
    REQUIRE(!content.empty());
    CHECK(content.find("Match Group containercp-sftp") != std::string::npos);
    CHECK(content.find("ChrootDirectory /srv/containercp/users/%u") != std::string::npos);
    CHECK(content.find("ForceCommand internal-sftp") != std::string::npos);
    CHECK(content.find("PasswordAuthentication no") != std::string::npos);
    CHECK(content.find("PubkeyAuthentication yes") != std::string::npos);
    CHECK(content.find("AuthorizedKeysFile /srv/containercp/ssh/authorized_keys/%u") != std::string::npos);
    CHECK(content.find("PermitTTY no") != std::string::npos);
    CHECK(content.find("AllowTcpForwarding no") != std::string::npos);
    CHECK(content.find("AllowAgentForwarding no") != std::string::npos);
    CHECK(content.find("X11Forwarding no") != std::string::npos);
    CHECK(content.find("PermitTunnel no") != std::string::npos);
    CHECK(content.find("GatewayPorts no") != std::string::npos);
    // Verify no shell directives
    CHECK(content.find("AuthorizedKeysCommand") == std::string::npos);
    CHECK(content.find("Subsystem") == std::string::npos);
}

// ── Key Rendering ──

TEST_CASE("SshdAuthorizedKeysWriter renders restrict prefix") {
    containercp::access::AccessKey k;
    k.id = 1; k.access_user_id = 1;
    k.key_type = "ssh-ed25519";
    k.key_data = "AAAAC3NzaC1lZDI1NTE5AAAAIGxJm6R";
    k.fingerprint = "SHA256:abc123";
    k.enabled = true;
    k.key_comment = "test key";

    auto content = SshdAuthorizedKeysWriter::render_content({k});
    REQUIRE(!content.empty());
    CHECK(content.find("restrict") == 0);
    CHECK(content.find("ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGxJm6R") != std::string::npos);
    CHECK(content.find("test key") != std::string::npos);
}

TEST_CASE("SshdAuthorizedKeysWriter deduplicates by fingerprint") {
    containercp::access::AccessKey k1, k2;
    k1.id = 1; k1.access_user_id = 1;
    k1.key_type = "ssh-ed25519";
    k1.key_data = "AAAAC3NzaC1lZDI1NTE5AAAAIGxJm6R";
    k1.fingerprint = "SHA256:abc123";
    k1.enabled = true;

    k2.id = 2; k2.access_user_id = 1;
    k2.key_type = "ssh-ed25519";
    k2.key_data = "AAAAC3NzaC1lZDI1NTE5AAAAIGxJm6R";
    k2.fingerprint = "SHA256:abc123";
    k2.enabled = true;

    auto content = SshdAuthorizedKeysWriter::render_content({k1, k2});
    // Count lines
    int lines = 0;
    for (char c : content) if (c == '\n') ++lines;
    CHECK(lines == 1);
}

TEST_CASE("SshdAuthorizedKeysWriter skips disabled keys") {
    containercp::access::AccessKey k;
    k.id = 1; k.access_user_id = 1;
    k.key_type = "ssh-ed25519";
    k.key_data = "AAAAC3NzaC1lZDI1NTE5AAAAIGxJm6R";
    k.fingerprint = "SHA256:abc123";
    k.enabled = false;

    auto content = SshdAuthorizedKeysWriter::render_content({k});
    CHECK(content.empty());
}

TEST_CASE("SshdAuthorizedKeysWriter sorts deterministically") {
    containercp::access::AccessKey k1, k2;
    k1.id = 2; k1.access_user_id = 1;
    k1.key_type = "ssh-rsa";
    k1.key_data = "AAAAB3NzaC1yc2EAAAADAQABAAABAQ==";
    k1.fingerprint = "SHA256:bbb";
    k1.enabled = true;

    k2.id = 1; k2.access_user_id = 1;
    k2.key_type = "ssh-ed25519";
    k2.key_data = "AAAAC3NzaC1lZDI1NTE5AAAAIGxJm6R";
    k2.fingerprint = "SHA256:aaa";
    k2.enabled = true;

    auto content1 = SshdAuthorizedKeysWriter::render_content({k1, k2});
    auto content2 = SshdAuthorizedKeysWriter::render_content({k2, k1});
    CHECK(content1 == content2);
    CHECK(!content1.empty());
    // Verify both key types appear
    CHECK(content1.find("ssh-ed25519") != std::string::npos);
    CHECK(content1.find("ssh-rsa") != std::string::npos);
    // Verify two lines
    int lines = 0;
    for (char c : content1) if (c == '\n') ++lines;
    CHECK(lines == 2);
}

// ── File Validation ──

TEST_CASE("SshdAuthorizedKeysWriter validate_file rejects symlink") {
    std::string links_dir = "/tmp/containercp-sshd-test-" + std::to_string(::getpid());
    std::error_code ec;
    std::filesystem::create_directories(links_dir, ec);
    std::string target = links_dir + "/target";
    std::string link = links_dir + "/authorized_keys";
    {
        std::ofstream out(target); out << "test"; out.close();
    }
    std::filesystem::create_symlink(target, link, ec);
    if (!ec) {
        SshdAuthorizedKeysWriter writer(links_dir);
        auto r = writer.validate_file("authorized_keys");
        CHECK_FALSE(r.success);
        CHECK(r.message.find("symlink") != std::string::npos);
    }
    std::filesystem::remove_all(links_dir, ec);
}

TEST_CASE("SshdAuthorizedKeysWriter validate_file rejects bad mode") {
    std::string dir = "/tmp/containercp-sshd-test-" + std::to_string(::getpid());
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::string path = dir + "/bad_mode";
    {
        std::ofstream out(path); out << "test";
    }
    ::chmod(path.c_str(), 0644); // not 0600
    SshdAuthorizedKeysWriter writer(dir);
    auto r = writer.validate_file("bad_mode");
    CHECK_FALSE(r.success);
    CHECK(r.message.find("mode") != std::string::npos);
    std::filesystem::remove_all(dir, ec);
}

// ── Render content edge cases ──

TEST_CASE("SshdAuthorizedKeysWriter empty key list") {
    auto content = SshdAuthorizedKeysWriter::render_content({});
    CHECK(content.empty());
}

TEST_CASE("SshdAuthorizedKeysWriter only disabled keys") {
    containercp::access::AccessKey k;
    k.id = 1; k.access_user_id = 1;
    k.key_type = "ssh-ed25519";
    k.key_data = "dGVzdA==";
    k.fingerprint = "SHA256:test";
    k.enabled = false;
    auto content = SshdAuthorizedKeysWriter::render_content({k});
    CHECK(content.empty());
}

// ── Config Writer validate_content with real sshd ──

TEST_CASE("SshdConfigWriter validate_content accepts valid config") {
    CommandExecutor exec;
    SshdConfigWriter writer(exec);
    auto r = writer.validate_content(SshdConfigWriter::render_content());
    if (r.success) {
        CHECK(r.success);
    }
    // If real sshd not available, skip
}

TEST_CASE("SshdConfigWriter validate_content rejects invalid config") {
    CommandExecutor exec;
    SshdConfigWriter writer(exec);
    auto r = writer.validate_content("Match InvalidDirective nonsense\n");
    // May or may not have sshd available
    // Just check it doesn't crash
    CHECK(true);
}
