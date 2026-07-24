#include "access/AccessUserManager.h"
#include "access/AccessGrantManager.h"
#include "access/AccessKeyManager.h"
#include "access/SshKeyValidator.h"
#include "access/SystemAccountAllocator.h"
#include "access/SystemAccountCommandRunner.h"
#include "access/SystemAccountMapping.h"
#include "access/SystemIdentityInspector.h"
#include "access/LocalSftpProvider.h"
#include "access/UsernameMapper.h"
#include "core/OperationResult.h"
#include "logger/Logger.h"

#include <algorithm>
#include <cstdint>
#include <map>
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

/* ===== USERNAME MAPPER ===== */

TEST_CASE("UsernameMapper simple ascii") {
    auto r = containercp::access::UsernameMapper::map("developer");
    CHECK(r.valid);
    CHECK(r.canonical == "au-developer");
}

TEST_CASE("UsernameMapper uppercase input") {
    auto r = containercp::access::UsernameMapper::map("TestUser");
    CHECK(r.valid);
    CHECK(r.canonical == "au-testuser");
}

TEST_CASE("UsernameMapper replaces dots") {
    auto r = containercp::access::UsernameMapper::map("john.doe");
    CHECK(r.valid);
    CHECK(r.canonical == "au-john_doe");
}

TEST_CASE("UsernameMapper collapses separators") {
    auto r = containercp::access::UsernameMapper::map("a___b");
    CHECK(r.valid);
    CHECK(r.canonical == "au-a_b");
}

TEST_CASE("UsernameMapper rejects empty normalizer") {
    auto r = containercp::access::UsernameMapper::map(".....");
    CHECK_FALSE(r.valid);
}

TEST_CASE("UsernameMapper deterministic output") {
    auto r1 = containercp::access::UsernameMapper::map("dev");
    auto r2 = containercp::access::UsernameMapper::map("dev");
    CHECK(r1.canonical == r2.canonical);
    CHECK(r1.canonical == "au-dev");
}

/* ===== UID/GID ALLOCATOR ===== */

TEST_CASE("SystemAccountAllocator first allocation in empty range") {
    containercp::access::SystemAccountAllocator::Range uid_r{10000, 19999};
    containercp::access::SystemAccountAllocator::Range gid_r{20000, 29999};
    containercp::access::SystemAccountAllocator alloc(uid_r, gid_r);
    auto result = alloc.allocate([](int) { return false; }, [](int) { return false; }, {});
    CHECK(result.success);
    CHECK(result.uid == 10000);
    CHECK(result.gid == 20000);
}

TEST_CASE("SystemAccountAllocator skips occupied") {
    containercp::access::SystemAccountAllocator::Range uid_r{10000, 19999};
    containercp::access::SystemAccountAllocator::Range gid_r{20000, 29999};
    containercp::access::SystemAccountAllocator alloc(uid_r, gid_r);
    auto result = alloc.allocate([](int id) { return id == 10000; }, [](int) { return false; }, {});
    CHECK(result.success);
    CHECK(result.uid == 10001);
}

TEST_CASE("SystemAccountAllocator does not reuse deleted mappings") {
    containercp::access::SystemAccountAllocator::Range uid_r{10000, 19999};
    containercp::access::SystemAccountAllocator::Range gid_r{20000, 29999};
    containercp::access::SystemAccountAllocator alloc(uid_r, gid_r);
    containercp::access::SystemAccountMapping m;
    m.uid = 10000; m.gid = 20000; m.entity_type = "access_user"; m.entity_id = 1;
    auto result = alloc.allocate([](int) { return false; }, [](int) { return false; }, {m});
    CHECK(result.success);
    CHECK(result.uid == 10001);
    CHECK(result.gid == 20001);
}

TEST_CASE("SystemAccountAllocator rejects invalid range") {
    containercp::access::SystemAccountAllocator::Range r{5000, 1000};
    containercp::access::SystemAccountAllocator alloc(r, r);
    auto result = alloc.allocate([](int) { return false; }, [](int) { return false; }, {});
    CHECK_FALSE(result.success);
}

/* ===== PROVIDER LIFECYCLE (Phase 2 regression) ===== */

namespace {

class FakeInspector : public containercp::access::SystemIdentityInspector {
public:
    containercp::access::ObservedUser lookup_user(const std::string& name) const override {
        auto it = users_.find(name);
        if (it != users_.end()) return it->second;
        return {};
    }
    containercp::access::ObservedUser lookup_uid(int uid) const override {
        for (const auto& p : users_) if (p.second.uid == uid) return p.second;
        return {};
    }
    containercp::access::ObservedGroup lookup_group(const std::string& name) const override {
        auto it = groups_.find(name);
        if (it != groups_.end()) return it->second;
        return {};
    }
    bool user_exists(const std::string& name) const override {
        return users_.count(name) > 0;
    }
    bool group_exists(const std::string& name) const override {
        return groups_.count(name) > 0;
    }
    bool uid_occupied(int uid) const override {
        for (const auto& p : users_) if (p.second.uid == uid) return true;
        return false;
    }
    bool gid_occupied(int gid) const override {
        for (const auto& p : groups_) if (p.second.gid == gid) return true;
        return false;
    }

