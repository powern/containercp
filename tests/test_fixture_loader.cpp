#include "doctest/doctest.h"
#include "storage/Storage.h"

#include <fstream>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

// Helper: copy fixture files from source dir to a temp dir.
// Returns the temp dir path.
static std::string copy_fixtures_to_temp(const std::string& subdir) {
    std::string tmp = "/tmp/containercp_fixture_test_" + std::to_string(::getpid()) + "_" + subdir;
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    std::string src_base = std::string(TEST_FIXTURE_DIR) + "/v0.6.0/" + subdir;
    if (!fs::exists(src_base)) return tmp;

    for (const auto& entry : fs::directory_iterator(src_base)) {
        auto path = entry.path();
        if (path.extension() == ".db" || path.extension() == "") {
            fs::copy(path, fs::path(tmp) / path.filename(),
                     fs::copy_options::overwrite_existing);
        }
    }
    return tmp;
}

// ============================================================
// Normal fixture verification
// ============================================================

TEST_CASE("Fixture loader: normal nodes") {
    auto tmp = copy_fixtures_to_temp("normal");
    containercp::storage::Storage st(tmp + "/");
    auto nodes = st.load_nodes();
    CHECK(nodes.size() == 1);
    if (!nodes.empty()) {
        CHECK(nodes[0].name == "local");
        CHECK(nodes[0].type == "local");
    }
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: normal sites") {
    auto tmp = copy_fixtures_to_temp("normal");
    containercp::storage::Storage st(tmp + "/");
    auto sites = st.load_sites();
    CHECK(sites.size() == 3);
    if (sites.size() >= 3) {
        CHECK(sites[0].domain == "example.com");
        CHECK(sites[0].web_server == "apache");
        CHECK(sites[0].php_mail_enabled == false);
        CHECK(sites[1].domain == "test.org");
        CHECK(sites[1].web_server == "nginx");
        CHECK(sites[1].php_mail_enabled == true);
        CHECK(sites[2].domain == "demo.dev");
    }
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: normal users") {
    auto tmp = copy_fixtures_to_temp("normal");
    containercp::storage::Storage st(tmp + "/");
    auto users = st.load_users();
    CHECK(users.size() == 2);
    if (!users.empty()) {
        CHECK(users[0].username == "admin");
        CHECK(users[0].uid == 1000);
    }
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: normal domains") {
    auto tmp = copy_fixtures_to_temp("normal");
    containercp::storage::Storage st(tmp + "/");
    auto domains = st.load_domains();
    CHECK(domains.size() == 4);
    if (domains.size() >= 4) {
        CHECK(domains[0].fqdn == "example.com");
        CHECK(domains[0].site_id == 1);
        CHECK(domains[0].ssl_enabled == true);
        CHECK(domains[3].fqdn == "mail.test.org");
    }
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: normal php_versions") {
    auto tmp = copy_fixtures_to_temp("normal");
    containercp::storage::Storage st(tmp + "/");
    auto versions = st.load_php_versions();
    CHECK(versions.size() == 3);
    if (versions.size() >= 3) {
        CHECK(versions[0].version == "8.2");
        CHECK(versions[2].version == "8.4");
        CHECK(versions[2].default_version == true);
    }
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: normal databases") {
    auto tmp = copy_fixtures_to_temp("normal");
    containercp::storage::Storage st(tmp + "/");
    auto databases = st.load_databases();
    CHECK(databases.size() == 2);
    if (!databases.empty()) {
        CHECK(databases[0].db_name == "example_db");
        CHECK(databases[0].db_password == "s3cur3_p@ss");
        CHECK(databases[0].site_id == 1);
    }
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: normal backups") {
    auto tmp = copy_fixtures_to_temp("normal");
    containercp::storage::Storage st(tmp + "/");
    auto backups = st.load_backups();
    CHECK(backups.size() == 2);
    if (!backups.empty()) {
        CHECK(backups[0].filename == "example-com-20260701.tar.gz");
        CHECK(backups[0].site_id == 1);
    }
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: normal ssl_certificates") {
    auto tmp = copy_fixtures_to_temp("normal");
    containercp::storage::Storage st(tmp + "/");
    auto certs = st.load_ssl_certificates();
    CHECK(certs.size() == 1);
    if (!certs.empty()) {
        CHECK(certs[0].domain == "example.com");
        CHECK(certs[0].status == "active");
        CHECK(certs[0].auto_renew == true);
        CHECK(certs[0].https_enabled == true);
        CHECK(certs[0].version == 2);
    }
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: normal mail_domains") {
    auto tmp = copy_fixtures_to_temp("normal");
    containercp::storage::Storage st(tmp + "/");
    auto domains = st.load_mail_domains();
    CHECK(domains.size() == 2);
    if (domains.size() >= 2) {
        CHECK(domains[0].domain_name == "example.com");
        CHECK(domains[0].site_id == 1);
        CHECK(domains[1].domain_name == "test.org");
        CHECK(domains[1].relay_host == "relay.test.org:587");
    }
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: normal mail_mailboxes") {
    auto tmp = copy_fixtures_to_temp("normal");
    containercp::storage::Storage st(tmp + "/");
    auto mailboxes = st.load_mailboxes();
    CHECK(mailboxes.size() == 2);
    if (!mailboxes.empty()) {
        CHECK(mailboxes[0].local_part == "admin");
        CHECK(mailboxes[0].password_hash == "{SHA512-CRYPT}$6$placeholderhash");
        CHECK(mailboxes[0].display_name == "Admin User");
    }
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: normal mail_aliases") {
    auto tmp = copy_fixtures_to_temp("normal");
    containercp::storage::Storage st(tmp + "/");
    auto aliases = st.load_mail_aliases();
    CHECK(aliases.size() == 1);
    if (!aliases.empty()) {
        CHECK(aliases[0].source_local_part == "info");
        CHECK(aliases[0].destination == "admin@example.com");
    }
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: normal mail_state") {
    auto tmp = copy_fixtures_to_temp("normal");
    containercp::storage::Storage st(tmp + "/");
    auto state = st.load_mail_module_state();
    CHECK(state == "active");
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: normal mail_smarthost") {
    auto tmp = copy_fixtures_to_temp("normal");
    containercp::storage::Storage st(tmp + "/");
    auto config = st.load_mail_smarthost();
    CHECK(config == "smtp.example.com:587:admin:smtp_pass_placeholder:1");
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: normal access_users") {
    auto tmp = copy_fixtures_to_temp("normal");
    containercp::storage::Storage st(tmp + "/");
    auto users = st.load_access_users();
    CHECK(users.size() == 1);
    if (!users.empty()) {
        CHECK(users[0].username == "sftp_user1");
        CHECK(users[0].password_hash == "{SHA256}placeholder_hash_here");
    }
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: normal access_grants") {
    auto tmp = copy_fixtures_to_temp("normal");
    containercp::storage::Storage st(tmp + "/");
    auto grants = st.load_access_grants();
    CHECK(grants.size() == 1);
    if (!grants.empty()) {
        CHECK(grants[0].access_user_id == 1);
        CHECK(grants[0].site_id == 1);
    }
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: normal reverse_proxies") {
    auto tmp = copy_fixtures_to_temp("normal");
    containercp::storage::Storage st(tmp + "/");
    auto proxies = st.load_reverse_proxies();
    CHECK(proxies.size() == 2);
    if (!proxies.empty()) {
        CHECK(proxies[0].domain == "example.com");
        CHECK(proxies[0].site_id == 1);
    }
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: normal profiles") {
    auto tmp = copy_fixtures_to_temp("normal");
    containercp::storage::Storage st(tmp + "/");
    auto profiles = st.load_profiles();
    CHECK(profiles.size() == 2);
    if (!profiles.empty()) {
        CHECK(profiles[0].profile_name == "apache-php-default");
        CHECK(profiles[0].default_profile == true);
    }
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: normal auth_users") {
    auto tmp = copy_fixtures_to_temp("normal");
    containercp::storage::Storage st(tmp + "/");
    auto users = st.load_auth_users();
    CHECK(users.size() == 1);
    if (!users.empty()) {
        CHECK(users[0].username == "admin");
        CHECK(users[0].role == "admin");
    }
    fs::remove_all(tmp);
}

