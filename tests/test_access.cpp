#include "access/AccessUserManager.h"
#include "access/AccessGrantManager.h"
#include "access/AccessKeyManager.h"
#include "access/SshKeyValidator.h"

#include <cstdint>
#include <string>
#include <vector>

#include "doctest/doctest.h"

TEST_CASE("AccessUserManager create/find/list/remove") {
    containercp::access::AccessUserManager mgr;

    uint64_t id = mgr.create("devuser");
    CHECK(id == 1);

    auto* u = mgr.find("devuser");
    REQUIRE(u != nullptr);
    CHECK(u->username == "devuser");
    CHECK(u->auth_type == "password");
    CHECK(u->enabled);

    CHECK(mgr.list().size() == 1);

    CHECK(mgr.remove(id));
    CHECK(mgr.find("devuser") == nullptr);
    CHECK(mgr.list().empty());
}

TEST_CASE("AccessUserManager enable/disable") {
    containercp::access::AccessUserManager mgr;
    mgr.create("editor");

    auto* u = mgr.find("editor");
    REQUIRE(u != nullptr);
    CHECK(u->enabled);

    u->enabled = false;
    CHECK_FALSE(u->enabled);

    u->enabled = true;
    CHECK(u->enabled);
}

TEST_CASE("AccessGrantManager create/find/list/remove") {
    containercp::access::AccessGrantManager mgr;

    uint64_t gid = mgr.create(1, 100, containercp::access::Permission::READ_WRITE);
    CHECK(gid == 1);

    auto* g = mgr.find(1);
    REQUIRE(g != nullptr);
    CHECK(g->access_user_id == 1);
    CHECK(g->site_id == 100);
    CHECK(g->permission == containercp::access::Permission::READ_WRITE);

    CHECK(mgr.list().size() == 1);

    auto by_user = mgr.find_by_user(1);
    CHECK(by_user.size() == 1);
    CHECK(by_user[0]->site_id == 100);

    CHECK(mgr.remove(gid));
    CHECK(mgr.find(1) == nullptr);
}

TEST_CASE("AccessGrantManager multiple grants") {
    containercp::access::AccessGrantManager mgr;

    mgr.create(1, 100, containercp::access::Permission::READ_ONLY);
    mgr.create(1, 200, containercp::access::Permission::DEPLOY);
    mgr.create(2, 100, containercp::access::Permission::READ_WRITE);

    CHECK(mgr.list().size() == 3);

    auto user1_grants = mgr.find_by_user(1);
    CHECK(user1_grants.size() == 2);

    auto site100_grants = mgr.find_by_site(100);
    CHECK(site100_grants.size() == 2);
}

TEST_CASE("AccessGrantManager permissions") {
    using containercp::access::Permission;
    CHECK(containercp::access::permission_to_string(Permission::READ_ONLY) == "read_only");
    CHECK(containercp::access::permission_to_string(Permission::READ_WRITE) == "read_write");
    CHECK(containercp::access::permission_to_string(Permission::DEPLOY) == "deploy");

    CHECK(containercp::access::permission_from_string("read_only") == Permission::READ_ONLY);
    CHECK(containercp::access::permission_from_string("read_write") == Permission::READ_WRITE);
    CHECK(containercp::access::permission_from_string("deploy") == Permission::DEPLOY);
    CHECK(containercp::access::permission_from_string("unknown") == Permission::READ_ONLY);
}

namespace {

std::string b64enc(const std::string& raw) {
    static const char* kChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((raw.size() + 2) / 3) * 4);
    for (std::size_t i = 0; i < raw.size(); i += 3) {
        unsigned long v = static_cast<unsigned char>(raw[i]) << 16;
        if (i + 1 < raw.size()) v |= static_cast<unsigned char>(raw[i + 1]) << 8;
        if (i + 2 < raw.size()) v |= static_cast<unsigned char>(raw[i + 2]);
        out.push_back(kChars[(v >> 18) & 0x3F]);
        out.push_back(kChars[(v >> 12) & 0x3F]);
        if (i + 1 < raw.size()) out.push_back(kChars[(v >> 6) & 0x3F]); else { out.push_back('='); out.push_back('='); continue; }
        if (i + 2 < raw.size()) out.push_back(kChars[v & 0x3F]); else out.push_back('=');
    }
    return out;
}

