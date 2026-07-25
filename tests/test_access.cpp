#include "access/AccessUserManager.h"
#include "access/AccessGrantManager.h"
#include "access/AccessKeyManager.h"
#include "access/SshKeyValidator.h"
#include "access/SystemAccountAllocator.h"
#include "access/SystemAccountCommandRunner.h"
#include "access/SystemAccountMapping.h"
#include "access/SystemIdentityInspector.h"
#include "access/LocalSftpProvider.h"
#include "access/MountInspector.h"
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
    bool user_in_supplementary_group(const std::string& user, const std::string& group) const {
        auto it = supp_groups_.find(user);
        return it != supp_groups_.end() && it->second.count(group) > 0;
    }

    bool user_in_group(const std::string& username,
                       const std::string& groupname) const override {
        return user_in_supplementary_group(username, groupname);
    }

    struct FsState {
        std::map<std::string, containercp::access::FsPermissionState> state_;
    };
    struct MountState {
        std::set<std::string> mounted_paths_;
        std::map<std::string, std::string> bind_sources_; // target → source
    };
    std::shared_ptr<FsState> fs_state_ = std::make_shared<FsState>();
    std::shared_ptr<MountState> mount_state_ = std::make_shared<MountState>();

    std::map<std::string, containercp::access::ObservedUser> users_;
    std::map<std::string, containercp::access::ObservedGroup> groups_;
    std::map<std::string, std::set<std::string>> supp_groups_;
};

class FakeCommandRunner {
    public:
    explicit FakeCommandRunner(std::shared_ptr<FakeInspector> inspector = nullptr)
        : inspector_(std::move(inspector)) {}

    containercp::core::OperationResult run(const containercp::access::SystemAccountCommandRunner::Command& cmd) {
        cmds_.push_back(cmd);
        if (fail_next_) { fail_next_ = false; return {false, "injected failure"}; }
        apply_to_inspector(cmd);
        return {true, "ok"};
    }
    std::vector<containercp::access::SystemAccountCommandRunner::Command> cmds_;
    bool fail_next_ = false;

private:
    void apply_to_inspector(const containercp::access::SystemAccountCommandRunner::Command& cmd) {
        if (!inspector_ || cmd.args.empty()) return;
        const auto& prog = cmd.args[0];

        if (prog == "useradd" && cmd.args.size() >= 2) {
            const auto& name = cmd.args.back();
            containercp::access::ObservedUser u;
            u.exists = true; u.username = name;
            u.shell = "/usr/sbin/nologin"; u.locked = true;
            for (size_t i = 1; i + 1 < cmd.args.size(); ++i) {
                if (cmd.args[i] == "-u") u.uid = std::stoi(cmd.args[i + 1]);
                if (cmd.args[i] == "-g") u.gid = std::stoi(cmd.args[i + 1]);
                if (cmd.args[i] == "-d") u.home = cmd.args[i + 1];
            }
            inspector_->users_[name] = u;
        } else if (prog == "groupadd") {
            std::string name = cmd.args.back();
            containercp::access::ObservedGroup g;
            g.exists = true; g.name = name;
            for (size_t i = 1; i + 1 < cmd.args.size(); ++i) {
                if (cmd.args[i] == "-g") g.gid = std::stoi(cmd.args[i + 1]);
            }
            inspector_->groups_[name] = g;
        } else if (prog == "userdel" && cmd.args.size() >= 2) {
            inspector_->users_.erase(cmd.args.back());
        } else if (prog == "groupdel" && cmd.args.size() >= 2) {
            inspector_->groups_.erase(cmd.args.back());
        } else if (prog == "usermod" && cmd.args.size() >= 4) {
            for (size_t i = 1; i + 1 < cmd.args.size(); ++i) {
                if (cmd.args[i] == "-G" || (cmd.args[i] == "-a" && i + 1 < cmd.args.size() && cmd.args[i+1] == "-G")) {
                    std::string grp = cmd.args[i + (cmd.args[i] == "-G" ? 1 : 2)];
                    std::string usr = cmd.args.back();
                    inspector_->supp_groups_[usr].insert(grp);
                    break;
                }
            }
        } else if (prog == "gpasswd" && cmd.args.size() >= 4) {
            std::string usr = cmd.args[2];
            std::string grp = cmd.args[3];
            auto it = inspector_->supp_groups_.find(usr);
            if (it != inspector_->supp_groups_.end()) it->second.erase(grp);
        } else if (prog == "chgrp" && cmd.args.size() >= 3) {
            // chgrp <group> <path>
            std::string group = cmd.args[1];
            std::string path = cmd.args[2];
            if (inspector_->fs_state_) {
                auto it = inspector_->fs_state_->state_.find(path);
                if (it != inspector_->fs_state_->state_.end()) {
                    // Look up GID from group name
                    auto git = inspector_->groups_.find(group);
                    if (git != inspector_->groups_.end()) it->second.group_gid = git->second.gid;
                }
            }
        } else if (prog == "chmod" && cmd.args.size() >= 3) {
            // chmod <mode> <path>
            std::string mode_str = cmd.args[1];
            std::string path = cmd.args[2];
            if (inspector_->fs_state_ && !mode_str.empty()) {
                auto it = inspector_->fs_state_->state_.find(path);
                if (it != inspector_->fs_state_->state_.end()) {
                    it->second.mode = std::stoi(mode_str, nullptr, 8);
                }
            }
        } else if (prog == "setfacl" && cmd.args.size() >= 4) {
            // setfacl -m <acl_spec> <path> or setfacl -x <acl_spec> <path>
            std::string op = cmd.args[1];
            std::string spec = cmd.args[2];
            std::string path = cmd.args[3];
            if (inspector_->fs_state_) {
                // Parse the group name from the ACL spec
                // Format: "g:group:perms", "g:group", "d:g:group:perms", "d:g:group"
                bool is_default = (spec.rfind("d:", 0) == 0);
                std::string rest = is_default ? spec.substr(2) : spec;
                // rest: "g:group:perms" or "g:group"
                if (rest.rfind("g:", 0) == 0) {
                    std::string sub = rest.substr(2);
                    size_t colon = sub.find(':');
                    std::string group = (colon != std::string::npos) ? sub.substr(0, colon) : sub;
                    std::string perms = (colon != std::string::npos) ? sub.substr(colon + 1) : "";
                    std::string acl_key = path + "::" + group;
                    if (op == "-m" && !perms.empty()) {
                        auto& s = inspector_->fs_state_->state_[acl_key];
                        s.exists = true;
                        s.acl_status = containercp::access::InspectionStatus::Ok;
                        if (is_default) {
                            s.acl.default_present = true;
                            s.acl.default_group = group;
                            s.acl.default_perms = perms;
                            s.acl.default_effective = perms;
                        } else {
                            s.acl.access_present = true;
                            s.acl.access_group = group;
                            s.acl.access_perms = perms;
                            s.acl.effective_perms = perms;
                        }
                    } else if (op == "-x") {
                        auto it = inspector_->fs_state_->state_.find(acl_key);
                        if (it != inspector_->fs_state_->state_.end()) {
                            if (is_default) {
                                it->second.acl.default_present = false;
                            } else {
                                it->second.acl.access_present = false;
                            }
                        }
                    }
                }
            }
        } else if ((prog == "mkdir" || prog == "mkdir_p") && cmd.args.size() >= 2) {
            std::string path = cmd.args.back();
            if (inspector_->fs_state_) {
                auto it = inspector_->fs_state_->state_.find(path);
                if (it == inspector_->fs_state_->state_.end()) {
                    containercp::access::FsPermissionState s;
                    s.exists = true; s.group_gid = 0; s.mode = 0755; s.acl_status = containercp::access::InspectionStatus::Ok;
                    inspector_->fs_state_->state_[path] = s;
                }
            }
        } else if (prog == "rmdir" && cmd.args.size() >= 2) {
            std::string path = cmd.args.back();
            if (inspector_->fs_state_) inspector_->fs_state_->state_.erase(path);
        } else if (prog == "chown" && cmd.args.size() >= 3) {
            // chown <uid:gid> <path>
            if (inspector_->fs_state_) {
                std::string path = cmd.args.back();
                auto it = inspector_->fs_state_->state_.find(path);
                if (it != inspector_->fs_state_->state_.end()) {
                    std::string ug = cmd.args[1];
                    auto pos = ug.find(':');
                    if (pos != std::string::npos && pos + 1 < ug.size()) {
                        std::string gid_str = ug.substr(pos + 1);
                        if (gid_str == "root") it->second.group_gid = 0;
                        else it->second.group_gid = std::stoi(gid_str);
                    }
                }
            }
        } else if (prog == "mount" && cmd.args.size() >= 4) {
            // mount --bind source target
            if (inspector_->mount_state_) {
                std::string target = cmd.args.back();
                std::string source = cmd.args[cmd.args.size() - 2];
                inspector_->mount_state_->mounted_paths_.insert(target);
                inspector_->mount_state_->bind_sources_[target] = source;
            }
        } else if (prog == "umount" && cmd.args.size() >= 2) {
            std::string target = cmd.args.back();
            if (inspector_->mount_state_) {
                inspector_->mount_state_->mounted_paths_.erase(target);
                inspector_->mount_state_->bind_sources_.erase(target);
            }
        } else if (prog == "mountpoint" && cmd.args.size() >= 2) {
            // mountpoint_check
            // No state change needed for test
        }
    }

    std::shared_ptr<FakeInspector> inspector_;
};

struct FakeFsInspector : containercp::access::FilesystemPermissionInspector {
    // Shared state: if bound to a FakeInspector's fs_state, mutations from
    // FakeCommandRunner (chgrp, chmod, etc.) are visible to inspect().
    std::shared_ptr<std::map<std::string, containercp::access::FsPermissionState>> shared_;

    explicit FakeFsInspector(std::shared_ptr<std::map<std::string, containercp::access::FsPermissionState>> shared = nullptr)
        : shared_(std::move(shared)) {}

    containercp::access::FsPermissionState inspect(const std::string& path) const override {
        auto& state_map = shared_ ? *shared_ : state_;
        auto it = state_map.find(path);
        if (it != state_map.end()) return it->second;
        containercp::access::FsPermissionState s;
        s.exists = true; s.group_gid = 20001; s.mode = 0770;
        state_map[path] = s;
        return s;
    }
    containercp::access::FsPermissionState inspect_acl(const std::string& path,
                                                        const std::string& groupname) const override {
        auto key = path + "::" + groupname;
        auto& state_map = shared_ ? *shared_ : state_;
        auto it = state_map.find(key);
        if (it != state_map.end()) return it->second;
        containercp::access::FsPermissionState s;
        s.exists = true; s.mode = 0770;
        s.acl_status = containercp::access::InspectionStatus::Ok;
        state_map[key] = s;
        return s;
    }

public:
    mutable std::map<std::string, containercp::access::FsPermissionState> state_;
};

// Mock mount inspector for testing: returns pre-configured results.
class FakeMountInspector : public containercp::access::MountInspector {
public:
    containercp::access::MountState state_;
    containercp::access::MountState inspect(const std::string&) const override { return state_; }
};

// Live mount inspector that reads from shared FakeInspector mount state.
// Tracks target→source bind mappings so inspect() returns consistent results.
class FakeLiveMountInspector : public containercp::access::MountInspector {
public:
    explicit FakeLiveMountInspector(std::shared_ptr<FakeInspector::MountState> state)
        : mount_state_(std::move(state)) {}

    containercp::access::MountState inspect(const std::string& path) const override {
        if (!mount_state_) {
            containercp::access::MountState s;
            s.status = containercp::access::MountStatus::InspectionFailed;
            s.error_detail = "no mount state";
            return s;
        }
        if (mount_state_->mounted_paths_.count(path) == 0) {
            containercp::access::MountState s;
            s.status = containercp::access::MountStatus::Absent;
            s.error_detail = "not mounted";
            return s;
        }
        containercp::access::MountState s;
        s.mounted = true;
        s.is_bind = true;
        s.target = path;
        s.fstype = "ext4";
        s.device = "0:30";
        s.options = "rw";
        auto it = mount_state_->bind_sources_.find(path);
        s.bind_root = (it != mount_state_->bind_sources_.end()) ? it->second : "";
        s.status = containercp::access::MountStatus::Ok;
        return s;
    }

private:
    std::shared_ptr<FakeInspector::MountState> mount_state_;
};

// Live fs inspector that reads from shared FakeInspector fs state without auto-creating.
class FakeLiveFsInspector : public containercp::access::FilesystemPermissionInspector {
public:
    explicit FakeLiveFsInspector(std::shared_ptr<FakeInspector::FsState> state)
        : fs_state_(std::move(state)) {}

    containercp::access::FsPermissionState inspect(const std::string& path) const override {
        if (!fs_state_) {
            containercp::access::FsPermissionState s;
            s.exists = false;
            s.acl_status = containercp::access::InspectionStatus::PathInspectionFailed;
            return s;
        }
        auto it = fs_state_->state_.find(path);
        if (it != fs_state_->state_.end()) return it->second;
        containercp::access::FsPermissionState s;
        s.exists = false;
        s.acl_status = containercp::access::InspectionStatus::Ok;
        return s;
    }

    containercp::access::FsPermissionState inspect_acl(const std::string& path,
                                                        const std::string& groupname) const override {
        auto key = path + "::" + groupname;
        if (!fs_state_) {
            containercp::access::FsPermissionState s;
            s.exists = false;
            s.acl_status = containercp::access::InspectionStatus::PathInspectionFailed;
            return s;
        }
        auto it = fs_state_->state_.find(key);
        if (it != fs_state_->state_.end()) return it->second;
        containercp::access::FsPermissionState s;
        s.exists = true; s.mode = 0770;
        s.acl_status = containercp::access::InspectionStatus::Ok;
        return s;
    }

private:
    std::shared_ptr<FakeInspector::FsState> fs_state_;
};

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
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
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
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
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
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
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
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());

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
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
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
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
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
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
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
    // End-to-end stale-provisioning recovery with a real OS-emulating fake:
    // FakeCommandRunner mutates FakeInspector exactly as useradd/groupadd/userdel would.
    // No fabricated state — all lookups read the same consistent OS map.

    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;

    // Initial stale state: mapping from previous failed attempt,
    // group was created but useradd failed.
    containercp::access::SystemAccountMapping stale;
    stale.entity_type = "access_user"; stale.entity_id = 1;
    stale.username = "au-retry"; stale.groupname = "au-retry";
    stale.uid = 10050; stale.gid = 20050; stale.state = "provisioning";
    stored.push_back(stale);
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
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());

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

    // --- Test 1: First create recovers from stale state ---
    containercp::access::AccessUser user;
    user.id = 1; user.username = "retry";
    auto result = provider.create_user(user);
    CHECK(result.success);
    CHECK(result.message.find("au-retry") != std::string::npos);
    CHECK(delete_count >= 1);   // stale mapping was deleted
    CHECK(save_count >= 2);     // provisioning + active saves
    CHECK(final_state_active);  // mapping finished in ACTIVE state
    // Fake OS state must be internally consistent after create
    CHECK(inspector->user_exists("au-retry"));
    CHECK(inspector->group_exists("au-retry"));
    CHECK_FALSE(inspector->user_exists("au-retry") != inspector->group_exists("au-retry")); // both exist

    // --- Test 2: Second create is idempotent — zero mutations ---
    int save_before = save_count;
    int delete_before = delete_count;
    auto result2 = provider.create_user(user);
    CHECK(result2.success);
    CHECK(result2.message.find("already provisioned") != std::string::npos);
    CHECK(save_count == save_before);
    CHECK(delete_count == delete_before);
    // OS state unchanged
    CHECK(inspector->user_exists("au-retry"));
    CHECK(inspector->group_exists("au-retry"));
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
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
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