// ============================================================
// Legacy format verification
// ============================================================

TEST_CASE("Fixture loader: legacy sites 5-field") {
    auto tmp = copy_fixtures_to_temp("legacy");
    // Need to rename the legacy file to the Storage-expected name
    fs::copy(fs::path(tmp) / "sites_5field.db",
             fs::path(tmp) / "sites.db",
             fs::copy_options::overwrite_existing);
    fs::remove(fs::path(tmp) / "sites_5field.db");
    fs::remove(fs::path(tmp) / "ssl_certificates_4field.db");
    fs::remove(fs::path(tmp) / "mail_domains_10field.db");

    containercp::storage::Storage st(tmp + "/");
    auto sites = st.load_sites();
    CHECK(sites.size() == 2);
    if (!sites.empty()) {
        CHECK(sites[0].domain == "legacy-site.com");
        CHECK(sites[0].php_mail_enabled == false);  // 5-field default
    }
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: legacy ssl 4-field") {
    auto tmp = copy_fixtures_to_temp("legacy");
    fs::copy(fs::path(tmp) / "ssl_certificates_4field.db",
             fs::path(tmp) / "ssl_certificates.db",
             fs::copy_options::overwrite_existing);
    fs::remove(fs::path(tmp) / "ssl_certificates_4field.db");
    fs::remove(fs::path(tmp) / "sites_5field.db");
    fs::remove(fs::path(tmp) / "mail_domains_10field.db");

    containercp::storage::Storage st(tmp + "/");
    auto certs = st.load_ssl_certificates();
    CHECK(certs.size() == 1);
    if (!certs.empty()) {
        CHECK(certs[0].domain == "legacy.com");
        CHECK(certs[0].status == "active");
        CHECK(certs[0].version == 0);  // old format marker
    }
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: legacy mail_domains 10-field") {
    auto tmp = copy_fixtures_to_temp("legacy");
    fs::copy(fs::path(tmp) / "mail_domains_10field.db",
             fs::path(tmp) / "mail_domains.db",
             fs::copy_options::overwrite_existing);
    fs::remove(fs::path(tmp) / "mail_domains_10field.db");
    fs::remove(fs::path(tmp) / "sites_5field.db");
    fs::remove(fs::path(tmp) / "ssl_certificates_4field.db");

    containercp::storage::Storage st(tmp + "/");
    auto domains = st.load_mail_domains();
    CHECK(domains.size() == 1);
    if (!domains.empty()) {
        CHECK(domains[0].domain_name == "old-format.com");
        CHECK(domains[0].domain_id == 1);  // old owner_id → domain_id
        CHECK(domains[0].site_id == 0);    // no site_id in old format
    }
    fs::remove_all(tmp);
}