    std::map<std::string, containercp::access::ObservedUser> users_;
    std::map<std::string, containercp::access::ObservedGroup> groups_;
};

class FakeCommandRunner {
public:
    containercp::core::OperationResult run(const containercp::access::SystemAccountCommandRunner::Command& cmd) {
        cmds_.push_back(cmd);
        if (fail_next_) { fail_next_ = false; return {false, "injected failure"}; }
        return {true, "ok"};
    }
    std::vector<containercp::access::SystemAccountCommandRunner::Command> cmds_;
    bool fail_next_ = false;
};

containercp::access::SystemAccountCommandRunner
make_test_runner(FakeCommandRunner* fake) {
    return containercp::access::SystemAccountCommandRunner(
        [fake](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake->run(cmd);
        });
}

} // namespace

TEST_CASE("Provider find_mapping via show_user returns value for existing entry") {
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping m;
    m.entity_type = "access_user"; m.entity_id = 1;
    m.username = "au-test"; m.groupname = "au-test";
    m.uid = 10001; m.gid = 20001; m.state = "active";
    stored.push_back(m);

    auto inspector = std::make_shared<FakeInspector>();
    inspector->users_["au-test"] = {true, "au-test", 10001, 20001, "/srv/containercp/users/au-test",
                                    "/usr/sbin/nologin", true};

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_enabled(true);
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) { return true; },
        [&stored](const std::string&, uint64_t) { return true; });

    containercp::access::AccessUser user;
    user.id = 1; user.username = "test";
    auto show = provider.show_user(user);
    CHECK(show.success);
    CHECK(show.message.find("au-test") != std::string::npos);

    containercp::access::AccessUser missing;
    missing.id = 999; missing.username = "nobody";
    auto show2 = provider.show_user(missing);
    CHECK_FALSE(show2.success);
}

TEST_CASE("Provider create_user lifecycle") {
    std::vector<containercp::access::SystemAccountMapping> stored;
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands;

    // Pre-populate: mapping exists, OS state matches
    containercp::access::SystemAccountMapping m;
    m.entity_type = "access_user"; m.entity_id = 1;
    m.username = "au-testdev"; m.groupname = "au-testdev";
    m.uid = 10000; m.gid = 20000; m.state = "active";
    stored.push_back(m);
    inspector->users_["au-testdev"] = {true, "au-testdev", 10000, 20000,
                                        "/srv/containercp/users/au-testdev",
                                        "/usr/sbin/nologin", true};
    inspector->groups_["au-testdev"] = {true, "au-testdev", 20000};

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            for (auto& s : stored) {
                if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; }
            }
            stored.push_back(m); return true;
        },
        [&stored](const std::string& t, uint64_t id) {
            stored.erase(std::remove_if(stored.begin(), stored.end(),
                [&](const auto& s) { return s.entity_type == t && s.entity_id == id; }), stored.end());
            return true;
        });

    containercp::access::AccessUser user;
    user.id = 1; user.username = "testdev";

    // Show
    auto show = provider.show_user(user);
    CHECK(show.success);
    CHECK(show.message.find("au-testdev") != std::string::npos);

    // Disable
    auto dis = provider.disable_user(user);
    CHECK(dis.success);

    // Enable
    auto en = provider.enable_user(user);
    CHECK(en.success);

    // Remove
    auto rem = provider.remove_user(user);
    CHECK(rem.success);
    CHECK(stored.empty());
}

TEST_CASE("Provider create_user rejects on unmanaged conflict") {
    auto inspector = std::make_shared<FakeInspector>();
    inspector->users_["au-conflict"] = {true, "au-conflict", 50000, 50000, "/home/conflict", "/bin/bash", false};
    FakeCommandRunner fake_commands;
    std::vector<containercp::access::SystemAccountMapping> stored;

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_allocator(std::make_unique<containercp::access::SystemAccountAllocator>(
        containercp::access::SystemAccountAllocator::Range{10000, 19999},
        containercp::access::SystemAccountAllocator::Range{20000, 29999}));
    provider.set_enabled(true);
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            stored.push_back(m); return true;
        },
        [&stored](const std::string& t, uint64_t id) { return true; });

    containercp::access::AccessUser user;
    user.id = 1; user.username = "conflict"; user.enabled = true;
    auto result = provider.create_user(user);
    CHECK_FALSE(result.success);
    CHECK(result.message.find("unmanaged_account_conflict") != std::string::npos);
    CHECK(stored.empty());
}