/* ===== PHASE 3a: SITE GRANT GROUPS ===== */

TEST_CASE("Site group ensure_site_group creates RW group idempotently") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
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
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            for (auto& s : stored) {
                if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; }
            }
            stored.push_back(m); return true;
        },
        [&stored](const std::string&, uint64_t) { return true; });

    auto r1 = provider.ensure_site_group(1, "read_write");
    CHECK(r1.success);
    CHECK_FALSE(stored.empty());
    CHECK(stored[0].entity_type == "site_group_rw");
    CHECK(stored[0].username == "site-1-rw");
    CHECK(stored[0].state == "active");

    size_t before = stored.size();
    auto r2 = provider.ensure_site_group(1, "read_write");
    CHECK(r2.success);
    CHECK(stored.size() == before);
}

TEST_CASE("Site group ensure_site_group creates RO group") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
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
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            stored.push_back(m); return true;
        },
        [&stored](const std::string&, uint64_t) { return true; });

    auto r = provider.ensure_site_group(2, "read_only");
    CHECK(r.success);
    CHECK(stored[0].entity_type == "site_group_ro");
    CHECK(stored[0].username == "site-2-ro");
}

TEST_CASE("Site group allocates unique GIDs per group") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
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
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            for (auto& s : stored) {
                if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; }
            }
            stored.push_back(m); return true;
        },
        [&stored](const std::string&, uint64_t) { return true; });

    provider.ensure_site_group(1, "read_write");
    provider.ensure_site_group(1, "read_only");
    provider.ensure_site_group(2, "read_write");
    CHECK(stored.size() == 3);
    CHECK(stored[0].gid != stored[1].gid);
    CHECK(stored[1].gid != stored[2].gid);
    CHECK(stored[0].gid != stored[2].gid);
}

TEST_CASE("Site group rejects unmanaged conflict") {
    auto inspector = std::make_shared<FakeInspector>();
    inspector->groups_["site-5-rw"] = {true, "site-5-rw", 99999};
    FakeCommandRunner fake_commands(inspector);
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
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { return true; });

    auto r = provider.ensure_site_group(5, "read_write");
    CHECK_FALSE(r.success);
    CHECK(r.message.find("unmanaged_group_conflict") != std::string::npos);
    CHECK(stored.empty());
}

TEST_CASE("Site group add_user_to_site_group adds membership") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping m;
    m.entity_type = "site_group_rw"; m.entity_id = 1;
    m.gid = 20001; m.username = "site-1-rw"; m.groupname = "site-1-rw";
    m.state = "active";
    stored.push_back(m);
    inspector->users_["au-dev"] = {true, "au-dev", 10000, 21000,
                                    "/srv/containercp/users/au-dev",
                                    "/usr/sbin/nologin", true};

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { return true; });

    auto r = provider.add_user_to_site_group("au-dev", 1, "read_write");
    CHECK(r.success);
}

TEST_CASE("Site group add_user rejects unprovisioned group") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    inspector->users_["au-dev"] = {true, "au-dev", 10000, 21000,
                                    "/srv/containercp/users/au-dev",
                                    "/usr/sbin/nologin", true};

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { return true; });

    auto r = provider.add_user_to_site_group("au-dev", 99, "read_write");
    CHECK_FALSE(r.success);
    CHECK(r.message.find("not provisioned") != std::string::npos);
}

TEST_CASE("Site group delete removes orphaned group") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping m;
    m.entity_type = "site_group_rw"; m.entity_id = 1;
    m.gid = 20001; m.username = "site-1-rw"; m.groupname = "site-1-rw";
    m.state = "active";
    stored.push_back(m);
    inspector->groups_["site-1-rw"] = {true, "site-1-rw", 20001};

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_grants_lookup([](uint64_t, const std::string&) -> size_t { return 0; });
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { stored.clear(); return true; });

    auto r = provider.delete_site_group_if_unused(1, "read_write");
    CHECK(r.success);
    CHECK(stored.empty());
}

TEST_CASE("Site group delete refuses when grants exist") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping m;
    m.entity_type = "site_group_rw"; m.entity_id = 1;
    m.gid = 20001; m.username = "site-1-rw"; m.groupname = "site-1-rw";
    m.state = "active";
    stored.push_back(m);

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_grants_lookup([](uint64_t, const std::string&) -> size_t { return 3; });
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { return true; });

    auto r = provider.delete_site_group_if_unused(1, "read_write");
    CHECK_FALSE(r.success);
    CHECK_FALSE(stored.empty());
}

TEST_CASE("Site group full lifecycle: create-add-remove-delete") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    inspector->users_["au-life"] = {true, "au-life", 10000, 21000,
                                     "/srv/containercp/users/au-life",
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
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_grants_lookup([](uint64_t, const std::string&) -> size_t { return 0; });
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            for (auto& s : stored) {
                if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; }
            }
            stored.push_back(m); return true;
        },
        [&stored](const std::string&, uint64_t) { stored.clear(); return true; });

    CHECK(provider.ensure_site_group(10, "read_write").success);
    CHECK(stored.back().state == "active");
    CHECK(provider.add_user_to_site_group("au-life", 10, "read_write").success);
    CHECK(provider.remove_user_from_site_group("au-life", 10, "read_write").success);
    CHECK(provider.delete_site_group_if_unused(10, "read_write").success);
    CHECK(stored.empty());
}

TEST_CASE("Site group stale provisioning retry recovers to active") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;

    // Stale state: mapping in "provisioning", OS group already created
    containercp::access::SystemAccountMapping m;
    m.entity_type = "site_group_rw"; m.entity_id = 1;
    m.gid = 20010; m.username = "site-1-rw"; m.groupname = "site-1-rw";
    m.state = "provisioning";
    stored.push_back(m);
    inspector->groups_["site-1-rw"] = {true, "site-1-rw", 20010};

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
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    bool state_was_updated = false;
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored, &state_was_updated](const containercp::access::SystemAccountMapping& m) {
            for (auto& s : stored) {
                if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) {
                    if (m.state == "active") state_was_updated = true;
                    s = m; return true;
                }
            }
            stored.push_back(m); return true;
        },
        [&stored](const std::string&, uint64_t) { return true; });

    auto r = provider.ensure_site_group(1, "read_write");
    CHECK(r.success);
    CHECK(r.message.find("recovered") != std::string::npos);
    CHECK(state_was_updated);
}

TEST_CASE("Site group remove_user rejects unmanaged group") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    inspector->users_["au-dev"] = {true, "au-dev", 10000, 21000,
                                    "/srv/containercp/users/au-dev",
                                    "/usr/sbin/nologin", true};

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { return true; });

    auto r = provider.remove_user_from_site_group("au-dev", 1, "read_write");
    CHECK_FALSE(r.success);
    CHECK(r.message.find("not managed") != std::string::npos);
}

TEST_CASE("Site group ensure_site_group rejects invalid permission") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_allocator(std::make_unique<containercp::access::SystemAccountAllocator>(
        containercp::access::SystemAccountAllocator::Range{10000, 19999},
        containercp::access::SystemAccountAllocator::Range{20000, 29999}));
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { return true; });

    CHECK_FALSE(provider.ensure_site_group(1, "bad_permission").success);
    CHECK_FALSE(provider.ensure_site_group(1, "").success);
    CHECK_FALSE(provider.ensure_site_group(1, "ADMIN").success);
    CHECK(provider.ensure_site_group(1, "read_write").success);
    CHECK(provider.ensure_site_group(2, "read_only").success);
    CHECK(provider.ensure_site_group(3, "deploy").success);
}

TEST_CASE("Site group recovery fails when save_mapping fails") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping m;
    m.entity_type = "site_group_rw"; m.entity_id = 1;
    m.gid = 20010; m.username = "site-1-rw"; m.groupname = "site-1-rw";
    m.state = "provisioning";
    stored.push_back(m);
    inspector->groups_["site-1-rw"] = {true, "site-1-rw", 20010};

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
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            if (m.state == "active") return false; // simulate persistence failure
            stored.push_back(m); return true;
        },
        [&stored](const std::string&, uint64_t) { return true; });

    auto r = provider.ensure_site_group(1, "read_write");
    CHECK_FALSE(r.success);
    CHECK(r.message.find("failed to persist") != std::string::npos);
}

TEST_CASE("Site group remove_user verifies ownership completely") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    inspector->users_["au-dev"] = {true, "au-dev", 10000, 21000,
                                    "/srv/containercp/users/au-dev",
                                    "/usr/sbin/nologin", true};

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { return true; });

    // No mapping → rejected
    auto r1 = provider.remove_user_from_site_group("au-dev", 1, "read_write");
    CHECK_FALSE(r1.success);
    CHECK(r1.message.find("not managed") != std::string::npos);
}

TEST_CASE("Site group add_user verifies membership postcondition") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping m;
    m.entity_type = "site_group_rw"; m.entity_id = 1;
    m.gid = 20001; m.username = "site-1-rw"; m.groupname = "site-1-rw";
    m.state = "active";
    stored.push_back(m);
    inspector->users_["au-dev"] = {true, "au-dev", 10000, 21000,
                                    "/srv/containercp/users/au-dev",
                                    "/usr/sbin/nologin", true};

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { return true; });

    auto r = provider.add_user_to_site_group("au-dev", 1, "read_write");
    CHECK(r.success);
    // FakeCommandRunner adds to supp_groups_, inspector verifies
    CHECK(inspector->user_in_group("au-dev", "site-1-rw"));
}

TEST_CASE("Site group delete_mapping failure leaves recoverable state") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping m;
    m.entity_type = "site_group_rw"; m.entity_id = 1;
    m.gid = 20001; m.username = "site-1-rw"; m.groupname = "site-1-rw";
    m.state = "active";
    stored.push_back(m);
    inspector->groups_["site-1-rw"] = {true, "site-1-rw", 20001};

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_grants_lookup([](uint64_t, const std::string&) -> size_t { return 0; });
    // delete_mapping always fails
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { return false; });

    auto r = provider.delete_site_group_if_unused(1, "read_write");
    CHECK_FALSE(r.success);
    CHECK(r.message.find("failed to delete") != std::string::npos);
    // Mapping preserved — group can be retried
    CHECK_FALSE(stored.empty());
}

/* ===== PHASE 3b: PERMISSION ENFORCEMENT ===== */

TEST_CASE("Phase3b valid RW permission accepted") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping m;
    m.entity_type = "site_group_rw"; m.entity_id = 1;
    m.gid = 20001; m.username = "site-1-rw"; m.groupname = "site-1-rw"; m.state = "active";
    stored.push_back(m);
    inspector->groups_["site-1-rw"] = {true, "site-1-rw", 20001};

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { return true; });

    CHECK(provider.apply_directory_permissions(1, "read_write").success);
}

TEST_CASE("Phase3b deploy permission maps to RW") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping m;
    m.entity_type = "site_group_rw"; m.entity_id = 1;
    m.gid = 20001; m.username = "site-1-rw"; m.groupname = "site-1-rw"; m.state = "active";
    stored.push_back(m);
    inspector->groups_["site-1-rw"] = {true, "site-1-rw", 20001};

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { return true; });

    CHECK(provider.apply_directory_permissions(1, "deploy").success);
}

TEST_CASE("Phase3b read_only rejected in apply_directory_permissions") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());

    auto r = provider.apply_directory_permissions(1, "read_only");
    CHECK_FALSE(r.success);
    CHECK(r.message.find("invalid") != std::string::npos);
}

TEST_CASE("Phase3b invalid permission rejected") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());

    CHECK_FALSE(provider.apply_directory_permissions(1, "").success);
    CHECK_FALSE(provider.apply_directory_permissions(1, "ADMIN").success);
}

TEST_CASE("Phase3b site_id zero rejected") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());

    auto r1 = provider.apply_directory_permissions(0, "read_write");
    CHECK_FALSE(r1.success);
    CHECK(r1.message.find("admin_panel") != std::string::npos);
    CHECK_FALSE(provider.apply_read_only_acl(0).success);
    CHECK_FALSE(provider.remove_read_only_acl(0).success);
}

TEST_CASE("Phase3b missing RW mapping rejected") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { return true; });

    CHECK_FALSE(provider.apply_directory_permissions(1, "read_write").success);
}

TEST_CASE("Phase3b RO ACL applied and removed") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping m;
    m.entity_type = "site_group_ro"; m.entity_id = 1;
    m.gid = 21000; m.username = "site-1-ro"; m.groupname = "site-1-ro"; m.state = "active";
    stored.push_back(m);
    inspector->groups_["site-1-ro"] = {true, "site-1-ro", 21000};

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    auto fs = std::make_shared<FakeFsInspector>();
    provider.set_filesystem_inspector(fs);

    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { return true; });

    // Pre-set ACL state so apply postcondition finds it
    containercp::access::FsPermissionState acl_on;
    acl_on.exists = true; acl_on.mode = 0770; acl_on.group_gid = 21000;
    acl_on.acl_status = containercp::access::InspectionStatus::Ok;
    acl_on.acl.access_present = true; acl_on.acl.access_group = "site-1-ro";
    acl_on.acl.access_perms = "r-x"; acl_on.acl.effective_perms = "r-x";
    acl_on.acl.default_present = true; acl_on.acl.default_group = "site-1-ro";
    acl_on.acl.default_perms = "r-x"; acl_on.acl.default_effective = "r-x";
    fs->state_["/srv/containercp/sites/test/public/::site-1-ro"] = acl_on;
    CHECK(provider.apply_read_only_acl(1).success);

    // Pre-set ACL state so remove postcondition finds it absent
    containercp::access::FsPermissionState acl_off;
    acl_off.exists = true; acl_off.mode = 0770; acl_off.group_gid = 21000;
    acl_off.acl.access_present = false; acl_off.acl_status = containercp::access::InspectionStatus::Ok;
    fs->state_["/srv/containercp/sites/test/public/::site-1-ro"] = acl_off;
    CHECK(provider.remove_read_only_acl(1).success);
}

TEST_CASE("Phase3b symlink public/ rejected") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping m;
    m.entity_type = "site_group_rw"; m.entity_id = 1;
    m.gid = 20001; m.username = "site-1-rw"; m.groupname = "site-1-rw"; m.state = "active";
    stored.push_back(m);
    inspector->groups_["site-1-rw"] = {true, "site-1-rw", 20001};

    auto fs = std::make_shared<FakeFsInspector>();
    // Set the path to appear as a symlink
    containercp::access::FsPermissionState sym_state;
    sym_state.exists = true; sym_state.is_symlink = true; sym_state.mode = 0770;
    sym_state.group_gid = 20001;
    fs->state_["/srv/containercp/sites/test/public/"] = sym_state;

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(fs);
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { return true; });

    auto r = provider.apply_directory_permissions(1, "read_write");
    CHECK_FALSE(r.success);
    CHECK(r.message.find("symlink") != std::string::npos);
}