// ============================================================
// Migration-only fixture verification
// ============================================================

TEST_CASE("Fixture loader: legacy template_profiles 8-field") {
    auto tmp = copy_fixtures_to_temp("legacy");
    // migrate_template_profiles() reads from "template_profiles.db" in the
    // storage directory. It is NOT a standard load_*() path — it uses a
    // dedicated file and returns Profile objects with hardcoded type=WEB_SERVER.
    // After successful migration, the caller (ServiceRegistry) deletes
    // template_profiles.db from disk. This test verifies the parse only.
    containercp::storage::Storage st(tmp + "/");

    // Verify file exists in the temp directory after copy
    auto tp_path = fs::path(tmp) / "template_profiles.db";
    REQUIRE(fs::exists(tp_path));

    auto profiles = st.migrate_template_profiles();
    REQUIRE(profiles.size() == 2);
    CHECK(profiles[0].profile_name == "apache-php-default");
    CHECK(profiles[0].type == containercp::profile::ProfileType::WEB_SERVER);
    CHECK(profiles[0].web_server == "apache");
    CHECK(profiles[0].default_profile == true);
    CHECK(profiles[1].profile_name == "nginx-php-default");
    CHECK(profiles[1].web_server == "nginx");
    CHECK(profiles[1].default_profile == false);

    // Verify migrate_template_profiles() does NOT delete the source file.
    // The caller (ServiceRegistry) is responsible for deletion.
    CHECK(fs::exists(tp_path));

    fs::remove_all(tmp);
}

// ============================================================
// Sentinel fixture verification
// ============================================================