void wr32(std::string& s, uint32_t v) {
    s.push_back(static_cast<char>(v >> 24));
    s.push_back(static_cast<char>((v >> 16) & 0xFF));
    s.push_back(static_cast<char>((v >> 8) & 0xFF));
    s.push_back(static_cast<char>(v & 0xFF));
}

void wrstr(std::string& s, const std::string& str) {
    wr32(s, static_cast<uint32_t>(str.size()));
    s += str;
}

std::string make_ed25519_key() {
    std::string blob;
    wrstr(blob, "ssh-ed25519");
    std::string pk(32, '\x42');
    pk[0] = '\x01'; pk[31] = '\xff';
    wrstr(blob, pk);
    return blob;
}

std::string make_rsa_2048_key() {
    std::string blob;
    wrstr(blob, "ssh-rsa");
    std::string e_data(1, '\x03');
    wrstr(blob, e_data);
    std::string n(256, '\x55');
    n[0] = '\x80';
    wrstr(blob, n);
    return blob;
}

} // namespace

/* ===== SSH KEY VALIDATOR ===== */

TEST_CASE("SshKeyValidator accepts valid ed25519 key") {
    std::string blob = make_ed25519_key();
    std::string line = "ssh-ed25519 " + b64enc(blob) + " test@example";
    auto r = containercp::access::SshKeyValidator::validate(line);
    CHECK(r.valid);
    CHECK(r.key_type == "ssh-ed25519");
    CHECK_FALSE(r.key_data.empty());
    CHECK(r.fingerprint.find("SHA256:") == 0);
    CHECK(r.key_comment == "test@example");
}

TEST_CASE("SshKeyValidator accepts valid RSA 2048 key") {
    std::string blob = make_rsa_2048_key();
    std::string line = "ssh-rsa " + b64enc(blob);
    auto r = containercp::access::SshKeyValidator::validate(line);
    CHECK(r.valid);
    CHECK(r.key_type == "ssh-rsa");
}

TEST_CASE("SshKeyValidator rejects RSA under 2048 bits") {
    auto r = containercp::access::SshKeyValidator::validate(
        "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAAAQQCk shortkey");
    CHECK_FALSE(r.valid);
}

TEST_CASE("SshKeyValidator rejects DSA") {
    auto r = containercp::access::SshKeyValidator::validate(
        "ssh-dss AAAAB3NzaC1kc3MAAACBAKtest");
    CHECK_FALSE(r.valid);
    CHECK(r.error.find("unsupported") != std::string::npos);
}

TEST_CASE("SshKeyValidator rejects empty input") {
    auto r = containercp::access::SshKeyValidator::validate("");
    CHECK_FALSE(r.valid);
}

TEST_CASE("SshKeyValidator rejects malformed base64") {
    auto r = containercp::access::SshKeyValidator::validate(
        "ssh-ed25519 !!!not-valid-base64!!!");
    CHECK_FALSE(r.valid);
}

TEST_CASE("SshKeyValidator rejects unknown algorithm") {
    auto r = containercp::access::SshKeyValidator::validate(
        "ssh-fake AAAAC3NzaC1lZDI1NTE5AAAAIGxJm6R");
    CHECK_FALSE(r.valid);
    CHECK(r.error.find("unsupported") != std::string::npos);
}

TEST_CASE("SshKeyValidator fingerprint is deterministic") {
    std::string blob = make_ed25519_key();
    std::string b64 = b64enc(blob);
    auto r1 = containercp::access::SshKeyValidator::validate(
        "ssh-ed25519 " + b64 + " comment-a");
    auto r2 = containercp::access::SshKeyValidator::validate(
        "ssh-ed25519 " + b64 + " comment-b");
    CHECK(r1.valid);
    CHECK(r2.valid);
    CHECK(r1.fingerprint == r2.fingerprint);
}

TEST_CASE("SshKeyValidator base64 decode produces bytes for valid key") {
    std::string blob = make_ed25519_key();
    std::string line = "ssh-ed25519 " + b64enc(blob);
    auto r = containercp::access::SshKeyValidator::validate(line);
    CHECK(r.valid);
    CHECK_FALSE(r.key_data.empty());
}