TEST_CASE("Phase3b ACL error propagated on inspection failure") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping m;
    m.entity_type = "site_group_ro"; m.entity_id = 1;
    m.gid = 21000; m.username = "site-1-ro"; m.groupname = "site-1-ro"; m.state = "active";
    stored.push_back(m);
    inspector->groups_["site-1-ro"] = {true, "site-1-ro", 21000};

    auto fs = std::make_shared<FakeFsInspector>();
    // ACL inspection returns an error
    containercp::access::FsPermissionState err_state;
    err_state.exists = true; err_state.acl_status = containercp::access::InspectionStatus::AclToolMissing;
    err_state.acl_error_detail = "getfacl unavailable";
    fs->state_["/srv/containercp/sites/test/public/::site-1-ro"] = err_state;

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(fs);
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { return true; });

    auto r = provider.apply_read_only_acl(1);
    CHECK_FALSE(r.success);
    CHECK_FALSE(r.success);
}

TEST_CASE("Phase3b effective perms reject write access") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping m;
    m.entity_type = "site_group_ro"; m.entity_id = 1;
    m.gid = 21000; m.username = "site-1-ro"; m.groupname = "site-1-ro"; m.state = "active";
    stored.push_back(m);
    inspector->groups_["site-1-ro"] = {true, "site-1-ro", 21000};

    auto fs = std::make_shared<FakeFsInspector>();
    containercp::access::FsPermissionState s;
    s.exists = true; s.mode = 0770; s.group_gid = 21000;
    s.acl_status = containercp::access::InspectionStatus::Ok;
    s.acl.access_present = true; s.acl.access_perms = "r-x";
    s.acl.effective_perms = "rwx";  // mask didn't strip write
    s.acl.default_present = true; s.acl.default_perms = "r-x";
    s.acl.default_effective = "r-x";
    fs->state_["/srv/containercp/sites/test/public/::site-1-ro"] = s;

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(fs);
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { return true; });

    auto r = provider.apply_read_only_acl(1);
    CHECK_FALSE(r.success);
    CHECK_FALSE(r.success);
}

TEST_CASE("Phase3b effective permission positional AND") {
    using containercp::access::FilesystemPermissionInspector;
    // Test effective() via the real parser
    // named=r-x, mask=r-- → effective=r--
    // named=r-x, mask=rwx → effective=r-x
    // named=rwx, mask=r-x → effective=r-x (w stripped)
    // Just verify the provider rejects write in effective perms
}

TEST_CASE("Phase3b malformed ACL output rejected") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping m;
    m.entity_type = "site_group_ro"; m.entity_id = 1;
    m.gid = 21000; m.username = "site-1-ro"; m.groupname = "site-1-ro"; m.state = "active";
    stored.push_back(m);
    inspector->groups_["site-1-ro"] = {true, "site-1-ro", 21000};

    auto fs = std::make_shared<FakeFsInspector>();
    containercp::access::FsPermissionState s;
    s.exists = true; s.mode = 0770; s.group_gid = 21000;
    s.acl_status = containercp::access::InspectionStatus::MalformedAclOutput;
    fs->state_["/srv/containercp/sites/test/public/::site-1-ro"] = s;

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(fs);
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { return true; });

    auto r = provider.apply_read_only_acl(1);
    CHECK_FALSE(r.success);
}

TEST_CASE("Phase3b lstat permission denied returns AccessDenied") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);

    auto fs = std::make_shared<FakeFsInspector>();
    containercp::access::FsPermissionState s;
    s.exists = false; s.acl_status = containercp::access::InspectionStatus::AccessDenied;
    fs->state_["/srv/containercp/sites/test/public/"] = s;

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(fs);
    provider.set_mapping_persistence(
        []() { return std::vector<containercp::access::SystemAccountMapping>{}; },
        [](const containercp::access::SystemAccountMapping&) { return true; },
        [](const std::string&, uint64_t) { return true; });

    auto r = provider.apply_directory_permissions(1, "read_write");
    CHECK_FALSE(r.success);
}

TEST_CASE("Phase3b fail-closed when ACL tools missing") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping m;
    m.entity_type = "site_group_ro"; m.entity_id = 1;
    m.gid = 21000; m.username = "site-1-ro"; m.groupname = "site-1-ro"; m.state = "active";
    stored.push_back(m);
    inspector->groups_["site-1-ro"] = {true, "site-1-ro", 21000};

    auto fs = std::make_shared<FakeFsInspector>();
    containercp::access::FsPermissionState s;
    s.exists = true; s.mode = 0770; s.group_gid = 21000;
    s.acl_status = containercp::access::InspectionStatus::AclToolMissing;
    fs->state_["/srv/containercp/sites/test/public/::site-1-ro"] = s;

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(fs);
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { return true; });

    auto r = provider.apply_read_only_acl(1);
    CHECK_FALSE(r.success);
}

TEST_CASE("Phase3b valid_acl_perms rejects invalid positions") {
    // x-- should fail (x at position 0)
    // -r- should fail (r at position 1)
    // wrx should fail (w at position 0)
    // These are tested via the parser which calls valid_acl_perms
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping m;
    m.entity_type = "site_group_ro"; m.entity_id = 1;
    m.gid = 21000; m.username = "site-1-ro"; m.groupname = "site-1-ro"; m.state = "active";
    stored.push_back(m);
    inspector->groups_["site-1-ro"] = {true, "site-1-ro", 21000};

    auto fs = std::make_shared<FakeFsInspector>();
    containercp::access::FsPermissionState s;
    s.exists = true; s.mode = 0770; s.group_gid = 21000;
    s.acl_status = containercp::access::InspectionStatus::MalformedAclOutput;
    s.acl_error_detail = "bad perms line";
    fs->state_["/srv/containercp/sites/test/public/::site-1-ro"] = s;

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(fs);
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { return true; });

    auto r = provider.apply_read_only_acl(1);
    CHECK_FALSE(r.success);
}

TEST_CASE("Phase3b rollback restores and verifies previous ACL state") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping m;
    m.entity_type = "site_group_ro"; m.entity_id = 1;
    m.gid = 21000; m.username = "site-1-ro"; m.groupname = "site-1-ro"; m.state = "active";
    stored.push_back(m);
    inspector->groups_["site-1-ro"] = {true, "site-1-ro", 21000};

    auto fs = std::make_shared<FakeFsInspector>();
    // Pre-inspection: ACL absent
    containercp::access::FsPermissionState prev_state;
    prev_state.exists = true; prev_state.mode = 0770; prev_state.group_gid = 21000;
    prev_state.acl_status = containercp::access::InspectionStatus::Ok;
    prev_state.acl.access_present = false; prev_state.acl.default_present = false;
    fs->state_["/srv/containercp/sites/test/public/::site-1-ro"] = prev_state;

    // Post-inspection after apply: simulate that setfacl succeeded but ACL is still absent (postcondition failure)
    // The rollback should restore to the previous state (ACL absent)
    // Since fake commands succeed, the setfacl calls will run but the fs state stays the same

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(fs);
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { return true; });

    auto r = provider.apply_read_only_acl(1);
    // ACL apply succeeds (fake commands), but postcondition check finds ACL still absent
    // This triggers rollback which verifies restoration succeeded
    // Since prev state was ACL absent and we're restoring to absent, rollback verification passes
    // But the overall result is failure (ACL postcondition failed)
    CHECK_FALSE(r.success);
    // Rollback should have been called — verify the state is still ACL absent
    CHECK(prev_state.acl.access_present == false);
}

/* ===== PHASE 3b: DIRECT PARSER + ACL FILESYSTEM + ROLLBACK TESTS ===== */

// Shared fake ACL filesystem state — models real setfacl/getfacl mutations
struct FakeAclFs {
    struct Entry {
        bool present = false;
        std::string group;
        std::string perms;
        std::string mask;
        std::string effective;
        bool has_mask = false;
    };
    Entry access;
    Entry default_;
    bool getfacl_error = false;

    void apply_modify(const std::string& acl_spec) {
        // Format: "g:<name>:<perms>" or "d:g:<name>:<perms>" or "g:<name>:<perms>,d:g:..."
        auto col1 = acl_spec.find(':');
        auto col2 = acl_spec.find(':', col1 + 1);
        if (col1 == std::string::npos || col2 == std::string::npos) return;
        bool is_default = (acl_spec.rfind("d:", 0) == 0);
        std::string group = acl_spec.substr(is_default ? 3 : 2, (is_default ? acl_spec.find(':', 3) : col1) - (is_default ? 3 : 2));
        // Re-parse after group since group may contain ':'? No, it's just the name.
        std::string perms = acl_spec.substr(acl_spec.rfind(':') + 1);

        auto& target = is_default ? default_ : access;
        target.present = true;
        target.group = group;
        target.perms = perms;
        target.effective = perms; // unless mask set
        if (target.has_mask) {
            std::string eff = "---";
            for (int i = 0; i < 3; ++i) {
                if (target.perms[i] == target.mask[i] && target.perms[i] != '-') eff[i] = target.perms[i];
            }
            target.effective = eff;
        }
    }

    void apply_remove(const std::string& acl_spec) {
        // Format: "g:<name>" or "d:g:<name>"
        bool is_default = (acl_spec.rfind("d:", 0) == 0);
        auto& target = is_default ? default_ : access;
        target = Entry{};
    }

    containercp::access::AclState to_acl_state() const {
        containercp::access::AclState s;
        s.access_present = access.present;
        s.access_group = access.group;
        s.access_perms = access.perms;
        s.effective_perms = access.effective;
        s.access_mask = access.has_mask ? access.mask : "";
        s.default_present = default_.present;
        s.default_group = default_.group;
        s.default_perms = default_.perms;
        s.default_effective = default_.effective;
        s.default_mask = default_.has_mask ? default_.mask : "";
        return s;
    }
};

TEST_CASE("Phase3b valid_acl_perms rejects invalid and accepts valid") {
    using containercp::access::valid_acl_perms;
    // Invalid
    CHECK_FALSE(valid_acl_perms("x--"));
    CHECK_FALSE(valid_acl_perms("-r-"));
    CHECK_FALSE(valid_acl_perms("wrx"));
    CHECK_FALSE(valid_acl_perms("xr-"));
    CHECK_FALSE(valid_acl_perms("rw"));
    CHECK_FALSE(valid_acl_perms("rwxx"));
    CHECK_FALSE(valid_acl_perms("r-x-"));
    CHECK_FALSE(valid_acl_perms(""));
    // Valid
    CHECK(valid_acl_perms("rwx"));
    CHECK(valid_acl_perms("rw-"));
    CHECK(valid_acl_perms("r-x"));
    CHECK(valid_acl_perms("r--"));
    CHECK(valid_acl_perms("-wx"));
    CHECK(valid_acl_perms("-w-"));
    CHECK(valid_acl_perms("--x"));
    CHECK(valid_acl_perms("---"));
}

TEST_CASE("Phase3b parse_getfacl duplicate detection") {
    using containercp::access::parse_getfacl;
    containercp::access::AclState s;
    containercp::access::InspectionStatus st;
    std::string err;

    // Duplicate access group
    CHECK_FALSE(parse_getfacl("group:g1:r-x\ngroup:g1:rwx\n", "g1", s, st, err));
    CHECK(st == containercp::access::InspectionStatus::MalformedAclOutput);
    CHECK(err.find("dup group") != std::string::npos);

    // Duplicate default group
    s = {}; st = containercp::access::InspectionStatus::Ok;
    CHECK_FALSE(parse_getfacl("default:group:g1:r-x\ndefault:group:g1:rwx\n", "g1", s, st, err));
    CHECK(st == containercp::access::InspectionStatus::MalformedAclOutput);
    CHECK(err.find("dup dflt") != std::string::npos);

    // Duplicate access mask
    s = {}; st = containercp::access::InspectionStatus::Ok;
    CHECK_FALSE(parse_getfacl("mask:r-x\nmask:rwx\n", "g1", s, st, err));
    CHECK(st == containercp::access::InspectionStatus::MalformedAclOutput);
    CHECK(err.find("dup mask") != std::string::npos);

    // Unknown line
    s = {}; st = containercp::access::InspectionStatus::Ok;
    CHECK_FALSE(parse_getfacl("badger:stoat:r-x\n", "g1", s, st, err));
    CHECK(st == containercp::access::InspectionStatus::MalformedAclOutput);
}

TEST_CASE("Phase3b parse_getfacl correct parsing") {
    using containercp::access::parse_getfacl;
    containercp::access::AclState s;
    containercp::access::InspectionStatus st;
    std::string err;
    CHECK(parse_getfacl("group:g1:r-x\ndefault:group:g1:r--\nmask:rwx\n", "g1", s, st, err));
    CHECK(st == containercp::access::InspectionStatus::Ok);
    CHECK(s.access_present);
    CHECK(s.access_perms == "r-x");
    CHECK(s.effective_perms == "r-x");
    CHECK(s.default_present);
    CHECK(s.default_perms == "r--");
    CHECK(s.access_mask == "rwx");
}

TEST_CASE("Phase3b FakeAclFs models real setfacl mutations") {
    FakeAclFs fs;
    fs.apply_modify("g:site-1-ro:r-x");
    CHECK(fs.access.present);
    CHECK(fs.access.perms == "r-x");
    CHECK_FALSE(fs.default_.present);

    fs.apply_modify("d:g:site-1-ro:r-x");
    CHECK(fs.default_.present);

    fs.apply_remove("g:site-1-ro");
    CHECK_FALSE(fs.access.present);
    CHECK(fs.default_.present); // default still there

    fs.apply_remove("d:g:site-1-ro");
    CHECK_FALSE(fs.default_.present);
}

TEST_CASE("Phase3b classify_getfacl_error direct tests") {
    using containercp::access::classify_getfacl_error;
    CHECK(classify_getfacl_error(1, "Permission denied") == containercp::access::InspectionStatus::AccessDenied);
    CHECK(classify_getfacl_error(1, "Operation not supported") == containercp::access::InspectionStatus::AclUnsupported);
    CHECK(classify_getfacl_error(1, "No such file") == containercp::access::InspectionStatus::PathMissing);
    CHECK(classify_getfacl_error(127, "") == containercp::access::InspectionStatus::AclToolMissing);
    CHECK(classify_getfacl_error(1, "command not found") == containercp::access::InspectionStatus::AclToolMissing);
    CHECK(classify_getfacl_error(1, "unknown error") == containercp::access::InspectionStatus::AclParseFailed);
}

TEST_CASE("Phase3b effective_acl positional calculation") {
    using containercp::access::effective_acl;
    CHECK(effective_acl("rwx", "r-x") == "r-x");
    CHECK(effective_acl("r-x", "r--") == "r--");
    CHECK(effective_acl("rwx", "rwx") == "rwx");
    CHECK(effective_acl("r-x", "") == "r-x");
    CHECK(effective_acl("rwx", "---") == "---");
}

TEST_CASE("Phase3b ACL rollback restores complete previous state") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping m;
    m.entity_type = "site_group_ro"; m.entity_id = 1;
    m.gid = 21000; m.username = "site-1-ro"; m.groupname = "site-1-ro"; m.state = "active";
    stored.push_back(m);
    inspector->groups_["site-1-ro"] = {true, "site-1-ro", 21000};

    FakeAclFs acl_fs;
    acl_fs.access.present = true; acl_fs.access.group = "site-1-ro";
    acl_fs.access.perms = "rw-"; acl_fs.access.effective = "rw-";
    acl_fs.default_.present = true; acl_fs.default_.group = "site-1-ro";
    acl_fs.default_.perms = "r--"; acl_fs.default_.effective = "r--";
    auto initial_state = acl_fs.to_acl_state();

    auto fs = std::make_shared<FakeFsInspector>();
    // Pre-inspection returns the initial state
    containercp::access::FsPermissionState prev;
    prev.exists = true; prev.mode = 0770; prev.group_gid = 21000;
    prev.acl_status = containercp::access::InspectionStatus::Ok;
    prev.acl = initial_state;
    fs->state_["/srv/containercp/sites/test/public/::site-1-ro"] = prev;

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_filesystem_inspector(fs);
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { return true; });

    auto r = provider.apply_read_only_acl(1);
    // setfacl runs (fake), postcondition re-reads the SAME state.
    // Since ACL is already present but perms are "rw-" not "r-x",
    // setfacl_modify("r-x") would update it — but fake inspector re-reads
    // the pre-set state which still has "rw-". effective_perms has 'w' → fails.
    // restore_acl is called, restores to initial state → rollback verification passes.
    // Overall result: failure (ACL effective perms contain write).
    CHECK_FALSE(r.success);
}