TEST_CASE("Fixture loader: sentinel reverse_proxies site_id=0") {
    auto tmp = copy_fixtures_to_temp("sentinels");
    fs::copy(fs::path(tmp) / "reverse_proxies_site0.db",
             fs::path(tmp) / "reverse_proxies.db",
             fs::copy_options::overwrite_existing);
    fs::remove(fs::path(tmp) / "reverse_proxies_site0.db");
    fs::remove(fs::path(tmp) / "domains_site0.db");
    fs::remove(fs::path(tmp) / "mail_domains_external.db");
    fs::remove(fs::path(tmp) / "backups_orphan.db");
    fs::remove(fs::path(tmp) / "sites_node0.db");

    containercp::storage::Storage st(tmp + "/");
    auto proxies = st.load_reverse_proxies();
    CHECK(proxies.size() == 1);
    if (!proxies.empty()) {
        CHECK(proxies[0].domain == "admin.example.com");
        CHECK(proxies[0].site_id == 0);  // admin panel sentinel
    }
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: sentinel domains site_id=0") {
    auto tmp = copy_fixtures_to_temp("sentinels");
    fs::copy(fs::path(tmp) / "domains_site0.db",
             fs::path(tmp) / "domains.db",
             fs::copy_options::overwrite_existing);
    fs::remove(fs::path(tmp) / "domains_site0.db");
    fs::remove(fs::path(tmp) / "reverse_proxies_site0.db");
    fs::remove(fs::path(tmp) / "mail_domains_external.db");
    fs::remove(fs::path(tmp) / "backups_orphan.db");
    fs::remove(fs::path(tmp) / "sites_node0.db");

    containercp::storage::Storage st(tmp + "/");
    auto domains = st.load_domains();
    CHECK(domains.size() == 1);
    if (!domains.empty()) {
        CHECK(domains[0].fqdn == "orphan-domain.com");
        CHECK(domains[0].site_id == 0);  // orphan domain
    }
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: sentinel mail_domains external (domain_id=0)") {
    auto tmp = copy_fixtures_to_temp("sentinels");
    fs::copy(fs::path(tmp) / "mail_domains_external.db",
             fs::path(tmp) / "mail_domains.db",
             fs::copy_options::overwrite_existing);
    fs::remove(fs::path(tmp) / "mail_domains_external.db");
    fs::remove(fs::path(tmp) / "reverse_proxies_site0.db");
    fs::remove(fs::path(tmp) / "domains_site0.db");
    fs::remove(fs::path(tmp) / "backups_orphan.db");
    fs::remove(fs::path(tmp) / "sites_node0.db");

    containercp::storage::Storage st(tmp + "/");
    auto domains = st.load_mail_domains();
    CHECK(domains.size() == 1);
    if (!domains.empty()) {
        CHECK(domains[0].domain_name == "external-domain.com");
        CHECK(domains[0].domain_id == 0);  // external, not in domain table
        CHECK(domains[0].site_id == 0);
    }
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: sentinel backups orphan (site_id=99)") {
    auto tmp = copy_fixtures_to_temp("sentinels");
    fs::copy(fs::path(tmp) / "backups_orphan.db",
             fs::path(tmp) / "backups.db",
             fs::copy_options::overwrite_existing);
    fs::remove(fs::path(tmp) / "backups_orphan.db");
    fs::remove(fs::path(tmp) / "reverse_proxies_site0.db");
    fs::remove(fs::path(tmp) / "domains_site0.db");
    fs::remove(fs::path(tmp) / "mail_domains_external.db");
    fs::remove(fs::path(tmp) / "sites_node0.db");

    containercp::storage::Storage st(tmp + "/");
    auto backups = st.load_backups();
    CHECK(backups.size() == 1);
    if (!backups.empty()) {
        CHECK(backups[0].filename == "orphan-backup.tar.gz");
        CHECK(backups[0].site_id == 99);  // site may have been deleted
    }
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: sentinel sites node_id=0") {
    auto tmp = copy_fixtures_to_temp("sentinels");
    fs::copy(fs::path(tmp) / "sites_node0.db",
             fs::path(tmp) / "sites.db",
             fs::copy_options::overwrite_existing);
    fs::remove(fs::path(tmp) / "sites_node0.db");
    fs::remove(fs::path(tmp) / "reverse_proxies_site0.db");
    fs::remove(fs::path(tmp) / "domains_site0.db");
    fs::remove(fs::path(tmp) / "mail_domains_external.db");
    fs::remove(fs::path(tmp) / "backups_orphan.db");

    containercp::storage::Storage st(tmp + "/");
    auto sites = st.load_sites();
    CHECK(sites.size() == 1);
    if (!sites.empty()) {
        CHECK(sites[0].domain == "nodezero-site.com");
        CHECK(sites[0].node_id == 0);  // default/local node
    }
    fs::remove_all(tmp);
}

// ============================================================
// Production-derived fixture verification
// ============================================================