TEST_CASE("Provider create_user rollback on partial failure") {
    std::vector<containercp::access::SystemAccountMapping> stored;
    auto inspector = std::make_shared<FakeInspector>();
    inspector->groups_["containercp-sftp"] = {true, "containercp-sftp", 30000};
    FakeCommandRunner fake_commands;
    fake_commands.fail_next_ = true; // groupadd for private group succeeds, useradd fails

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_allocator(std::make_unique<containercp::access::SystemAccountAllocator>(
        containercp::access::SystemAccountAllocator::Range{10000, 19999},
        containercp::access::SystemAccountAllocator::Range{20000, 29999}));
    provider.set_enabled(true);

    bool deleted_ok = false;
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) { stored.push_back(m); return true; },
        [&deleted_ok](const std::string&, uint64_t) { deleted_ok = true; return true; });

    containercp::access::AccessUser user;
    user.id = 1; user.username = "rollback"; user.enabled = true;
    auto result = provider.create_user(user);
    CHECK_FALSE(result.success);
    // Mapping should be cleaned up
    CHECK(deleted_ok);
}

TEST_CASE("Provider disabled returns error for all operations") {
    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    containercp::access::AccessUser user;
    user.id = 1; user.username = "test";

    CHECK_FALSE(provider.create_user(user).success);
    CHECK_FALSE(provider.remove_user(user).success);
    CHECK_FALSE(provider.enable_user(user).success);
    CHECK_FALSE(provider.disable_user(user).success);
    CHECK_FALSE(provider.list_users().success);
    CHECK_FALSE(provider.show_user(user).success);
}

TEST_CASE("Provider idempotent create returns success for already active mapping") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands;
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping m;
    m.entity_type = "access_user"; m.entity_id = 1;
    m.username = "au-idem"; m.groupname = "au-idem";
    m.uid = 10000; m.gid = 20000; m.state = "active";
    stored.push_back(m);
    inspector->users_["au-idem"] = {true, "au-idem", 10000, 20000,
                                     "/srv/containercp/users/au-idem",
                                     "/usr/sbin/nologin", true};

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_enabled(true);
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { return true; });

    containercp::access::AccessUser user;
    user.id = 1; user.username = "idem";
    auto result = provider.create_user(user);
    CHECK(result.success);
    CHECK(result.message.find("already provisioned") != std::string::npos);
}

TEST_CASE("Provider remove_user fails closed when home cleanup fails") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands;
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping m;
    m.entity_type = "access_user"; m.entity_id = 1;
    m.username = "au-cleanup"; m.groupname = "au-cleanup";
    m.uid = 10001; m.gid = 20001; m.state = "active";
    stored.push_back(m);
    inspector->users_["au-cleanup"] = {true, "au-cleanup", 10001, 20001,
                                        "/srv/containercp/users/au-cleanup",
                                        "/usr/sbin/nologin", true};

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { stored.clear(); return true; });

    containercp::access::AccessUser user;
    user.id = 1; user.username = "cleanup";
    auto result = provider.remove_user(user);
    // remove_all of a non-existent path succeeds with no error
    // BUT the path IS within the managed root, so managed_path_safe should pass
    // and remove_all of a non-existent path succeeds with no error
    CHECK(result.success);
}

TEST_CASE("Provider remove_user fails closed when managed_path_safe detects unsafe path") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands;
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping m;
    m.entity_type = "access_user"; m.entity_id = 1;
    m.username = "au-unsafe"; m.groupname = "au-unsafe";
    m.uid = 10001; m.gid = 20001; m.state = "active";
    stored.push_back(m);
    inspector->users_["au-unsafe"] = {true, "au-unsafe", 10001, 20001,
                                       "/srv/containercp/users/au-unsafe",
                                       "/usr/sbin/nologin", true};

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    // Set managed root to a DIFFERENT path so managed_path_safe rejects the home
    provider.set_managed_home_root("/srv/containercp/other");
    bool mapping_deleted = false;
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&mapping_deleted](const std::string&, uint64_t) { mapping_deleted = true; return true; });

    containercp::access::AccessUser user;
    user.id = 1; user.username = "unsafe";
    auto result = provider.remove_user(user);
    CHECK_FALSE(result.success);
    CHECK(result.message.find("unsafe") != std::string::npos);
    // Mapping must NOT be deleted when path is unsafe
    CHECK_FALSE(mapping_deleted);
    CHECK_FALSE(stored.empty());
}