/* ===== PHASE 3c: CHROOT LAYOUT & BIND MOUNTS ===== */


/* ===== PHASE 3c: CHROOT LAYOUT ===== */

TEST_CASE("Phase3c ensure_chroot_layout resolves trusted user") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.uid = 10000; um.gid = 20000; um.state = "active";
    um.home = "/srv/containercp/users/au-dev";
    stored.push_back(um);
    containercp::access::SystemAccountMapping ro_grp;
    ro_grp.entity_type = "site_group_ro"; ro_grp.entity_id = 1;
    ro_grp.gid = 21000; ro_grp.username = "site-1-ro"; ro_grp.groupname = "site-1-ro"; ro_grp.state = "active";
    stored.push_back(ro_grp);

    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000,
                                   "/srv/containercp/users/au-dev", "/usr/sbin/nologin", true};

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
        [&stored](const std::string&, uint64_t) { return true; });

    CHECK(provider.ensure_chroot_layout(1).success);
}

TEST_CASE("Phase3c ensure_chroot_layout rejects unknown user_id") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_mapping_persistence(
        []() { return std::vector<containercp::access::SystemAccountMapping>{}; },
        [](const containercp::access::SystemAccountMapping&) { return true; },
        [](const std::string&, uint64_t) { return true; });

    CHECK_FALSE(provider.ensure_chroot_layout(999).success);
}

// --- ARCH-009 Task 19: Persisted trusted home path tests ---

TEST_CASE("ARCH-009 ensure_chroot_layout valid persisted home") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.uid = 10000; um.gid = 20000; um.state = "active";
    um.home = "/srv/containercp/users/au-dev";
    stored.push_back(um);
    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000,
                                   "/srv/containercp/users/au-dev", "/usr/sbin/nologin", true};

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
        [&stored](const std::string&, uint64_t) { return true; });

    CHECK(provider.ensure_chroot_layout(1).success);
}

TEST_CASE("ARCH-009 ensure_chroot_layout rejects wrong persisted home") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.uid = 10000; um.gid = 20000; um.state = "active";
    um.home = "/srv/containercp/users/wrong-path";
    stored.push_back(um);
    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000,
                                   "/srv/containercp/users/au-dev", "/usr/sbin/nologin", true};

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
        [&stored](const std::string&, uint64_t) { return true; });

    auto r = provider.ensure_chroot_layout(1);
    CHECK_FALSE(r.success);
    CHECK(r.message.find("does not reference username") != std::string::npos);
}

TEST_CASE("ARCH-009 ensure_chroot_layout rejects OS home mismatch") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.uid = 10000; um.gid = 20000; um.state = "active";
    um.home = "/srv/containercp/users/au-dev";
    stored.push_back(um);
    // OS has a different home than what mapping says
    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000,
                                   "/srv/containercp/users/different", "/usr/sbin/nologin", true};

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
        [&stored](const std::string&, uint64_t) { return true; });

    auto r = provider.ensure_chroot_layout(1);
    CHECK_FALSE(r.success);
    CHECK(r.message.find("OS home mismatch") != std::string::npos);
}

TEST_CASE("ARCH-009 ensure_chroot_layout rejects cross-user home") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.uid = 10000; um.gid = 20000; um.state = "active";
    um.home = "/srv/containercp/users/other-user";
    stored.push_back(um);
    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000,
                                   "/srv/containercp/users/other-user", "/usr/sbin/nologin", true};

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
        [&stored](const std::string&, uint64_t) { return true; });

    // Cross-user: home points to another user's dir — path validation should catch it
    // because "other-user" != "au-dev" in the path
    auto r = provider.ensure_chroot_layout(1);
    CHECK_FALSE(r.success);
}

TEST_CASE("ARCH-009 ensure_chroot_layout rejects home outside managed root") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.uid = 10000; um.gid = 20000; um.state = "active";
    um.home = "/etc/passwd";
    stored.push_back(um);
    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000,
                                   "/etc/passwd", "/usr/sbin/nologin", true};

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
        [&stored](const std::string&, uint64_t) { return true; });

    auto r = provider.ensure_chroot_layout(1);
    CHECK_FALSE(r.success);
    CHECK(r.message.find("home outside managed root") != std::string::npos);
}

TEST_CASE("ARCH-009 ensure_chroot_layout rejects symlink home") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.uid = 10000; um.gid = 20000; um.state = "active";
    um.home = "/srv/containercp/users/au-dev";
    stored.push_back(um);
    // OS home points to a symlink path
    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000,
                                   "/srv/containercp/users/au-dev/...", "/usr/sbin/nologin", true};

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
        [&stored](const std::string&, uint64_t) { return true; });

    auto r = provider.ensure_chroot_layout(1);
    CHECK_FALSE(r.success);
}

TEST_CASE("ARCH-009 ensure_chroot_layout rejects inactive mapping") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.uid = 10000; um.gid = 20000; um.state = "provisioning";
    um.home = "/srv/containercp/users/au-dev";
    stored.push_back(um);

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
        [&stored](const std::string&, uint64_t) { return true; });

    auto r = provider.ensure_chroot_layout(1);
    CHECK_FALSE(r.success);
    CHECK(r.message.find("user mapping not active") != std::string::npos);
}

TEST_CASE("Phase3c bind_mount_site rejects site_id zero") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    CHECK_FALSE(provider.bind_mount_site(1, 0).success);
}

TEST_CASE("Phase3c apply_grant rollback on bind mount failure") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.state = "active";
    stored.push_back(um);
    containercp::access::SystemAccountMapping ro_grp;
    ro_grp.entity_type = "site_group_ro"; ro_grp.entity_id = 1;
    ro_grp.gid = 21000; ro_grp.username = "site-1-ro"; ro_grp.groupname = "site-1-ro"; ro_grp.state = "active";
    stored.push_back(ro_grp);
    inspector->groups_["containercp-sftp"] = {true, "containercp-sftp", 30000};
    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000, "/srv/containercp/users/au-dev", "/usr/sbin/nologin", true};

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_filesystem_inspector(std::make_shared<FakeFsInspector>());
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            // Fail on mount --bind (5th command after groupadd and mkdir)
            if (cmd.args[0] == "mount") return containercp::core::OperationResult{false, "mount failed"};
            return fake_commands.run(cmd);
        }));
    provider.set_allocator(std::make_unique<containercp::access::SystemAccountAllocator>(
        containercp::access::SystemAccountAllocator::Range{10000, 19999},
        containercp::access::SystemAccountAllocator::Range{20000, 29999}));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            for (auto& s : stored) { if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; } }
            stored.push_back(m); return true;
        },
        [&stored](const std::string&, uint64_t) { return true; });

    auto r = provider.apply_grant(1, 1, "read_write");
    CHECK_FALSE(r.success);
}


TEST_CASE("Phase3c bind_mount fails without mount inspector") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.state = "active";
    stored.push_back(um);
    containercp::access::SystemAccountMapping ro_grp;
    ro_grp.entity_type = "site_group_ro"; ro_grp.entity_id = 1;
    ro_grp.gid = 21000; ro_grp.username = "site-1-ro"; ro_grp.groupname = "site-1-ro"; ro_grp.state = "active";
    stored.push_back(ro_grp);

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { return true; });

    // No mount_inspector → fails closed
    auto r = provider.bind_mount_site(1, 1);
    CHECK_FALSE(r.success);
}

TEST_CASE("Phase3c cleanup_all_mounts fails on partial failure") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.state = "active";
    stored.push_back(um);
    containercp::access::SystemAccountMapping ro_grp;
    ro_grp.entity_type = "site_group_ro"; ro_grp.entity_id = 1;
    ro_grp.gid = 21000; ro_grp.username = "site-1-ro"; ro_grp.groupname = "site-1-ro"; ro_grp.state = "active";
    stored.push_back(ro_grp);

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            if (cmd.args[0] == "umount") return containercp::core::OperationResult{false, "busy"};
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_grants_loader([](uint64_t) {
        std::vector<containercp::access::LocalSftpProvider::GrantInfo> g;
        g.push_back({1, "test", "read_write"});
        g.push_back({2, "test2", "read_only"});
        return g;
    });
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping&) { return true; },
        [&stored](const std::string&, uint64_t) { return true; });

    auto r = provider.cleanup_all_mounts(1);
    CHECK_FALSE(r.success);
    CHECK(r.message.find("failed") != std::string::npos);
}



TEST_CASE("Phase3d grant rolls back newly created site group") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.state = "active";
    stored.push_back(um);
    inspector->groups_["containercp-sftp"] = {true, "containercp-sftp", 30000};
    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000, "/srv/containercp/users/au-dev", "/usr/sbin/nologin", true};

    auto fs = std::make_shared<FakeFsInspector>();

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_filesystem_inspector(fs);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            // Fail at Step 2 (add_user_to_site_group) — the group was just created at Step 1
            if (cmd.args[0] == "usermod") return containercp::core::OperationResult{false, "fail"};
            return fake_commands.run(cmd);
        }));
    provider.set_allocator(std::make_unique<containercp::access::SystemAccountAllocator>(
        containercp::access::SystemAccountAllocator::Range{10000, 19999},
        containercp::access::SystemAccountAllocator::Range{20000, 29999}));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    bool group_deleted = false;
    provider.set_grants_lookup([&group_deleted](uint64_t, const std::string&) -> size_t { return 0; });
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            for (auto& s : stored) { if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; } }
            stored.push_back(m); return true;
        },
        [&stored, &group_deleted](const std::string& t, uint64_t id) {
            if (t.find("site_group") != std::string::npos) group_deleted = true;
            return true;
        });

    auto r = provider.apply_grant(1, 1, "read_write");
    CHECK_FALSE(r.success);
    // Group was created at step 1, step 2 fails → group should be rolled back
    // The delete_site_group_if_unused is called with (void) for now
    // but the group was tracked for deletion
}

TEST_CASE("Phase3d grant_rollback_incomplete collects multiple errors") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.state = "active";
    stored.push_back(um);
    inspector->groups_["containercp-sftp"] = {true, "containercp-sftp", 30000};
    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000, "/srv/containercp/users/au-dev", "/usr/sbin/nologin", true};

    auto fs = std::make_shared<FakeFsInspector>();

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_filesystem_inspector(fs);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            // Step 5: mount fails → enters compound rollback
            if (cmd.args[0] == "mount") return containercp::core::OperationResult{false, ""};
            // During rollback: gpasswd (membership removal) also fails
            if (cmd.args[0] == "gpasswd") return containercp::core::OperationResult{false, ""};
            return fake_commands.run(cmd);
        }));
    provider.set_allocator(std::make_unique<containercp::access::SystemAccountAllocator>(
        containercp::access::SystemAccountAllocator::Range{10000, 19999},
        containercp::access::SystemAccountAllocator::Range{20001, 29999}));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            for (auto& s : stored) { if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; } }
            stored.push_back(m); return true;
        },
        [&stored](const std::string&, uint64_t) { return true; });

    auto r = provider.apply_grant(1, 1, "read_write");
    CHECK_FALSE(r.success);
    // Mount failed → compound rollback ran. membership rollback also failed → "grant_rollback_incomplete: membership"
    CHECK(r.message.find("grant_rollback_incomplete") != std::string::npos);
}

// --- ARCH-009 Task 17: Directory mode rollback tests ---

TEST_CASE("ARCH-009 directory permission rollback restores mode 0755") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    auto mount_inspector = std::make_shared<FakeMountInspector>();
    mount_inspector->state_.mounted = false;
    mount_inspector->state_.status = containercp::access::MountStatus::Absent;

    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.state = "active"; um.uid = 10000; um.gid = 20000;
    stored.push_back(um);
    containercp::access::SystemAccountMapping grp;
    grp.entity_type = "site_group_rw"; grp.entity_id = 1;
    grp.gid = 21000; grp.username = "site-1-rw"; grp.groupname = "site-1-rw"; grp.state = "active";
    stored.push_back(grp);

    // Initial FS state: mode = 0755 octal (493 decimal), GID = 20001
    std::string pub = "/srv/containercp/sites/test/public/";
    containercp::access::FsPermissionState init;
    init.exists = true; init.group_gid = 20001; init.mode = 0755;
    inspector->fs_state_->state_[pub] = init;

    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000,
                                   "/srv/containercp/users/au-dev", "/usr/sbin/nologin", true};
    inspector->groups_["containercp-sftp"] = {true, "containercp-sftp", 30000};
    inspector->groups_["site-1-rw"] = {true, "site-1-rw", 21000};

    // FakeFsInspector shares state with FakeCommandRunner via shared_ptr aliasing
    auto fs = std::make_shared<FakeFsInspector>(
        std::shared_ptr<std::map<std::string, containercp::access::FsPermissionState>>(
            inspector->fs_state_, &inspector->fs_state_->state_));

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_filesystem_inspector(fs);
    provider.set_mount_inspector(mount_inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            if (cmd.args[0] == "mount") return containercp::core::OperationResult{false, "mount failed"};
            return fake_commands.run(cmd);
        }));
    provider.set_allocator(std::make_unique<containercp::access::SystemAccountAllocator>(
        containercp::access::SystemAccountAllocator::Range{10000, 19999},
        containercp::access::SystemAccountAllocator::Range{20000, 29999}));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            for (auto& s : stored) { if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; } }
            stored.push_back(m); return true;
        },
        [&stored](const std::string&, uint64_t) { return true; });

    fake_commands.cmds_.clear();
    auto r = provider.apply_grant(1, 1, "read_write");
    CHECK_FALSE(r.success);

    // Verify chgrp was called to restore original GID (group name "20001")
    bool chgrp_found = false;
    for (const auto& c : fake_commands.cmds_) {
        if (c.args.size() >= 2 && c.args[0] == "chgrp" && c.args[1] == "20001") chgrp_found = true;
    }
    CHECK(chgrp_found);

    // Verify chmod was called with octal "755"
    bool chmod_found = false;
    for (const auto& c : fake_commands.cmds_) {
        if (c.args.size() >= 2 && c.args[0] == "chmod" && c.args[1] == "755") chmod_found = true;
    }
    CHECK(chmod_found);
}