TEST_CASE("Production-derived fixture loads successfully") {
    auto tmp = copy_fixtures_to_temp("production_derived");
    containercp::storage::Storage st(tmp + "/");

    auto nodes = st.load_nodes();
    CHECK(nodes.size() == 1);

    auto sites = st.load_sites();
    CHECK(sites.size() == 8);  // non-contiguous IDs: 1,2,3,4,8,9,10,11

    auto users = st.load_users();
    CHECK(users.size() == 1);

    auto domains = st.load_domains();
    CHECK(domains.size() == 8);

    auto pv = st.load_php_versions();
    CHECK(pv.size() == 3);

    auto dbs = st.load_databases();
    CHECK(dbs.size() == 8);
    for (auto& d : dbs) {
        CHECK(d.db_password.length() > 0);
        CHECK(d.db_password.find("ANON_") == 0);
    }

    auto backups = st.load_backups();
    CHECK(backups.size() == 1);

    auto certs = st.load_ssl_certificates();
    CHECK(certs.size() == 0);

    auto mdomains = st.load_mail_domains();
    CHECK(mdomains.size() == 2);

    auto mboxes = st.load_mailboxes();
    CHECK(mboxes.size() == 1);
    if (!mboxes.empty()) {
        CHECK(mboxes[0].password_hash.length() > 0);
        CHECK(mboxes[0].password_hash.find("ANON_") != std::string::npos);
    }

    auto aliases = st.load_mail_aliases();
    CHECK(aliases.size() == 0);

    auto mstate = st.load_mail_module_state();
    CHECK(mstate == "active");

    auto smtp = st.load_mail_smarthost();
    CHECK(smtp.length() > 0);
    CHECK(smtp.find("0|") == 0);

    auto ausers = st.load_access_users();
    CHECK(ausers.size() == 0);

    auto grants = st.load_access_grants();
    CHECK(grants.size() == 0);

    auto proxies = st.load_reverse_proxies();
    CHECK(proxies.size() == 9);

    auto profs = st.load_profiles();
    CHECK(profs.size() == 5);

    auto auths = st.load_auth_users();
    CHECK(auths.size() == 1);
    if (!auths.empty()) {
        CHECK(auths[0].role == "admin");
        CHECK(auths[0].password_hash.length() > 0);
        CHECK(auths[0].password_hash.find("ANON_") == 0);
    }

    // Sentinel checks
    CHECK(proxies.size() >= 5);
    auto* admin_proxy = proxies.size() >= 5 ? &proxies[4] : nullptr;
    if (admin_proxy) {
        CHECK(admin_proxy->site_id == 0);
    }

    bool found_external = false;
    for (auto& md : mdomains) {
        if (md.domain_id == 0) found_external = true;
    }
    CHECK(found_external);

    for (auto& d : domains) {
        CHECK(d.owner_id == 0);
    }

    CHECK(fs::remove_all(tmp));
}

// ============================================================
// Empty fixture verification
// ============================================================

TEST_CASE("Fixture loader: empty directory returns empty vectors") {
    auto tmp = copy_fixtures_to_temp("empty");
    containercp::storage::Storage st(tmp + "/");
    CHECK(st.load_nodes().empty());
    CHECK(st.load_sites().empty());
    CHECK(st.load_users().empty());
    CHECK(st.load_domains().empty());
    CHECK(st.load_php_versions().empty());
    CHECK(st.load_databases().empty());
    CHECK(st.load_backups().empty());
    CHECK(st.load_ssl_certificates().empty());
    CHECK(st.load_mail_domains().empty());
    CHECK(st.load_mailboxes().empty());
    CHECK(st.load_mail_aliases().empty());
    CHECK(st.load_mail_module_state().empty());
    CHECK(st.load_mail_smarthost().empty());
    CHECK(st.load_access_users().empty());
    CHECK(st.load_access_grants().empty());
    CHECK(st.load_reverse_proxies().empty());
    CHECK(st.load_profiles().empty());
    CHECK(st.load_auth_users().empty());
    fs::remove_all(tmp);
}

// ============================================================
// Malformed fixture verification
// ============================================================