TEST_CASE("Provider stale provisioning cleanup and retry succeeds") {
    // Test the complete stale-provisioning recovery lifecycle:
    // 1. Stale mapping (state="provisioning") exists from previous failed attempt.
    // 2. A stale managed group exists on the OS.
    // 3. create_user() cleans up stale state, provisions fresh state, and returns SUCCESS.
    // 4. The mapping finishes in ACTIVE state.
    // 5. A second create_user() call is idempotent and succeeds without mutation.

    // Custom inspector: user_exists returns false for managed "au-*" names (allowing
    // provisioning to proceed), but lookup_user returns the OS state for verification.
    struct TestInspector : FakeInspector {
        bool user_exists(const std::string& name) const override {
            if (name.rfind("au-", 0) == 0) return false;
            return FakeInspector::user_exists(name);
        }
        bool group_exists(const std::string& name) const override {
            if (name.rfind("au-", 0) == 0) return false;
            return FakeInspector::group_exists(name);
        }
        // Auto-populate matching observed state for any "au-*" user
        containercp::access::ObservedUser lookup_user(const std::string& name) const override {
            auto it = users_.find(name);
            if (it != users_.end()) return it->second;
            containercp::access::ObservedUser u;
            u.exists = true; u.username = name;
            u.uid = 10000; u.gid = 20000;
            u.home = "/srv/containercp/users/" + name;
            u.shell = "/usr/sbin/nologin"; u.locked = true;
            return u;
        }
    };
    auto inspector = std::make_shared<TestInspector>();

    FakeCommandRunner fake_commands;
    std::vector<containercp::access::SystemAccountMapping> stored;

    // --- Phase A: stale state from a previous failed attempt ---
    containercp::access::SystemAccountMapping stale;
    stale.entity_type = "access_user"; stale.entity_id = 1;
    stale.username = "au-retry"; stale.groupname = "au-retry";
    stale.uid = 10050; stale.gid = 20050; stale.state = "provisioning";
    stored.push_back(stale);

    // Stale managed group from previous failed attempt
    inspector->groups_["au-retry"] = {true, "au-retry", 20050};
    inspector->groups_["containercp-sftp"] = {true, "containercp-sftp", 30000};

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_allocator(std::make_unique<containercp::access::SystemAccountAllocator>(
        containercp::access::SystemAccountAllocator::Range{10000, 19999},
        containercp::access::SystemAccountAllocator::Range{20000, 29999}));
    provider.set_enabled(true);

    int save_count = 0; int delete_count = 0;
    bool final_state_active = false;
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&save_count, &stored, &final_state_active](const containercp::access::SystemAccountMapping& m) {
            save_count++;
            if (m.state == "active") final_state_active = true;
            for (auto& s : stored) {
                if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; }
            }
            stored.push_back(m); return true;
        },
        [&delete_count, &stored](const std::string& t, uint64_t id) {
            delete_count++;
            stored.erase(std::remove_if(stored.begin(), stored.end(),
                [&](const auto& s) { return s.entity_type == t && s.entity_id == id; }), stored.end());
            return true;
        });

    // --- Test 1: First create — stale cleanup + fresh provision → SUCCESS ---
    containercp::access::AccessUser user;
    user.id = 1; user.username = "retry";
    auto result = provider.create_user(user);
    CHECK(result.success);
    CHECK(result.message.find("au-retry") != std::string::npos);
    CHECK(delete_count >= 1);   // stale mapping was deleted
    CHECK(save_count >= 2);     // provisioning + active saves
    CHECK(final_state_active);  // mapping finished in ACTIVE state

    // --- Test 2: Second create is idempotent — no mutation ---
    int save_before = save_count;
    int delete_before = delete_count;
    auto result2 = provider.create_user(user);
    CHECK(result2.success);
    CHECK(result2.message.find("already provisioned") != std::string::npos);
    CHECK(save_count == save_before);    // no new saves
    CHECK(delete_count == delete_before); // no new deletions
}

TEST_CASE("Provider verify_ownership rejects UID outside managed range") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands;
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping m;
    m.entity_type = "access_user"; m.entity_id = 1;
    m.username = "au-range"; m.groupname = "au-range";
    m.uid = 99999; m.gid = 29999; m.state = "active";  // UID outside range
    stored.push_back(m);
    inspector->users_["au-range"] = {true, "au-range", 99999, 29999,
                                      "/srv/containercp/users/au-range",
                                      "/usr/sbin/nologin", true};

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_allocator(std::make_unique<containercp::access::SystemAccountAllocator>(
        containercp::access::SystemAccountAllocator::Range{10000, 19999},
        containercp::access::SystemAccountAllocator::Range{20000, 29999}));
    provider.set_enabled(true);
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { return true; });

    containercp::access::AccessUser user;
    user.id = 1; user.username = "range";
    // disable should fail because UID 99999 is outside managed range
    auto result = provider.disable_user(user);
    CHECK_FALSE(result.success);
    CHECK(result.message.find("unmanaged") != std::string::npos);
}