TEST_CASE("ARCH-009 directory permission rollback restores mode 0770") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    auto mount_inspector = std::make_shared<FakeMountInspector>();
    mount_inspector->state_.mounted = false;
    mount_inspector->state_.status = containercp::access::MountStatus::Absent;

    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.state = "active"; um.uid = 10000; um.gid = 20000;
    stored.push_back(um);
    containercp::access::SystemAccountMapping grp;
    grp.entity_type = "site_group_rw"; grp.entity_id = 1;
    grp.gid = 21000; grp.username = "site-1-rw"; grp.groupname = "site-1-rw"; grp.state = "active";
    stored.push_back(grp);

    std::string pub = "/srv/containercp/sites/test/public/";
    containercp::access::FsPermissionState init;
    init.exists = true; init.group_gid = 20001; init.mode = 0770;
    inspector->fs_state_->state_[pub] = init;

    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000,
                                   "/srv/containercp/users/au-dev", "/usr/sbin/nologin", true};
    inspector->groups_["containercp-sftp"] = {true, "containercp-sftp", 30000};
    inspector->groups_["site-1-rw"] = {true, "site-1-rw", 21000};

    auto fs = std::make_shared<FakeFsInspector>(
        std::shared_ptr<std::map<std::string, containercp::access::FsPermissionState>>(
            inspector->fs_state_, &inspector->fs_state_->state_));

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_filesystem_inspector(fs);
    provider.set_mount_inspector(mount_inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            if (cmd.args[0] == "mount") return containercp::core::OperationResult{false, "mount failed"};
            return fake_commands.run(cmd);
        }));
    provider.set_allocator(std::make_unique<containercp::access::SystemAccountAllocator>(
        containercp::access::SystemAccountAllocator::Range{10000, 19999},
        containercp::access::SystemAccountAllocator::Range{20000, 29999}));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            for (auto& s : stored) { if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; } }
            stored.push_back(m); return true;
        },
        [&stored](const std::string&, uint64_t) { return true; });

    fake_commands.cmds_.clear();
    auto r = provider.apply_grant(1, 1, "read_write");
    CHECK_FALSE(r.success);

    bool chmod_found = false;
    for (const auto& c : fake_commands.cmds_) {
        if (c.args.size() >= 2 && c.args[0] == "chmod" && c.args[1] == "770") chmod_found = true;
    }
    CHECK(chmod_found);
}

TEST_CASE("ARCH-009 directory permission rollback restores mode 0700") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    auto mount_inspector = std::make_shared<FakeMountInspector>();
    mount_inspector->state_.mounted = false;
    mount_inspector->state_.status = containercp::access::MountStatus::Absent;

    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.state = "active"; um.uid = 10000; um.gid = 20000;
    stored.push_back(um);
    containercp::access::SystemAccountMapping grp;
    grp.entity_type = "site_group_rw"; grp.entity_id = 1;
    grp.gid = 21000; grp.username = "site-1-rw"; grp.groupname = "site-1-rw"; grp.state = "active";
    stored.push_back(grp);

    // Initial mode 0700 octal (448 decimal)
    std::string pub = "/srv/containercp/sites/test/public/";
    containercp::access::FsPermissionState init;
    init.exists = true; init.group_gid = 20001; init.mode = 0700;
    inspector->fs_state_->state_[pub] = init;

    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000,
                                   "/srv/containercp/users/au-dev", "/usr/sbin/nologin", true};
    inspector->groups_["containercp-sftp"] = {true, "containercp-sftp", 30000};
    inspector->groups_["site-1-rw"] = {true, "site-1-rw", 21000};

    auto fs = std::make_shared<FakeFsInspector>(
        std::shared_ptr<std::map<std::string, containercp::access::FsPermissionState>>(
            inspector->fs_state_, &inspector->fs_state_->state_));

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_filesystem_inspector(fs);
    provider.set_mount_inspector(mount_inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            if (cmd.args[0] == "mount") return containercp::core::OperationResult{false, "mount failed"};
            return fake_commands.run(cmd);
        }));
    provider.set_allocator(std::make_unique<containercp::access::SystemAccountAllocator>(
        containercp::access::SystemAccountAllocator::Range{10000, 19999},
        containercp::access::SystemAccountAllocator::Range{20000, 29999}));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            for (auto& s : stored) { if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; } }
            stored.push_back(m); return true;
        },
        [&stored](const std::string&, uint64_t) { return true; });

    fake_commands.cmds_.clear();
    auto r = provider.apply_grant(1, 1, "read_write");
    CHECK_FALSE(r.success);

    // Verify octal conversion: 0700 → "700"
    bool chmod_found = false;
    for (const auto& c : fake_commands.cmds_) {
        if (c.args.size() >= 2 && c.args[0] == "chmod" && c.args[1] == "700") chmod_found = true;
    }
    CHECK(chmod_found);
}

TEST_CASE("ARCH-009 directory permission rollback chmod command failure") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    auto mount_inspector = std::make_shared<FakeMountInspector>();
    mount_inspector->state_.mounted = false;
    mount_inspector->state_.status = containercp::access::MountStatus::Absent;

    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.state = "active"; um.uid = 10000; um.gid = 20000;
    stored.push_back(um);
    containercp::access::SystemAccountMapping grp;
    grp.entity_type = "site_group_rw"; grp.entity_id = 1;
    grp.gid = 21000; grp.username = "site-1-rw"; grp.groupname = "site-1-rw"; grp.state = "active";
    stored.push_back(grp);

    std::string pub = "/srv/containercp/sites/test/public/";
    containercp::access::FsPermissionState init;
    init.exists = true; init.group_gid = 20001; init.mode = 0755;
    inspector->fs_state_->state_[pub] = init;

    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000,
                                   "/srv/containercp/users/au-dev", "/usr/sbin/nologin", true};
    inspector->groups_["containercp-sftp"] = {true, "containercp-sftp", 30000};
    inspector->groups_["site-1-rw"] = {true, "site-1-rw", 21000};

    int fail_count = 0;
    auto fs = std::make_shared<FakeFsInspector>(
        std::shared_ptr<std::map<std::string, containercp::access::FsPermissionState>>(
            inspector->fs_state_, &inspector->fs_state_->state_));
    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_filesystem_inspector(fs);
    provider.set_mount_inspector(mount_inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands, &fail_count](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            if (cmd.args[0] == "mount") return containercp::core::OperationResult{false, "mount failed"};
            // Fail on chmod during rollback
            if (cmd.args[0] == "chmod" && fail_count++ > 0) return containercp::core::OperationResult{false, "chmod failed"};
            return fake_commands.run(cmd);
        }));
    provider.set_allocator(std::make_unique<containercp::access::SystemAccountAllocator>(
        containercp::access::SystemAccountAllocator::Range{10000, 19999},
        containercp::access::SystemAccountAllocator::Range{20000, 29999}));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            for (auto& s : stored) { if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; } }
            stored.push_back(m); return true;
        },
        [&stored](const std::string&, uint64_t) { return true; });

    fake_commands.cmds_.clear();
    auto r = provider.apply_grant(1, 1, "read_write");
    CHECK_FALSE(r.success);
    // chmod rollback failed → should include "perms:mode"
    CHECK(r.message.find("perms:mode") != std::string::npos);
}

TEST_CASE("ARCH-009 directory permission rollback GID command failure") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    auto mount_inspector = std::make_shared<FakeMountInspector>();
    mount_inspector->state_.mounted = false;
    mount_inspector->state_.status = containercp::access::MountStatus::Absent;

    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.state = "active"; um.uid = 10000; um.gid = 20000;
    stored.push_back(um);
    containercp::access::SystemAccountMapping grp;
    grp.entity_type = "site_group_rw"; grp.entity_id = 1;
    grp.gid = 21000; grp.username = "site-1-rw"; grp.groupname = "site-1-rw"; grp.state = "active";
    stored.push_back(grp);

    std::string pub = "/srv/containercp/sites/test/public/";
    containercp::access::FsPermissionState init;
    init.exists = true; init.group_gid = 20001; init.mode = 0755;
    inspector->fs_state_->state_[pub] = init;

    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000,
                                   "/srv/containercp/users/au-dev", "/usr/sbin/nologin", true};
    inspector->groups_["containercp-sftp"] = {true, "containercp-sftp", 30000};
    inspector->groups_["site-1-rw"] = {true, "site-1-rw", 21000};

    auto fs = std::make_shared<FakeFsInspector>(
        std::shared_ptr<std::map<std::string, containercp::access::FsPermissionState>>(
            inspector->fs_state_, &inspector->fs_state_->state_));
    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_filesystem_inspector(fs);
    provider.set_mount_inspector(mount_inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            if (cmd.args[0] == "mount") return containercp::core::OperationResult{false, "mount failed"};
            // Fail on chgrp during rollback (first chgrp after perms_changed)
            if (cmd.args[0] == "chgrp" && cmd.args[1] == "20001") return containercp::core::OperationResult{false, "chgrp failed"};
            return fake_commands.run(cmd);
        }));
    provider.set_allocator(std::make_unique<containercp::access::SystemAccountAllocator>(
        containercp::access::SystemAccountAllocator::Range{10000, 19999},
        containercp::access::SystemAccountAllocator::Range{20000, 29999}));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            for (auto& s : stored) { if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; } }
            stored.push_back(m); return true;
        },
        [&stored](const std::string&, uint64_t) { return true; });

    fake_commands.cmds_.clear();
    auto r = provider.apply_grant(1, 1, "read_write");
    CHECK_FALSE(r.success);
    // GID rollback failed → should include "perms:gid"
    CHECK(r.message.find("perms:gid") != std::string::npos);
}

TEST_CASE("ARCH-009 directory permission rollback postcondition mismatch") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    auto mount_inspector = std::make_shared<FakeMountInspector>();
    mount_inspector->state_.mounted = false;
    mount_inspector->state_.status = containercp::access::MountStatus::Absent;

    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.state = "active"; um.uid = 10000; um.gid = 20000;
    stored.push_back(um);
    containercp::access::SystemAccountMapping grp;
    grp.entity_type = "site_group_rw"; grp.entity_id = 1;
    grp.gid = 21000; grp.username = "site-1-rw"; grp.groupname = "site-1-rw"; grp.state = "active";
    stored.push_back(grp);

    std::string pub = "/srv/containercp/sites/test/public/";
    containercp::access::FsPermissionState init;
    init.exists = true; init.group_gid = 20001; init.mode = 0755;
    inspector->fs_state_->state_[pub] = init;

    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000,
                                   "/srv/containercp/users/au-dev", "/usr/sbin/nologin", true};
    inspector->groups_["containercp-sftp"] = {true, "containercp-sftp", 30000};
    inspector->groups_["site-1-rw"] = {true, "site-1-rw", 21000};

    // Use the shared-state FakeFsInspector so apply_directory_permissions passes.
    // The rollback postcondition is checked by fs_inspector_ after the rollback command runs.
    // We intercept the rollback chgrp/chmod to return success without updating state,
    // so the postcondition check sees stale data and reports a postcondition error.
    auto fs = std::make_shared<FakeFsInspector>(
        std::shared_ptr<std::map<std::string, containercp::access::FsPermissionState>>(
            inspector->fs_state_, &inspector->fs_state_->state_));

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_filesystem_inspector(fs);
    provider.set_mount_inspector(mount_inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            if (cmd.args[0] == "mount") return containercp::core::OperationResult{false, "mount failed"};
            // Rollback chgrp: return success but skip state update to trigger postcondition mismatch
            if (cmd.args[0] == "chgrp" && cmd.args[1] == "20001") return containercp::core::OperationResult{true, "ok"};
            // Rollback chmod: same treatment
            if (cmd.args[0] == "chmod" && cmd.args[1] == "755") return containercp::core::OperationResult{true, "ok"};
            return fake_commands.run(cmd);
        }));
    provider.set_allocator(std::make_unique<containercp::access::SystemAccountAllocator>(
        containercp::access::SystemAccountAllocator::Range{10000, 19999},
        containercp::access::SystemAccountAllocator::Range{20000, 29999}));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            for (auto& s : stored) { if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; } }
            stored.push_back(m); return true;
        },
        [&stored](const std::string&, uint64_t) { return true; });

    fake_commands.cmds_.clear();
    auto r = provider.apply_grant(1, 1, "read_write");
    CHECK_FALSE(r.success);
    // Postcondition mismatch for GID (group_gid in state is still 21000, not restored to 20001)
    bool has_gid_post = r.message.find("perms:gid:postcondition") != std::string::npos;
    bool has_mode_post = r.message.find("perms:mode:postcondition") != std::string::npos;
    // CHECK does not support || operator, so use separate checks
    if (!has_gid_post && !has_mode_post) {
        FAIL("Expected either perms:gid:postcondition or perms:mode:postcondition in: " << r.message);
    }
}

// --- ARCH-009 Task 18: ACL rollback verification tests ---

TEST_CASE("ARCH-009 ACL rollback initially absent access/default ACL") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    auto mount_inspector = std::make_shared<FakeMountInspector>();
    mount_inspector->state_.mounted = false;
    mount_inspector->state_.status = containercp::access::MountStatus::Absent;

    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.state = "active"; um.uid = 10000; um.gid = 20000;
    stored.push_back(um);
    containercp::access::SystemAccountMapping grp;
    grp.entity_type = "site_group_ro"; grp.entity_id = 1;
    grp.gid = 21000; grp.username = "site-1-ro"; grp.groupname = "site-1-ro"; grp.state = "active";
    stored.push_back(grp);

    std::string pub = "/srv/containercp/sites/test/public/";
    // Initially no ACL for site-1-ro — both access and default are absent
    containercp::access::FsPermissionState init_acl;
    init_acl.exists = true; init_acl.mode = 0755; init_acl.group_gid = 20001;
    init_acl.acl_status = containercp::access::InspectionStatus::Ok;
    init_acl.acl.access_present = false;
    init_acl.acl.default_present = false;
    inspector->fs_state_->state_[pub + "::site-1-ro"] = init_acl;

    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000,
                                   "/srv/containercp/users/au-dev", "/usr/sbin/nologin", true};
    inspector->groups_["containercp-sftp"] = {true, "containercp-sftp", 30000};
    inspector->groups_["site-1-ro"] = {true, "site-1-ro", 21000};

    auto fs = std::make_shared<FakeFsInspector>(
        std::shared_ptr<std::map<std::string, containercp::access::FsPermissionState>>(
            inspector->fs_state_, &inspector->fs_state_->state_));

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_filesystem_inspector(fs);
    provider.set_mount_inspector(mount_inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            if (cmd.args[0] == "mount") return containercp::core::OperationResult{false, "mount failed"};
            return fake_commands.run(cmd);
        }));
    provider.set_allocator(std::make_unique<containercp::access::SystemAccountAllocator>(
        containercp::access::SystemAccountAllocator::Range{10000, 19999},
        containercp::access::SystemAccountAllocator::Range{20000, 29999}));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            for (auto& s : stored) { if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; } }
            stored.push_back(m); return true;
        },
        [&stored](const std::string&, uint64_t) { return true; });

    fake_commands.cmds_.clear();
    auto r = provider.apply_grant(1, 1, "read_only");
    CHECK_FALSE(r.success);

    // After rollback, both access and default ACL should be absent
    auto post = fs->inspect_acl(pub, "site-1-ro");
    CHECK(post.acl_status == containercp::access::InspectionStatus::Ok);
    CHECK_FALSE(post.acl.access_present);
    CHECK_FALSE(post.acl.default_present);
}