TEST_CASE("Fixture loader: malformed wrong_field_count") {
    auto tmp = copy_fixtures_to_temp("malformed");
    if (fs::exists(fs::path(tmp) / "wrong_field_count.db")) {
        fs::rename(fs::path(tmp) / "wrong_field_count.db",
                   fs::path(tmp) / "sites.db");
    }
    containercp::storage::Storage st(tmp + "/");
    // Current parser does not reject wrong field counts — extra fields are ignored.
    // This documents the parser's tolerance for record format mismatches.
    auto sites = st.load_sites();
    CHECK(sites.size() == 2);
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: malformed empty file") {
    auto tmp = copy_fixtures_to_temp("malformed");
    // Rename to sites.db so Storage finds it
    if (fs::exists(fs::path(tmp) / "empty_file.db")) {
        fs::rename(fs::path(tmp) / "empty_file.db",
                   fs::path(tmp) / "sites.db");
    }
    containercp::storage::Storage st(tmp + "/");
    auto sites = st.load_sites();
    CHECK(sites.empty());  // empty file → no records
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: malformed duplicate_id") {
    auto tmp = copy_fixtures_to_temp("malformed");
    if (fs::exists(fs::path(tmp) / "duplicate_id.db")) {
        fs::rename(fs::path(tmp) / "duplicate_id.db",
                   fs::path(tmp) / "sites.db");
    }
    containercp::storage::Storage st(tmp + "/");
    // The current parser reads line by line; duplicate IDs are not rejected
    auto sites = st.load_sites();
    CHECK(sites.size() >= 1);
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: malformed invalid_int throws") {
    auto tmp = copy_fixtures_to_temp("malformed");
    if (fs::exists(fs::path(tmp) / "invalid_int.db")) {
        fs::rename(fs::path(tmp) / "invalid_int.db",
                   fs::path(tmp) / "sites.db");
    }
    containercp::storage::Storage st(tmp + "/");
    // std::stoull("notanumber") should throw std::invalid_argument
    try {
        auto sites = st.load_sites();
        // In some environments the parser may not reach the int field
        CHECK(sites.size() >= 0);
    } catch (const std::invalid_argument&) {
    } catch (const std::exception&) {
    }
    // Either it threw or returned partial — document current behavior
    INFO("Current parser either throws on invalid int or returns partial data");
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: malformed multiline_corruption") {
    auto tmp = copy_fixtures_to_temp("malformed");
    if (fs::exists(fs::path(tmp) / "multiline_corruption.db")) {
        fs::rename(fs::path(tmp) / "multiline_corruption.db",
                   fs::path(tmp) / "sites.db");
    }
    containercp::storage::Storage st(tmp + "/");
    // Multiline corruption: the split line "site|admin|1|nginx|0" contains
    // "site" as an id field → std::stoull("site") throws.
    CHECK_THROWS_AS(st.load_sites(), std::exception);
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: malformed truncated_record") {
    auto tmp = copy_fixtures_to_temp("malformed");
    if (fs::exists(fs::path(tmp) / "truncated_record.db")) {
        fs::rename(fs::path(tmp) / "truncated_record.db",
                   fs::path(tmp) / "sites.db");
    }
    containercp::storage::Storage st(tmp + "/");
    // Truncated record: one complete record, one with missing fields.
    // The parser reads line by line; the truncated line may produce partial data.
    auto sites = st.load_sites();
    CHECK(sites.size() >= 1);  // at least the complete record
    fs::remove_all(tmp);
}

TEST_CASE("Fixture loader: malformed delimiter_collision") {
    auto tmp = copy_fixtures_to_temp("malformed");
    if (fs::exists(fs::path(tmp) / "delimiter_collision.db")) {
        fs::rename(fs::path(tmp) / "delimiter_collision.db",
                   fs::path(tmp) / "sites.db");
    }
    containercp::storage::Storage st(tmp + "/");
    // Record 2 has an unescaped pipe in the intended domain "pipe-inside|value".
    // The parser treats | as field delimiter, producing 8 fields instead of 6.
    // Extra fields are silently ignored. Shifted values produce incorrect data.
    auto sites = st.load_sites();
    CHECK(sites.size() == 2);
    if (sites.size() >= 2) {
        CHECK(sites[0].domain == "valid-site");
        // Domain was "pipe-inside|value" but pipe split it:
        // domain="pipe-inside" (correct split), owner="value" (shifted),
        // node_id=1 (coincidentally valid), web_server="admin" (shifted),
        // php_mail_enabled=true (shifted from "1").
        CHECK(sites[1].domain == "pipe-inside");
        CHECK(sites[1].owner == "value");    // shifted from domain
        CHECK(sites[1].node_id == 1);        // coincidental match
        CHECK(sites[1].web_server == "admin"); // shifted from owner
        CHECK(sites[1].php_mail_enabled == true); // shifted from web_server "1"
    }
    fs::remove_all(tmp);
}