/* ===== ACCESS KEY MANAGER ===== */

TEST_CASE("AccessKeyManager create/find/list/remove") {
    containercp::access::AccessKeyManager mgr;
    containercp::access::AccessKey key;
    key.access_user_id = 1;
    key.key_type = "ssh-ed25519";
    key.key_data = "AAAAC3NzaC1lZDI1NTE5AAAAIGxJm6R";
    key.fingerprint = "SHA256:abcdef";
    key.key_comment = "test";

    uint64_t id = mgr.create(key);
    CHECK(id == 1);

    auto* found = mgr.find(1);
    REQUIRE(found != nullptr);
    CHECK(found->access_user_id == 1);
    CHECK(found->key_type == "ssh-ed25519");
    CHECK(found->key_comment == "test");
    CHECK(found->enabled);

    CHECK(mgr.list().size() == 1);

    auto by_user = mgr.list_by_user(1);
    CHECK(by_user.size() == 1);

    CHECK(mgr.remove(id));
    CHECK(mgr.find(1) == nullptr);
    CHECK(mgr.list().empty());
}

TEST_CASE("AccessKeyManager duplicate fingerprint rejected for same user") {
    containercp::access::AccessKeyManager mgr;
    containercp::access::AccessKey key;
    key.access_user_id = 1;
    key.key_type = "ssh-ed25519";
    key.key_data = "AAAAC3NzaC1lZDI1NTE5AAAAIGxJm6R";
    key.fingerprint = "SHA256:abcdef";

    CHECK(mgr.create(key) == 1);
    CHECK(mgr.create(key) == 0);
}

TEST_CASE("AccessKeyManager same fingerprint allowed for different user") {
    containercp::access::AccessKeyManager mgr;
    containercp::access::AccessKey k1;
    k1.access_user_id = 1;
    k1.key_type = "ssh-ed25519";
    k1.key_data = "AAAAC3NzaC1lZDI1NTE5AAAAIGxJm6R";
    k1.fingerprint = "SHA256:abcdef";
    CHECK(mgr.create(k1) == 1);

    containercp::access::AccessKey k2;
    k2.access_user_id = 2;
    k2.key_type = "ssh-ed25519";
    k2.key_data = "AAAAC3NzaC1lZDI1NTE5AAAAIGxJm6R";
    k2.fingerprint = "SHA256:abcdef";
    CHECK(mgr.create(k2) == 2);
}

TEST_CASE("AccessKeyManager set_enabled and revoke") {
    containercp::access::AccessKeyManager mgr;
    containercp::access::AccessKey key;
    key.access_user_id = 1;
    key.key_type = "ssh-rsa";
    key.key_data = "AAAAB3NzaC1yc2EAAA";
    key.fingerprint = "SHA256:rsa1";

    mgr.create(key);
    auto* found = mgr.find(1);
    REQUIRE(found != nullptr);
    CHECK(found->enabled);

    CHECK(mgr.set_enabled(1, false));
    CHECK_FALSE(mgr.find(1)->enabled);

    CHECK(mgr.set_enabled(1, true));
    CHECK(mgr.find(1)->enabled);

    CHECK_FALSE(mgr.set_enabled(999, false));
}

TEST_CASE("AccessKeyManager set_keys bulk load") {
    containercp::access::AccessKeyManager mgr;
    std::vector<containercp::access::AccessKey> keys;
    containercp::access::AccessKey k1;
    k1.id = 5; k1.access_user_id = 1; k1.key_type = "ssh-ed25519";
    k1.key_data = "data1"; k1.fingerprint = "SHA256:fp1";
    containercp::access::AccessKey k2;
    k2.id = 10; k2.access_user_id = 2; k2.key_type = "ssh-rsa";
    k2.key_data = "data2"; k2.fingerprint = "SHA256:fp2";
    keys.push_back(k1); keys.push_back(k2);

    mgr.set_keys(keys);
    CHECK(mgr.list().size() == 2);
    CHECK(mgr.find(5) != nullptr);
    CHECK(mgr.find(10) != nullptr);
}