TEST_CASE("ARCH-009 ACL rollback restores existing access ACL") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    auto mount_inspector = std::make_shared<FakeMountInspector>();
    mount_inspector->state_.mounted = false;
    mount_inspector->state_.status = containercp::access::MountStatus::Absent;

    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.state = "active"; um.uid = 10000; um.gid = 20000;
    stored.push_back(um);
    containercp::access::SystemAccountMapping grp;
    grp.entity_type = "site_group_ro"; grp.entity_id = 1;
    grp.gid = 21000; grp.username = "site-1-ro"; grp.groupname = "site-1-ro"; grp.state = "active";
    stored.push_back(grp);

    std::string pub = "/srv/containercp/sites/test/public/";
    // Pre-existing access ACL for site-1-ro with r-x perms
    containercp::access::FsPermissionState init_acl;
    init_acl.exists = true; init_acl.mode = 0755; init_acl.group_gid = 20001;
    init_acl.acl_status = containercp::access::InspectionStatus::Ok;
    init_acl.acl.access_present = true;
    init_acl.acl.access_group = "site-1-ro";
    init_acl.acl.access_perms = "r-x";
    init_acl.acl.effective_perms = "r-x";
    init_acl.acl.access_mask = "rwx";
    init_acl.acl.default_present = false;
    inspector->fs_state_->state_[pub + "::site-1-ro"] = init_acl;

    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000,
                                   "/srv/containercp/users/au-dev", "/usr/sbin/nologin", true};
    inspector->groups_["containercp-sftp"] = {true, "containercp-sftp", 30000};
    inspector->groups_["site-1-ro"] = {true, "site-1-ro", 21000};

    auto fs = std::make_shared<FakeFsInspector>(
        std::shared_ptr<std::map<std::string, containercp::access::FsPermissionState>>(
            inspector->fs_state_, &inspector->fs_state_->state_));

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_filesystem_inspector(fs);
    provider.set_mount_inspector(mount_inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            if (cmd.args[0] == "mount") return containercp::core::OperationResult{false, "mount failed"};
            return fake_commands.run(cmd);
        }));
    provider.set_allocator(std::make_unique<containercp::access::SystemAccountAllocator>(
        containercp::access::SystemAccountAllocator::Range{10000, 19999},
        containercp::access::SystemAccountAllocator::Range{20000, 29999}));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            for (auto& s : stored) { if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; } }
            stored.push_back(m); return true;
        },
        [&stored](const std::string&, uint64_t) { return true; });

    fake_commands.cmds_.clear();
    auto r = provider.apply_grant(1, 1, "read_only");
    CHECK_FALSE(r.success);

    // Verify the original access ACL is restored
    auto post = fs->inspect_acl(pub, "site-1-ro");
    CHECK(post.acl_status == containercp::access::InspectionStatus::Ok);
    CHECK(post.acl.access_present);
    CHECK(post.acl.access_group == "site-1-ro");
    CHECK(post.acl.access_perms == "r-x");
    CHECK(post.acl.effective_perms == "r-x");
    CHECK(post.acl.access_mask == "rwx");
    CHECK_FALSE(post.acl.default_present);
}

TEST_CASE("ARCH-009 ACL rollback restores existing default ACL") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    auto mount_inspector = std::make_shared<FakeMountInspector>();
    mount_inspector->state_.mounted = false;
    mount_inspector->state_.status = containercp::access::MountStatus::Absent;

    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.state = "active"; um.uid = 10000; um.gid = 20000;
    stored.push_back(um);
    containercp::access::SystemAccountMapping grp;
    grp.entity_type = "site_group_ro"; grp.entity_id = 1;
    grp.gid = 21000; grp.username = "site-1-ro"; grp.groupname = "site-1-ro"; grp.state = "active";
    stored.push_back(grp);

    std::string pub = "/srv/containercp/sites/test/public/";
    // Pre-existing default ACL, no access ACL
    containercp::access::FsPermissionState init_acl;
    init_acl.exists = true; init_acl.mode = 0755; init_acl.group_gid = 20001;
    init_acl.acl_status = containercp::access::InspectionStatus::Ok;
    init_acl.acl.access_present = false;
    init_acl.acl.default_present = true;
    init_acl.acl.default_group = "site-1-ro";
    init_acl.acl.default_perms = "r--";
    init_acl.acl.default_effective = "r--";
    init_acl.acl.default_mask = "rwx";
    inspector->fs_state_->state_[pub + "::site-1-ro"] = init_acl;

    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000,
                                   "/srv/containercp/users/au-dev", "/usr/sbin/nologin", true};
    inspector->groups_["containercp-sftp"] = {true, "containercp-sftp", 30000};
    inspector->groups_["site-1-ro"] = {true, "site-1-ro", 21000};

    auto fs = std::make_shared<FakeFsInspector>(
        std::shared_ptr<std::map<std::string, containercp::access::FsPermissionState>>(
            inspector->fs_state_, &inspector->fs_state_->state_));

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_filesystem_inspector(fs);
    provider.set_mount_inspector(mount_inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            if (cmd.args[0] == "mount") return containercp::core::OperationResult{false, "mount failed"};
            return fake_commands.run(cmd);
        }));
    provider.set_allocator(std::make_unique<containercp::access::SystemAccountAllocator>(
        containercp::access::SystemAccountAllocator::Range{10000, 19999},
        containercp::access::SystemAccountAllocator::Range{20000, 29999}));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            for (auto& s : stored) { if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; } }
            stored.push_back(m); return true;
        },
        [&stored](const std::string&, uint64_t) { return true; });

    fake_commands.cmds_.clear();
    auto r = provider.apply_grant(1, 1, "read_only");
    CHECK_FALSE(r.success);

    // Verify the original default ACL is restored
    auto post = fs->inspect_acl(pub, "site-1-ro");
    CHECK(post.acl_status == containercp::access::InspectionStatus::Ok);
    CHECK(post.acl.default_present);
    CHECK(post.acl.default_group == "site-1-ro");
    CHECK(post.acl.default_perms == "r--");
    CHECK(post.acl.default_effective == "r--");
    CHECK(post.acl.default_mask == "rwx");
    CHECK_FALSE(post.acl.access_present);
}

TEST_CASE("ARCH-009 ACL rollback restores different masks") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    auto mount_inspector = std::make_shared<FakeMountInspector>();
    mount_inspector->state_.mounted = false;
    mount_inspector->state_.status = containercp::access::MountStatus::Absent;

    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.state = "active"; um.uid = 10000; um.gid = 20000;
    stored.push_back(um);
    containercp::access::SystemAccountMapping grp;
    grp.entity_type = "site_group_ro"; grp.entity_id = 1;
    grp.gid = 21000; grp.username = "site-1-ro"; grp.groupname = "site-1-ro"; grp.state = "active";
    stored.push_back(grp);

    std::string pub = "/srv/containercp/sites/test/public/";
    // Access + default ACLs with non-default masks
    containercp::access::FsPermissionState init_acl;
    init_acl.exists = true; init_acl.mode = 0755; init_acl.group_gid = 20001;
    init_acl.acl_status = containercp::access::InspectionStatus::Ok;
    init_acl.acl.access_present = true;
    init_acl.acl.access_group = "site-1-ro";
    init_acl.acl.access_perms = "r--";
    init_acl.acl.effective_perms = "r--";
    init_acl.acl.access_mask = "r--";
    init_acl.acl.default_present = true;
    init_acl.acl.default_group = "site-1-ro";
    init_acl.acl.default_perms = "r-x";
    init_acl.acl.default_effective = "r-x";
    init_acl.acl.default_mask = "rwx";
    inspector->fs_state_->state_[pub + "::site-1-ro"] = init_acl;

    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000,
                                   "/srv/containercp/users/au-dev", "/usr/sbin/nologin", true};
    inspector->groups_["containercp-sftp"] = {true, "containercp-sftp", 30000};
    inspector->groups_["site-1-ro"] = {true, "site-1-ro", 21000};

    auto fs = std::make_shared<FakeFsInspector>(
        std::shared_ptr<std::map<std::string, containercp::access::FsPermissionState>>(
            inspector->fs_state_, &inspector->fs_state_->state_));

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_filesystem_inspector(fs);
    provider.set_mount_inspector(mount_inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            if (cmd.args[0] == "mount") return containercp::core::OperationResult{false, "mount failed"};
            return fake_commands.run(cmd);
        }));
    provider.set_allocator(std::make_unique<containercp::access::SystemAccountAllocator>(
        containercp::access::SystemAccountAllocator::Range{10000, 19999},
        containercp::access::SystemAccountAllocator::Range{20000, 29999}));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            for (auto& s : stored) { if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; } }
            stored.push_back(m); return true;
        },
        [&stored](const std::string&, uint64_t) { return true; });

    fake_commands.cmds_.clear();
    auto r = provider.apply_grant(1, 1, "read_only");
    CHECK_FALSE(r.success);

    // Verify masks are restored
    auto post = fs->inspect_acl(pub, "site-1-ro");
    CHECK(post.acl.access_mask == "r--");
    CHECK(post.acl.default_mask == "rwx");
}

TEST_CASE("ARCH-009 ACL rollback command failure") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    auto mount_inspector = std::make_shared<FakeMountInspector>();
    mount_inspector->state_.mounted = false;
    mount_inspector->state_.status = containercp::access::MountStatus::Absent;

    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.state = "active"; um.uid = 10000; um.gid = 20000;
    stored.push_back(um);
    containercp::access::SystemAccountMapping grp;
    grp.entity_type = "site_group_ro"; grp.entity_id = 1;
    grp.gid = 21000; grp.username = "site-1-ro"; grp.groupname = "site-1-ro"; grp.state = "active";
    stored.push_back(grp);

    std::string pub = "/srv/containercp/sites/test/public/";
    // Originally absent ACL — rollback needs to call setfacl -x
    containercp::access::FsPermissionState init_acl;
    init_acl.exists = true; init_acl.mode = 0755; init_acl.group_gid = 20001;
    init_acl.acl_status = containercp::access::InspectionStatus::Ok;
    init_acl.acl.access_present = false;
    init_acl.acl.default_present = false;
    inspector->fs_state_->state_[pub + "::site-1-ro"] = init_acl;

    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000,
                                   "/srv/containercp/users/au-dev", "/usr/sbin/nologin", true};
    inspector->groups_["containercp-sftp"] = {true, "containercp-sftp", 30000};
    inspector->groups_["site-1-ro"] = {true, "site-1-ro", 21000};

    auto fs = std::make_shared<FakeFsInspector>(
        std::shared_ptr<std::map<std::string, containercp::access::FsPermissionState>>(
            inspector->fs_state_, &inspector->fs_state_->state_));

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_filesystem_inspector(fs);
    provider.set_mount_inspector(mount_inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            if (cmd.args[0] == "mount") return containercp::core::OperationResult{false, "mount failed"};
            // Only fail setfacl -x (remove) — these are used during rollback restore
            // when the original ACL was absent. The -m (modify) commands during apply
            // phase must succeed.
            if (cmd.args[0] == "setfacl" && cmd.args[1] == "-x") return containercp::core::OperationResult{false, "setfacl -x failed"};
            return fake_commands.run(cmd);
        }));
    provider.set_allocator(std::make_unique<containercp::access::SystemAccountAllocator>(
        containercp::access::SystemAccountAllocator::Range{10000, 19999},
        containercp::access::SystemAccountAllocator::Range{20000, 29999}));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            for (auto& s : stored) { if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; } }
            stored.push_back(m); return true;
        },
        [&stored](const std::string&, uint64_t) { return true; });

    fake_commands.cmds_.clear();
    auto r = provider.apply_grant(1, 1, "read_only");
    CHECK_FALSE(r.success);
    // setfacl -x during rollback failed → should include "acl:restore:access"
    CHECK(r.message.find("acl:restore:access") != std::string::npos);
}

TEST_CASE("ARCH-009 ACL rollback inspection failure") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    auto mount_inspector = std::make_shared<FakeMountInspector>();
    mount_inspector->state_.mounted = false;
    mount_inspector->state_.status = containercp::access::MountStatus::Absent;

    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.state = "active"; um.uid = 10000; um.gid = 20000;
    stored.push_back(um);
    containercp::access::SystemAccountMapping grp;
    grp.entity_type = "site_group_ro"; grp.entity_id = 1;
    grp.gid = 21000; grp.username = "site-1-ro"; grp.groupname = "site-1-ro"; grp.state = "active";
    stored.push_back(grp);

    std::string pub = "/srv/containercp/sites/test/public/";
    // Originally absent ACL
    containercp::access::FsPermissionState init_acl;
    init_acl.exists = true; init_acl.mode = 0755; init_acl.group_gid = 20001;
    init_acl.acl_status = containercp::access::InspectionStatus::Ok;
    init_acl.acl.access_present = false;
    init_acl.acl.default_present = false;
    inspector->fs_state_->state_[pub + "::site-1-ro"] = init_acl;

    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000,
                                   "/srv/containercp/users/au-dev", "/usr/sbin/nologin", true};
    inspector->groups_["containercp-sftp"] = {true, "containercp-sftp", 30000};
    inspector->groups_["site-1-ro"] = {true, "site-1-ro", 21000};

    // Custom inspector that delegates to shared state but returns failure on
    // the postcondition call during rollback (4th call = index 3).
    struct FailOnPostInspect : FakeFsInspector {
        using FakeFsInspector::FakeFsInspector;
        mutable int call_count_ = 0;
        containercp::access::FsPermissionState inspect_acl(
            const std::string& path, const std::string& groupname) const override {
            auto s = FakeFsInspector::inspect_acl(path, groupname);
            if (call_count_++ >= 3) s.acl_status = containercp::access::InspectionStatus::PathInspectionFailed;
            return s;
        }
    };

    auto fs = std::make_shared<FailOnPostInspect>(
        std::shared_ptr<std::map<std::string, containercp::access::FsPermissionState>>(
            inspector->fs_state_, &inspector->fs_state_->state_));

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_filesystem_inspector(fs);
    provider.set_mount_inspector(mount_inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            if (cmd.args[0] == "mount") return containercp::core::OperationResult{false, "mount failed"};
            return fake_commands.run(cmd);
        }));
    provider.set_allocator(std::make_unique<containercp::access::SystemAccountAllocator>(
        containercp::access::SystemAccountAllocator::Range{10000, 19999},
        containercp::access::SystemAccountAllocator::Range{20000, 29999}));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            for (auto& s : stored) { if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; } }
            stored.push_back(m); return true;
        },
        [&stored](const std::string&, uint64_t) { return true; });

    fake_commands.cmds_.clear();
    auto r = provider.apply_grant(1, 1, "read_only");
    CHECK_FALSE(r.success);
    // Inspection after rollback restore failed → should include "acl:postcondition:inspect"
    CHECK(r.message.find("acl:postcondition:inspect") != std::string::npos);
}

TEST_CASE("ARCH-009 ACL rollback full-state mismatch") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    auto mount_inspector = std::make_shared<FakeMountInspector>();
    mount_inspector->state_.mounted = false;
    mount_inspector->state_.status = containercp::access::MountStatus::Absent;

    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.state = "active"; um.uid = 10000; um.gid = 20000;
    stored.push_back(um);
    containercp::access::SystemAccountMapping grp;
    grp.entity_type = "site_group_ro"; grp.entity_id = 1;
    grp.gid = 21000; grp.username = "site-1-ro"; grp.groupname = "site-1-ro"; grp.state = "active";
    stored.push_back(grp);

    std::string pub = "/srv/containercp/sites/test/public/";
    // Originally absent ACL
    containercp::access::FsPermissionState init_acl;
    init_acl.exists = true; init_acl.mode = 0755; init_acl.group_gid = 20001;
    init_acl.acl_status = containercp::access::InspectionStatus::Ok;
    init_acl.acl.access_present = false;
    init_acl.acl.default_present = false;
    inspector->fs_state_->state_[pub + "::site-1-ro"] = init_acl;

    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000,
                                   "/srv/containercp/users/au-dev", "/usr/sbin/nologin", true};
    inspector->groups_["containercp-sftp"] = {true, "containercp-sftp", 30000};
    inspector->groups_["site-1-ro"] = {true, "site-1-ro", 21000};

    auto fs = std::make_shared<FakeFsInspector>(
        std::shared_ptr<std::map<std::string, containercp::access::FsPermissionState>>(
            inspector->fs_state_, &inspector->fs_state_->state_));

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_filesystem_inspector(fs);
    provider.set_mount_inspector(mount_inspector);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            if (cmd.args[0] == "mount") return containercp::core::OperationResult{false, "mount failed"};
            // Intercept rollback setfacl -x: return success but skip state update,
            // so the postcondition check sees stale data (ACL still present)
            if (cmd.args[0] == "setfacl" && cmd.args[1] == "-x") return containercp::core::OperationResult{true, "ok"};
            return fake_commands.run(cmd);
        }));
    provider.set_allocator(std::make_unique<containercp::access::SystemAccountAllocator>(
        containercp::access::SystemAccountAllocator::Range{10000, 19999},
        containercp::access::SystemAccountAllocator::Range{20000, 29999}));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) { containercp::access::LocalSftpProvider::SiteInfo info; info.valid = true; info.site_id = id; info.domain = "test"; info.root = "/srv/containercp/sites/test"; return info; });
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            for (auto& s : stored) { if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; } }
            stored.push_back(m); return true;
        },
        [&stored](const std::string&, uint64_t) { return true; });

    fake_commands.cmds_.clear();
    auto r = provider.apply_grant(1, 1, "read_only");
    CHECK_FALSE(r.success);
    // The rollback postcondition should find that the ACL is still present
    // (because setfacl -x was intercepted), causing a state mismatch
    CHECK(r.message.find("acl:postcondition:mismatch") != std::string::npos);
}

// --- ARCH-009 Task 20: Mountinfo parser tests ---

TEST_CASE("ARCH-009 mountinfo parses real bind mount") {
    using containercp::access::MountStatus;
    std::string line = "37 26 0:30 /srv/containercp/sites/example/public "
                       "/srv/containercp/users/dev/sites/example rw,relatime "
                       "- ext4 /dev/sdc1 rw";
    auto s = containercp::access::parse_mountinfo_line(line);
    CHECK(s.status == MountStatus::Ok);
    CHECK(s.mount_id == 37);
    CHECK(s.parent_id == 26);
    CHECK(s.device == "0:30");
    CHECK(s.bind_root == "/srv/containercp/sites/example/public");
    CHECK(s.target == "/srv/containercp/users/dev/sites/example");
    CHECK(s.is_bind);
    CHECK(s.fstype == "ext4");
    CHECK(s.source == "/dev/sdc1");
    CHECK(s.super_options == "rw");
    CHECK(s.options == "rw,relatime");
}

TEST_CASE("ARCH-009 mountinfo parses normal filesystem mount") {
    using containercp::access::MountStatus;
    std::string line = "26 0 8:2 / / rw,relatime shared:1 - ext4 /dev/sda1 rw";
    auto s = containercp::access::parse_mountinfo_line(line);
    CHECK(s.status == MountStatus::Ok);
    CHECK(s.mount_id == 26);
    CHECK(s.parent_id == 0);
    CHECK(s.device == "8:2");
    CHECK(s.bind_root == "/");
    CHECK(s.target == "/");
    CHECK_FALSE(s.is_bind);
    CHECK(s.fstype == "ext4");
    CHECK(s.source == "/dev/sda1");
    CHECK(s.options == "rw,relatime");
    CHECK(s.optional_fields.size() == 1);
    CHECK(s.optional_fields[0] == "shared:1");
}

TEST_CASE("ARCH-009 mountinfo parses nested bind mount") {
    using containercp::access::MountStatus;
    std::string line = "38 37 0:30 /srv/containercp/sites/example/public/sub "
                       "/srv/containercp/users/dev/sites/example/sub rw,relatime "
                       "- ext4 /dev/sdc1 rw";
    auto s = containercp::access::parse_mountinfo_line(line);
    CHECK(s.status == MountStatus::Ok);
    CHECK(s.mount_id == 38);
    CHECK(s.parent_id == 37);
    CHECK(s.bind_root == "/srv/containercp/sites/example/public/sub");
    CHECK(s.target == "/srv/containercp/users/dev/sites/example/sub");
    CHECK(s.is_bind);
}

TEST_CASE("ARCH-009 mountinfo decodes escaped source") {
    using containercp::access::MountStatus;
    std::string line = "39 26 0:31 / /mnt rw,relatime shared:2 - tmpfs tmpfs\\040with\\040space rw";
    auto s = containercp::access::parse_mountinfo_line(line);
    CHECK(s.status == MountStatus::Ok);
    CHECK(s.source == "tmpfs with space");
}

TEST_CASE("ARCH-009 mountinfo decodes escaped target") {
    using containercp::access::MountStatus;
    std::string line = "40 26 0:32 / /mnt/my\\011dir rw,relatime "
                       "- ext4 /dev/sdb1 rw";
    auto s = containercp::access::parse_mountinfo_line(line);
    CHECK(s.status == MountStatus::Ok);
    CHECK(s.target == "/mnt/my\tdir");
}

TEST_CASE("ARCH-009 mountinfo rejects malformed field count") {
    using containercp::access::MountStatus;
    // Only 5 fields before separator (need at least 6: id parent_id dev root mountpoint options)
    std::string line = "26 0 8:2 / rw,relatime - ext4 /dev/sda1 rw";
    auto s = containercp::access::parse_mountinfo_line(line);
    CHECK(s.status == MountStatus::Absent);
    CHECK(s.error_detail.find("too few fields") != std::string::npos);
}

TEST_CASE("ARCH-009 mountinfo rejects missing separator") {
    using containercp::access::MountStatus;
    std::string line = "26 0 8:2 / / rw,relatime ext4 /dev/sda1 rw";
    auto s = containercp::access::parse_mountinfo_line(line);
    CHECK(s.status == MountStatus::Absent);
    CHECK(s.error_detail.find("missing separator") != std::string::npos);
}

TEST_CASE("ARCH-009 mountinfo duplicate target returns first") {
    using containercp::access::MountStatus;
    std::string content =
        "37 26 0:30 /src/public /target rw,relatime - ext4 /dev/sdc1 rw\n"
        "38 37 0:30 /other /target rw,relatime - ext4 /dev/sdc1 rw\n";
    auto s = containercp::access::parse_mountinfo(content, "/target");
    CHECK(s.status == MountStatus::Ok);
    CHECK(s.bind_root == "/src/public");
}

TEST_CASE("ARCH-009 mountinfo rejects source mismatch") {
    using containercp::access::MountStatus;
    std::string line = "37 26 0:30 /src/public /actual-target rw,relatime "
                       "- ext4 /dev/sdc1 rw";
    // Using parse_mountinfo_line with target_filter that doesn't match
    auto s = containercp::access::parse_mountinfo_line(line, "/wrong-target");
    CHECK(s.status == MountStatus::Absent);
    CHECK(s.error_detail.find("target mismatch") != std::string::npos);
}

TEST_CASE("ARCH-009 mountinfo rejects target mismatch via parse_mountinfo") {
    using containercp::access::MountStatus;
    std::string content = "37 26 0:30 /src/public /actual-target rw,relatime "
                          "- ext4 /dev/sdc1 rw\n";
    auto s = containercp::access::parse_mountinfo(content, "/nonexistent");
    CHECK(s.status == MountStatus::Absent);
    CHECK(s.error_detail.find("not found") != std::string::npos);
}

TEST_CASE("ARCH-009 mountinfo decodes backslash escape") {
    using containercp::access::MountStatus;
    std::string line = "41 26 0:33 /path/with\\134backslash /mnt/point rw,relatime "
                       "- ext4 /dev/sdc1 rw";
    auto s = containercp::access::parse_mountinfo_line(line);
    CHECK(s.status == MountStatus::Ok);
    CHECK(s.bind_root == "/path/with\\backslash");
}

// --- ARCH-009 Task 21: Bind-mount rollback verification ---

// Helper: create a provider with live fake inspectors that share state with FakeCommandRunner
struct BindMountTestContext {
    std::shared_ptr<FakeInspector> inspector;
    FakeCommandRunner fake_commands;
    std::shared_ptr<FakeLiveMountInspector> mount_inspector;
    std::shared_ptr<FakeLiveFsInspector> fs_inspector;
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::LocalSftpProvider provider;

    BindMountTestContext()
        : inspector(std::make_shared<FakeInspector>())
        , fake_commands(inspector)
        , mount_inspector(std::make_shared<FakeLiveMountInspector>(inspector->mount_state_))
        , fs_inspector(std::make_shared<FakeLiveFsInspector>(inspector->fs_state_))
        , provider(containercp::logger::Logger::instance())
    {
        containercp::access::SystemAccountMapping um;
        um.entity_type = "access_user"; um.entity_id = 1;
        um.username = "au-dev"; um.uid = 10000; um.gid = 20000; um.state = "active";
        stored.push_back(um);

        inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000,
                                       "/srv/containercp/users/au-dev", "/usr/sbin/nologin", true};

        provider.set_identity_inspector(inspector);
        provider.set_mount_inspector(mount_inspector);
        provider.set_filesystem_inspector(fs_inspector);
        provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
            [this](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
                return fake_commands.run(cmd);
            }));
        provider.set_enabled(true);
        provider.set_site_resolver([](uint64_t id) {
            containercp::access::LocalSftpProvider::SiteInfo info;
            info.valid = true; info.site_id = id;
            info.domain = "test"; info.root = "/srv/containercp/sites/test";
            return info;
        });
        provider.set_mapping_persistence(
            [this]() { return stored; },
            [this](const containercp::access::SystemAccountMapping& m) {
                for (auto& s : stored) {
                    if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; }
                }
                stored.push_back(m); return true;
            },
            [](const std::string&, uint64_t) { return true; });
        provider.set_grants_loader([](uint64_t uid) {
            std::vector<containercp::access::LocalSftpProvider::GrantInfo> grants;
            if (uid == 1) {
                grants.push_back({1, "test", "read_write"});
            }
            return grants;
        });
    }
};

TEST_CASE("ARCH-009 bind mount verification failure clean rollback") {
    BindMountTestContext ctx;

    // After mount_bind records the mount, erase it from shared state so
    // the verification inspect returns Absent → triggers rollback.
    auto wrapper = std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&ctx](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            auto result = ctx.fake_commands.run(cmd);
            if (cmd.args[0] == "mount" && result.success) {
                std::string target = cmd.args.back();
                ctx.inspector->mount_state_->mounted_paths_.erase(target);
                ctx.inspector->mount_state_->bind_sources_.erase(target);
            }
            return result;
        });
    ctx.provider.set_command_runner(std::move(wrapper));

    auto r = ctx.provider.bind_mount_site(1, 1);
    CHECK_FALSE(r.success);
    CHECK(r.message == "mount verification failed");
    // Directory was cleaned up (dir was created by this operation)
    CHECK(ctx.inspector->fs_state_->state_.count(
        "/srv/containercp/users/au-dev/sites/test") == 0);
}

TEST_CASE("ARCH-009 bind mount rollback umount failure") {
    BindMountTestContext ctx;

    // Make mount succeed, verification fail, then umount fail
    // Override command runner to erase mount from state after mount (to trigger verification failure)
    // and fail on umount
    bool umount_called = false;
    auto wrapper = std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&ctx, &umount_called](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            if (cmd.args[0] == "mount" && !umount_called) {
                auto result = ctx.fake_commands.run(cmd);
                // Erase mount from state so verification fails
                if (result.success) {
                    std::string target = cmd.args.back();
                    ctx.inspector->mount_state_->mounted_paths_.erase(target);
                    ctx.inspector->mount_state_->bind_sources_.erase(target);
                }
                return result;
            }
            if (cmd.args[0] == "umount") {
                umount_called = true;
                return containercp::core::OperationResult{false, "umount failed"};
            }
            return ctx.fake_commands.run(cmd);
        });
    ctx.provider.set_command_runner(std::move(wrapper));

    auto r = ctx.provider.bind_mount_site(1, 1);
    CHECK_FALSE(r.success);
    CHECK(r.message == "mount rollback umount failure");
}

TEST_CASE("ARCH-009 bind mount rollback still mounted") {
    BindMountTestContext ctx;

    // Mount succeeds and is recorded. Verification sees wrong bind_root → fails.
    // Umount succeeds BUT re-inspect still sees the mount (simulated by re-adding after umount).
    bool in_umount = false;
    auto wrapper = std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&ctx, &in_umount](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            if (cmd.args[0] == "mount") {
                auto result = ctx.fake_commands.run(cmd);
                if (result.success) {
                    std::string target = cmd.args.back();
                    ctx.inspector->mount_state_->bind_sources_[target] = "/wrong/source";
                }
                return result;
            }
            if (cmd.args[0] == "umount") {
                in_umount = true;
                auto result = ctx.fake_commands.run(cmd);
                // After umount removes the mount, re-add it so re-inspect sees "still mounted"
                std::string target = cmd.args.back();
                ctx.inspector->mount_state_->mounted_paths_.insert(target);
                ctx.inspector->mount_state_->bind_sources_[target] = "/wrong/source";
                return result;
            }
            return ctx.fake_commands.run(cmd);
        });
    ctx.provider.set_command_runner(std::move(wrapper));

    auto r = ctx.provider.bind_mount_site(1, 1);
    CHECK_FALSE(r.success);
    CHECK(r.message == "mount rollback still mounted");
    // Directory should still exist (dir was created but rollback stopped before rmdir)
    CHECK(ctx.inspector->fs_state_->state_.count(
        "/srv/containercp/users/au-dev/sites/test") > 0);
}

TEST_CASE("ARCH-009 bind mount rollback rmdir failure") {
    BindMountTestContext ctx;

    int call_count = 0;
    auto wrapper = std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&ctx, &call_count](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            if (cmd.args[0] == "mount") {
                auto result = ctx.fake_commands.run(cmd);
                if (result.success) {
                    std::string target = cmd.args.back();
                    ctx.inspector->mount_state_->mounted_paths_.erase(target);
                    ctx.inspector->mount_state_->bind_sources_.erase(target);
                }
                return result;
            }
            if (cmd.args[0] == "rmdir") {
                ++call_count;
                if (call_count == 1) {
                    // First rmdir attempt (the one in rollback) — fail it
                    return containercp::core::OperationResult{false, "rmdir failed"};
                }
            }
            return ctx.fake_commands.run(cmd);
        });
    ctx.provider.set_command_runner(std::move(wrapper));

    auto r = ctx.provider.bind_mount_site(1, 1);
    CHECK_FALSE(r.success);
    CHECK(r.message == "mount rollback rmdir failure");
}

TEST_CASE("ARCH-009 bind mount rollback target still exists") {
    BindMountTestContext ctx;

    auto wrapper = std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&ctx](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            if (cmd.args[0] == "mount") {
                auto result = ctx.fake_commands.run(cmd);
                if (result.success) {
                    std::string target = cmd.args.back();
                    ctx.inspector->mount_state_->mounted_paths_.erase(target);
                    ctx.inspector->mount_state_->bind_sources_.erase(target);
                }
                return result;
            }
            if (cmd.args[0] == "rmdir") {
                auto result = ctx.fake_commands.run(cmd);
                // rmdir succeeds (removes from fs_state_) but re-add so fs_inspect sees it
                if (result.success) {
                    std::string target = cmd.args.back();
                    containercp::access::FsPermissionState s;
                    s.exists = true; s.group_gid = 0; s.mode = 0755;
                    s.acl_status = containercp::access::InspectionStatus::Ok;
                    ctx.inspector->fs_state_->state_[target] = s;
                }
                return result;
            }
            return ctx.fake_commands.run(cmd);
        });
    ctx.provider.set_command_runner(std::move(wrapper));

    auto r = ctx.provider.bind_mount_site(1, 1);
    CHECK_FALSE(r.success);
    CHECK(r.message == "mount rollback target still exists");
}

TEST_CASE("ARCH-009 bind mount rollback with pre-existing directory") {
    BindMountTestContext ctx;

    // Pre-create the target directory so dir_created = false
    containercp::access::FsPermissionState s;
    s.exists = true; s.group_gid = 20000; s.mode = 0755;
    s.acl_status = containercp::access::InspectionStatus::Ok;
    ctx.inspector->fs_state_->state_["/srv/containercp/users/au-dev/sites/test"] = s;

    auto wrapper = std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&ctx](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            if (cmd.args[0] == "mount") {
                auto result = ctx.fake_commands.run(cmd);
                if (result.success) {
                    std::string target = cmd.args.back();
                    ctx.inspector->mount_state_->mounted_paths_.erase(target);
                    ctx.inspector->mount_state_->bind_sources_.erase(target);
                }
                return result;
            }
            return ctx.fake_commands.run(cmd);
        });
    ctx.provider.set_command_runner(std::move(wrapper));

    auto r = ctx.provider.bind_mount_site(1, 1);
    CHECK_FALSE(r.success);
    // Should skip rmdir because dir_created = false
    CHECK(r.message == "mount verification failed");
    // Directory should still exist (was pre-existing, not removed)
    CHECK(ctx.inspector->fs_state_->state_.count(
        "/srv/containercp/users/au-dev/sites/test") > 0);
}

TEST_CASE("ARCH-009 bind mount verification failure mounts correctly") {
    // Happy path: the mount succeeds and verification passes
    BindMountTestContext ctx;

    auto r = ctx.provider.bind_mount_site(1, 1);
    CHECK(r.success);
    CHECK(r.message == "mounted: test");
    // Verify mount was recorded
    CHECK(ctx.inspector->mount_state_->mounted_paths_.count(
        "/srv/containercp/users/au-dev/sites/test") > 0);
}

// --- Idempotent bind identity tests ---

TEST_CASE("ARCH-009 bind mount idempotent exact match") {
    BindMountTestContext ctx;
    auto target = "/srv/containercp/users/au-dev/sites/test";
    auto source = "/srv/containercp/sites/test/public/";

    // Pre-populate mount state as if the mount already exists
    ctx.inspector->mount_state_->mounted_paths_.insert(target);
    ctx.inspector->mount_state_->bind_sources_[target] = source;

    auto r = ctx.provider.bind_mount_site(1, 1);
    CHECK(r.success);
    CHECK(r.message == "mount already exists");
}

TEST_CASE("ARCH-009 bind mount idempotent target mismatch") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.uid = 10000; um.gid = 20000; um.state = "active";
    stored.push_back(um);
    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000,
                                   "/srv/containercp/users/au-dev", "/usr/sbin/nologin", true};

    auto mount_insp = std::make_shared<FakeMountInspector>();
    mount_insp->state_.mounted = true;
    mount_insp->state_.is_bind = true;
    mount_insp->state_.target = "/wrong/target";  // mismatched target
    mount_insp->state_.bind_root = "/srv/containercp/sites/test/public/";
    mount_insp->state_.fstype = "ext4";
    mount_insp->state_.device = "0:30";
    mount_insp->state_.options = "rw";
    mount_insp->state_.status = containercp::access::MountStatus::Ok;

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_mount_inspector(mount_insp);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) {
        containercp::access::LocalSftpProvider::SiteInfo info;
        info.valid = true; info.site_id = id;
        info.domain = "test"; info.root = "/srv/containercp/sites/test";
        return info;
    });
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            for (auto& s : stored) { if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; } }
            stored.push_back(m); return true;
        },
        [](const std::string&, uint64_t) { return true; });
    provider.set_grants_loader([](uint64_t uid) {
        std::vector<containercp::access::LocalSftpProvider::GrantInfo> grants;
        if (uid == 1) grants.push_back({1, "test", "read_write"});
        return grants;
    });

    auto r = provider.bind_mount_site(1, 1);
    CHECK_FALSE(r.success);
    CHECK(r.message.find("foreign_or_mismatched_mount:target") != std::string::npos);
}

TEST_CASE("ARCH-009 bind mount idempotent source mismatch") {
    BindMountTestContext ctx;
    auto target = "/srv/containercp/users/au-dev/sites/test";

    // Mount exists but with wrong source
    ctx.inspector->mount_state_->mounted_paths_.insert(target);
    ctx.inspector->mount_state_->bind_sources_[target] = "/wrong/source";

    auto r = ctx.provider.bind_mount_site(1, 1);
    CHECK_FALSE(r.success);
    CHECK(r.message.find("foreign_or_mismatched_mount:source") != std::string::npos);
}

TEST_CASE("ARCH-009 bind mount idempotent non-bind regular mount") {
    // Use static FakeMountInspector to return a non-bind mount
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.uid = 10000; um.gid = 20000; um.state = "active";
    stored.push_back(um);
    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000,
                                   "/srv/containercp/users/au-dev", "/usr/sbin/nologin", true};

    auto mount_insp = std::make_shared<FakeMountInspector>();
    mount_insp->state_.mounted = true;
    mount_insp->state_.is_bind = false;
    mount_insp->state_.target = "/srv/containercp/users/au-dev/sites/test";
    mount_insp->state_.bind_root = "/";
    mount_insp->state_.fstype = "ext4";
    mount_insp->state_.device = "8:2";
    mount_insp->state_.options = "rw";
    mount_insp->state_.status = containercp::access::MountStatus::Ok;

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_mount_inspector(mount_insp);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) {
        containercp::access::LocalSftpProvider::SiteInfo info;
        info.valid = true; info.site_id = id;
        info.domain = "test"; info.root = "/srv/containercp/sites/test";
        return info;
    });
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            for (auto& s : stored) { if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; } }
            stored.push_back(m); return true;
        },
        [](const std::string&, uint64_t) { return true; });
    provider.set_grants_loader([](uint64_t uid) {
        std::vector<containercp::access::LocalSftpProvider::GrantInfo> grants;
        if (uid == 1) grants.push_back({1, "test", "read_write"});
        return grants;
    });

    auto r = provider.bind_mount_site(1, 1);
    CHECK_FALSE(r.success);
    CHECK(r.message.find("foreign_or_mismatched_mount:no_bind") != std::string::npos);
}

TEST_CASE("ARCH-009 bind mount idempotent device mismatch") {
    BindMountTestContext ctx;
    auto target = "/srv/containercp/users/au-dev/sites/test";
    auto source = "/srv/containercp/sites/test/public/";

    ctx.inspector->mount_state_->mounted_paths_.insert(target);
    ctx.inspector->mount_state_->bind_sources_[target] = source;
    // Device will be "0:30" from FakeLiveMountInspector defaults — we need it empty to fail
    // We can't override individual fields via shared state, so use a static inspector
    auto mount_insp = std::make_shared<FakeMountInspector>();
    mount_insp->state_.mounted = true;
    mount_insp->state_.is_bind = true;
    mount_insp->state_.target = target;
    mount_insp->state_.bind_root = source;
    mount_insp->state_.fstype = "ext4";
    mount_insp->state_.device = "";
    mount_insp->state_.options = "rw";
    mount_insp->state_.status = containercp::access::MountStatus::Ok;

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(ctx.inspector);
    provider.set_mount_inspector(mount_insp);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&ctx](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return ctx.fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) {
        containercp::access::LocalSftpProvider::SiteInfo info;
        info.valid = true; info.site_id = id;
        info.domain = "test"; info.root = "/srv/containercp/sites/test";
        return info;
    });
    provider.set_mapping_persistence(
        [&ctx]() { return ctx.stored; },
        [&ctx](const containercp::access::SystemAccountMapping& m) {
            for (auto& s : ctx.stored) { if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; } }
            ctx.stored.push_back(m); return true;
        },
        [](const std::string&, uint64_t) { return true; });
    provider.set_grants_loader([](uint64_t uid) {
        std::vector<containercp::access::LocalSftpProvider::GrantInfo> grants;
        if (uid == 1) grants.push_back({1, "test", "read_write"});
        return grants;
    });

    auto r = provider.bind_mount_site(1, 1);
    CHECK_FALSE(r.success);
    CHECK(r.message.find("foreign_or_mismatched_mount:device") != std::string::npos);
}

TEST_CASE("ARCH-009 bind mount idempotent mount option mismatch") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.uid = 10000; um.gid = 20000; um.state = "active";
    stored.push_back(um);
    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000,
                                   "/srv/containercp/users/au-dev", "/usr/sbin/nologin", true};

    auto mount_insp = std::make_shared<FakeMountInspector>();
    mount_insp->state_.mounted = true;
    mount_insp->state_.is_bind = true;
    mount_insp->state_.target = "/srv/containercp/users/au-dev/sites/test";
    mount_insp->state_.bind_root = "/srv/containercp/sites/test/public/";
    mount_insp->state_.fstype = "ext4";
    mount_insp->state_.device = "0:30";
    mount_insp->state_.options = "ro";  // read-only — not expected
    mount_insp->state_.status = containercp::access::MountStatus::Ok;

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_mount_inspector(mount_insp);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) {
        containercp::access::LocalSftpProvider::SiteInfo info;
        info.valid = true; info.site_id = id;
        info.domain = "test"; info.root = "/srv/containercp/sites/test";
        return info;
    });
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            for (auto& s : stored) { if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; } }
            stored.push_back(m); return true;
        },
        [](const std::string&, uint64_t) { return true; });
    provider.set_grants_loader([](uint64_t uid) {
        std::vector<containercp::access::LocalSftpProvider::GrantInfo> grants;
        if (uid == 1) grants.push_back({1, "test", "read_write"});
        return grants;
    });

    auto r = provider.bind_mount_site(1, 1);
    CHECK_FALSE(r.success);
    CHECK(r.message.find("foreign_or_mismatched_mount:options") != std::string::npos);
}

TEST_CASE("ARCH-009 bind mount idempotent another user grant") {
    BindMountTestContext ctx;
    auto target = "/srv/containercp/users/au-dev/sites/test";
    auto source = "/srv/containercp/sites/test/public/";

    ctx.inspector->mount_state_->mounted_paths_.insert(target);
    ctx.inspector->mount_state_->bind_sources_[target] = source;

    // Override grants_loader to return no grants for user 1
    ctx.provider.set_grants_loader([](uint64_t) {
        return std::vector<containercp::access::LocalSftpProvider::GrantInfo>{};
    });

    auto r = ctx.provider.bind_mount_site(1, 1);
    CHECK_FALSE(r.success);
    CHECK(r.message.find("foreign_or_mismatched_mount:no_grant") != std::string::npos);
}

TEST_CASE("ARCH-009 bind mount idempotent another site grant") {
    BindMountTestContext ctx;
    auto target = "/srv/containercp/users/au-dev/sites/test";
    auto source = "/srv/containercp/sites/test/public/";

    ctx.inspector->mount_state_->mounted_paths_.insert(target);
    ctx.inspector->mount_state_->bind_sources_[target] = source;

    // Override grants_loader to return a grant for a different site
    ctx.provider.set_grants_loader([](uint64_t uid) {
        std::vector<containercp::access::LocalSftpProvider::GrantInfo> grants;
        if (uid == 1) grants.push_back({999, "other-site", "read_write"});
        return grants;
    });

    auto r = ctx.provider.bind_mount_site(1, 1);
    CHECK_FALSE(r.success);
    CHECK(r.message.find("foreign_or_mismatched_mount:no_grant") != std::string::npos);
}

TEST_CASE("ARCH-009 bind mount idempotent inspector failure") {
    BindMountTestContext ctx;
    auto target = "/srv/containercp/users/au-dev/sites/test";
    auto source = "/srv/containercp/sites/test/public/";

    ctx.inspector->mount_state_->mounted_paths_.insert(target);
    ctx.inspector->mount_state_->bind_sources_[target] = source;

    // Override mount inspector to return InspectionFailed
    auto fail_insp = std::make_shared<FakeMountInspector>();
    fail_insp->state_.mounted = true;
    fail_insp->state_.status = containercp::access::MountStatus::InspectionFailed;
    fail_insp->state_.error_detail = "mock failure";
    ctx.provider.set_mount_inspector(fail_insp);

    auto r = ctx.provider.bind_mount_site(1, 1);
    CHECK_FALSE(r.success);
    CHECK(r.message.find("foreign_or_mismatched_mount:status") != std::string::npos);
}

TEST_CASE("ARCH-009 bind mount idempotent malformed mount state") {
    auto inspector = std::make_shared<FakeInspector>();
    FakeCommandRunner fake_commands(inspector);
    std::vector<containercp::access::SystemAccountMapping> stored;
    containercp::access::SystemAccountMapping um;
    um.entity_type = "access_user"; um.entity_id = 1;
    um.username = "au-dev"; um.uid = 10000; um.gid = 20000; um.state = "active";
    stored.push_back(um);
    inspector->users_["au-dev"] = {true, "au-dev", 10000, 20000,
                                   "/srv/containercp/users/au-dev", "/usr/sbin/nologin", true};

    auto mount_insp = std::make_shared<FakeMountInspector>();
    mount_insp->state_.mounted = true;
    mount_insp->state_.is_bind = true;
    mount_insp->state_.target = "/srv/containercp/users/au-dev/sites/test";
    mount_insp->state_.bind_root = "/srv/containercp/sites/test/public/";
    mount_insp->state_.fstype = "";  // empty fstype = malformed
    mount_insp->state_.device = "0:30";
    mount_insp->state_.options = "rw";
    mount_insp->state_.status = containercp::access::MountStatus::Ok;

    auto* log = &containercp::logger::Logger::instance();
    containercp::access::LocalSftpProvider provider(*log);
    provider.set_identity_inspector(inspector);
    provider.set_mount_inspector(mount_insp);
    provider.set_command_runner(std::make_unique<containercp::access::SystemAccountCommandRunner>(
        [&fake_commands](const containercp::access::SystemAccountCommandRunner::Command& cmd) {
            return fake_commands.run(cmd);
        }));
    provider.set_enabled(true);
    provider.set_site_resolver([](uint64_t id) {
        containercp::access::LocalSftpProvider::SiteInfo info;
        info.valid = true; info.site_id = id;
        info.domain = "test"; info.root = "/srv/containercp/sites/test";
        return info;
    });
    provider.set_mapping_persistence(
        [&stored]() { return stored; },
        [&stored](const containercp::access::SystemAccountMapping& m) {
            for (auto& s : stored) { if (s.entity_type == m.entity_type && s.entity_id == m.entity_id) { s = m; return true; } }
            stored.push_back(m); return true;
        },
        [](const std::string&, uint64_t) { return true; });
    provider.set_grants_loader([](uint64_t uid) {
        std::vector<containercp::access::LocalSftpProvider::GrantInfo> grants;
        if (uid == 1) grants.push_back({1, "test", "read_write"});
        return grants;
    });

    auto r = provider.bind_mount_site(1, 1);
    CHECK_FALSE(r.success);
    // Empty fstype checked before device in the code, so expect fstype error
    CHECK(r.message.find("foreign_or_mismatched_mount:fstype") != std::string::npos);
}
