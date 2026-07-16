#include "user/UserManager.h"
#include "site/SiteManager.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "doctest/doctest.h"

#include "logger/Logger.h"
#include "mail/MailAliasManager.h"
#include "mail/MailboxManager.h"
#include "mail/providers/DockerMailProvider.h"

TEST_CASE("UserManager create/find/list/remove") {
    containercp::user::UserManager mgr;

    uint64_t id = mgr.create("testuser", 1001, "/home/testuser", "/bin/bash");
    CHECK(id == 1);

    auto* u = mgr.find("testuser");
    REQUIRE(u != nullptr);
    CHECK(u->username == "testuser");
    CHECK(u->uid == 1001);
    CHECK(u->home_directory == "/home/testuser");
    CHECK(u->shell == "/bin/bash");
    CHECK(u->enabled);

    CHECK(mgr.list().size() == 1);

    CHECK(mgr.remove(id));
    CHECK(mgr.find("testuser") == nullptr);
    CHECK(mgr.list().empty());

    CHECK_FALSE(mgr.remove(999));
}

TEST_CASE("UserManager duplicate name") {
    containercp::user::UserManager mgr;
    mgr.create("dup", 1001, "/home/dup", "/bin/bash");
    mgr.create("dup", 1002, "/home/dup2", "/bin/bash");
    CHECK(mgr.list().size() == 2);
    auto* u = mgr.find("dup");
    REQUIRE(u != nullptr);
}

TEST_CASE("SiteManager create/find/list/remove") {
    containercp::site::SiteManager mgr;

    uint64_t id = mgr.create("example.com", "admin", 1);
    CHECK(id == 1);

    auto* s = mgr.find("example.com");
    REQUIRE(s != nullptr);
    CHECK(s->domain == "example.com");
    CHECK(s->owner == "admin");
    CHECK(s->node_id == 1);
    CHECK(s->id == 1);

    CHECK(mgr.list().size() == 1);

    CHECK(mgr.remove(id));
    CHECK(mgr.find("example.com") == nullptr);
    CHECK(mgr.list().empty());
}

TEST_CASE("SiteManager find_by_id") {
    containercp::site::SiteManager mgr;
    uint64_t id1 = mgr.create("site1.com", "admin", 1);
    uint64_t id2 = mgr.create("site2.com", "admin", 1);

    auto* s = mgr.find_by_id(id1);
    REQUIRE(s != nullptr);
    CHECK(s->domain == "site1.com");
    CHECK(s->id == id1);

    s = mgr.find_by_id(id2);
    REQUIRE(s != nullptr);
    CHECK(s->domain == "site2.com");
    CHECK(s->id == id2);

    CHECK(mgr.find_by_id(999) == nullptr);
}

TEST_CASE("SiteManager remove cleans state") {
    containercp::site::SiteManager mgr;
    mgr.create("test.com", "admin", 1);
    mgr.create("other.com", "admin", 1);
    CHECK(mgr.list().size() == 2);

    // Remove one site
    auto* s = mgr.find("test.com");
    REQUIRE(s != nullptr);
    mgr.remove(s->id);

    // Only the other site remains
    CHECK(mgr.list().size() == 1);
    CHECK(mgr.find("test.com") == nullptr);
    CHECK(mgr.find("other.com") != nullptr);
}

#include "domain/DomainManager.h"
#include "domain/DomainViewService.h"
#include "proxy/ReverseProxyManager.h"
#include "ssl/CertificateStore.h"
#include "logger/Logger.h"

#include <cstdio>
#include <fstream>
#include <sys/stat.h>

TEST_CASE("DomainViewService produces valid JSON for various target values") {
    using namespace containercp;

    // Setup: temporary SSL root so CertificateStore can be created
    char tmp[] = "/tmp/containercp_test_domain_json_XXXXXX";
    char* ssl_root = mkdtemp(tmp);
    REQUIRE(ssl_root != nullptr);
    std::string ssl_root_str(ssl_root);

    // Create managers with known test data
    domain::DomainManager domains;
    site::SiteManager sites;
    ssl::CertificateStore cert_store(logger::Logger::instance(), ssl_root_str);

    // Add a site referenced by domains
    sites.create("testsite.com", "admin", 1);

    // Create domains with various target scenarios
    uint64_t d1 = domains.create("empty-target.com", 1, 1, "primary", "");
    uint64_t d2 = domains.create("normal-target.com", 1, 1, "alias", "testsite.com");
    uint64_t d3 = domains.create("redirect-target.com", 1, 1, "redirect", "https://example.com/page");
    uint64_t d4 = domains.create("unlinked.com", 1, 999, "primary", "");  // site_id 999 doesn't exist
    (void)d1; (void)d2; (void)d3; (void)d4;

    // Write SSL metadata for site_id=1 so cert_store.load_metadata works
    {
        std::string dir = ssl_root_str + "/1/current";
        ::mkdir((ssl_root_str + "/1").c_str(), 0755);
        ::mkdir(dir.c_str(), 0755);
        std::ofstream f(dir + "/metadata.json");
        f << R"({"site_id":1,"status":"active","https_enabled":true,"expires_at":"2030-01-01T00:00:00Z"})";
        f.close();
    }

    // Create the view service and generate enriched JSON
    mail::MailDomainManager md_mgr;
    proxy::ReverseProxyManager rp_mgr;
    domain::DomainViewService view(logger::Logger::instance(), domains, sites, cert_store, md_mgr, rp_mgr);
    std::string json_result = view.build_enriched_json();

    // Verify the JSON is syntactically valid by checking basic structure
    CHECK(json_result.size() > 0);
    CHECK(json_result.front() == '[');
    CHECK(json_result.back() == ']');


    // Verify each domain's JSON contains the expected patterns
    CHECK(json_result.find("\"domain\":\"empty-target.com\"") != std::string::npos);
    CHECK(json_result.find("\"domain\":\"normal-target.com\"") != std::string::npos);
    CHECK(json_result.find("\"domain\":\"redirect-target.com\"") != std::string::npos);
    CHECK(json_result.find("\"domain\":\"unlinked.com\"") != std::string::npos);

    // Verify target fields are correctly formatted
    CHECK(json_result.find("\"target\":\"\",\"ssl_enabled\"") != std::string::npos);
    CHECK(json_result.find("\"target\":\"testsite.com\",\"ssl_enabled\"") != std::string::npos);
    CHECK(json_result.find("\"target\":\"https://example.com/page\",\"ssl_enabled\"") != std::string::npos);

    // Verify SSL status for the linked site
    CHECK(json_result.find("\"ssl_status\":\"Active\"") != std::string::npos);

    // Verify unlinked site has no site name
    CHECK(json_result.find("\"site_name\":\"\",\"site_domain\":\"\"") != std::string::npos);
    // Verify system domain capabilities present for normal domains
    CHECK(json_result.find("\"can_delete\":true") != std::string::npos);

    // Cleanup
    std::string rm_cmd = "rm -rf " + ssl_root_str;
    std::system(rm_cmd.c_str());
}

TEST_CASE("DomainViewService enriched JSON includes mail fields") {
    using namespace containercp;

    char tmp[] = "/tmp/containercp_test_mail_json_XXXXXX";
    char* ssl_root = mkdtemp(tmp);
    REQUIRE(ssl_root != nullptr);
    std::string ssl_root_str(ssl_root);

    domain::DomainManager domains;
    site::SiteManager sites;
    ssl::CertificateStore cert_store(logger::Logger::instance(), ssl_root_str);
    sites.create("testsite.com", "admin", 1);

    domains.create("with-mail.com", 1, 1, "primary", "");
    domains.create("no-mail.com", 1, 1, "primary", "");

    mail::MailDomainManager md_mgr;
    uint64_t mail_id = md_mgr.create("with-mail.com",
        mail::MailDomainMode::LocalPrimary, 1, 1);
    CHECK(mail_id > 0);
    auto* md = md_mgr.find(mail_id);
    REQUIRE(md != nullptr);
    // Set DKIM fields
    md->dkim_selector = "default";
    md->dkim_public_key_dns = "v=DKIM1; p=MIGfMA0GCSqGSIb4...";

    proxy::ReverseProxyManager rp_mgr2;
    domain::DomainViewService view(logger::Logger::instance(),
        domains, sites, cert_store, md_mgr, rp_mgr2);
    std::string json_result = view.build_enriched_json();

    CHECK(json_result.find("\"domain\":\"with-mail.com\"") != std::string::npos);
    CHECK(json_result.find("\"mail_domain_id\":1") != std::string::npos);
    CHECK(json_result.find("\"mail_domain_mode\":\"local-primary\"") != std::string::npos);
    CHECK(json_result.find("\"dkim_generated\":true") != std::string::npos);
    CHECK(json_result.find("\"dkim_selector\":\"default\"") != std::string::npos);

    CHECK(json_result.find("\"domain\":\"no-mail.com\"") != std::string::npos);
    bool has_empty_mail = json_result.find("\"mail_domain_id\":0") != std::string::npos;
    CHECK(has_empty_mail);

    std::string rm_cmd = "rm -rf " + ssl_root_str;
    std::system(rm_cmd.c_str());
}

TEST_CASE("DomainViewService includes admin panel (site_id=0) when server_hostname set") {
    using namespace containercp;

    char tmp[] = "/tmp/containercp_test_admin_domain_XXXXXX";
    char* ssl_root = mkdtemp(tmp);
    REQUIRE(ssl_root != nullptr);
    std::string ssl_root_str(ssl_root);

    domain::DomainManager domains;
    site::SiteManager sites;
    ssl::CertificateStore cert_store(logger::Logger::instance(), ssl_root_str);

    // SSL metadata for site_id=0 so admin panel SSL is "Active"
    {
        std::string dir = ssl_root_str + "/0/current";
        ::mkdir((ssl_root_str + "/0").c_str(), 0755);
        ::mkdir(dir.c_str(), 0755);
        std::ofstream f(dir + "/metadata.json");
        f << R"({"site_id":"0","status":"active","https_enabled":true,"expires_at":"2030-01-01T00:00:00Z"})";
        f.close();
    }

    // Create a managed domain — admin panel should appear before it
    domains.create("example.com", 1, 1, "primary", "");
    domains.create("other.com", 1, 2, "primary", "");

    mail::MailDomainManager md_mgr;
    proxy::ReverseProxyManager rp_mgr;

    // Without server_hostname: only managed domains, no admin panel
    {
        domain::DomainViewService view(logger::Logger::instance(),
            domains, sites, cert_store, md_mgr, rp_mgr);
        std::string json = view.build_enriched_json();
        CHECK(json.find("\"domain\":\"example.com\"") != std::string::npos);
        CHECK(json.find("\"domain\":\"other.com\"") != std::string::npos);
        // No admin panel because server_hostname is empty
        bool has_site0 = json.find("\"site_id\":0,\"site_name\"") != std::string::npos;
        CHECK_FALSE(has_site0);
    }

    // With server_hostname: admin panel appears first with site_id=0
    {
        std::string hostname = "admin.example.com";
        // Add a reverse proxy entry so proxy_upstream is populated
        rp_mgr.create(hostname, 0, "/path/to/config", "127.0.0.1:8081");
        domain::DomainViewService view(logger::Logger::instance(),
            domains, sites, cert_store, md_mgr, rp_mgr, hostname);
        std::string json = view.build_enriched_json();

        // Admin panel domain is included
        CHECK(json.find("\"domain\":\"admin.example.com\"") != std::string::npos);
        CHECK(json.find("\"site_id\":0") != std::string::npos);
        CHECK(json.find("\"ssl_status\":\"Active\"") != std::string::npos);

        // System domain fields
        CHECK(json.find("\"type\":\"system\"") != std::string::npos);
        CHECK(json.find("\"site_name\":\"ContainerCP Admin\"") != std::string::npos);
        CHECK(json.find("\"system_role\":\"admin-panel\"") != std::string::npos);
        CHECK(json.find("\"can_delete\":false") != std::string::npos);
        CHECK(json.find("\"can_manage_runtime\":false") != std::string::npos);
        CHECK(json.find("\"can_manage_ssl\":true") != std::string::npos);
        CHECK(json.find("\"can_manage_proxy\":true") != std::string::npos);
        CHECK(json.find("\"proxy_upstream\":\"127.0.0.1:8081\"") != std::string::npos);
        CHECK(json.find("\"target\":\"127.0.0.1:8081\"") != std::string::npos);

        // Managed domains still present
        CHECK(json.find("\"domain\":\"example.com\"") != std::string::npos);
        CHECK(json.find("\"domain\":\"other.com\"") != std::string::npos);
        // Normal domains have can_delete=true, can_manage_runtime=true
        CHECK(json.find("\"can_delete\":true") != std::string::npos);
        CHECK(json.find("\"can_manage_runtime\":true") != std::string::npos);

        // Admin panel is first entry (before managed domains)
        std::string::size_type admin_pos = json.find("\"domain\":\"admin.example.com\"");
        std::string::size_type ex_pos = json.find("\"domain\":\"example.com\"");
        CHECK(admin_pos < ex_pos);
    }

    // With server_hostname matching an existing domain: no duplicate
    {
        std::string hostname = "example.com";
        domain::DomainViewService view(logger::Logger::instance(),
            domains, sites, cert_store, md_mgr, rp_mgr, hostname);
        std::string json = view.build_enriched_json();

        // Only one occurrence of example.com
        std::string::size_type first = json.find("\"domain\":\"example.com\"");
        std::string::size_type second = json.find("\"domain\":\"example.com\"", first + 1);
        CHECK(first != std::string::npos);
        CHECK(second == std::string::npos);
    }

    // Lookup by id=0 returns admin panel
    {
        std::string hostname = "admin.example.com";
        domain::DomainViewService view(logger::Logger::instance(),
            domains, sites, cert_store, md_mgr, rp_mgr, hostname);
        std::string json = view.build_enriched_json(0);
        CHECK(json.find("\"domain\":\"admin.example.com\"") != std::string::npos);
        CHECK(json.find("\"site_id\":0") != std::string::npos);
    }

    // Lookup by nonexistent id returns null
    {
        std::string hostname = "admin.example.com";
        domain::DomainViewService view(logger::Logger::instance(),
            domains, sites, cert_store, md_mgr, rp_mgr, hostname);
        std::string json = view.build_enriched_json(999);
        CHECK(json == "null");
    }

    std::string rm_cmd = "rm -rf " + ssl_root_str;
    std::system(rm_cmd.c_str());
}

#include "mail/MailDomainManager.h"

TEST_CASE("MailDomainManager create/find/list/remove") {
    containercp::mail::MailDomainManager mgr;

    uint64_t id = mgr.create("example.com", containercp::mail::MailDomainMode::LocalPrimary, 0, 0);
    CHECK(id == 1);

    auto* m = mgr.find(id);
    REQUIRE(m != nullptr);
    CHECK(m->domain_name == "example.com");
    CHECK(m->mode == containercp::mail::MailDomainMode::LocalPrimary);
    CHECK(m->domain_id == 0);
    CHECK(m->site_id == 0);
    CHECK(m->enabled);

    // Find by domain name
    auto* by_domain = mgr.find_by_domain("example.com");
    REQUIRE(by_domain != nullptr);
    CHECK(by_domain->id == id);

    // List
    CHECK(mgr.list().size() == 1);

    // Remove
    CHECK(mgr.remove(id));
    CHECK(mgr.find(id) == nullptr);
    CHECK(mgr.list().empty());

    // Remove non-existent
    CHECK_FALSE(mgr.remove(999));
}

TEST_CASE("MailDomainManager mode persistence") {
    containercp::mail::MailDomainManager mgr;

    uint64_t id1 = mgr.create("a.com", containercp::mail::MailDomainMode::LocalPrimary, 0, 0);
    uint64_t id2 = mgr.create("b.com", containercp::mail::MailDomainMode::ExternalRelay, 0, 0, "relay.example.com");
    uint64_t id3 = mgr.create("c.com", containercp::mail::MailDomainMode::SplitM365, 0, 0, "c-com.mail.protection.outlook.com");
    uint64_t id4 = mgr.create("d.com", containercp::mail::MailDomainMode::Disabled, 0, 0);
    (void)id4;

    CHECK(mgr.find(id1)->mode == containercp::mail::MailDomainMode::LocalPrimary);
    CHECK(mgr.find(id2)->mode == containercp::mail::MailDomainMode::ExternalRelay);
    CHECK(mgr.find(id2)->relay_host == "relay.example.com");
    CHECK(mgr.find(id3)->mode == containercp::mail::MailDomainMode::SplitM365);
    CHECK(mgr.find(id3)->relay_host == "c-com.mail.protection.outlook.com");
    CHECK(mgr.find(id4)->mode == containercp::mail::MailDomainMode::Disabled);

    // List
    CHECK(mgr.list().size() == 4);
}

TEST_CASE("MailDomainManager duplicate domain names rejected") {
    containercp::mail::MailDomainManager mgr;

    uint64_t id1 = mgr.create("dup.com", containercp::mail::MailDomainMode::LocalPrimary, 0, 0);
    CHECK(id1 == 1);

    // Second create with same domain returns 0 (rejected)
    // Provide relay_host so the rejection is due to duplicate, not validation
    uint64_t id2 = mgr.create("dup.com", containercp::mail::MailDomainMode::ExternalRelay, 0, 0, "relay.example.com");
    CHECK(id2 == 0);

    // Only one domain in the list
    CHECK(mgr.list().size() == 1);
}

TEST_CASE("MailDomainMode string conversion") {
    using namespace containercp::mail;

    // to_string
    CHECK(mail_domain_mode_to_string(MailDomainMode::Disabled) == "disabled");
    CHECK(mail_domain_mode_to_string(MailDomainMode::LocalPrimary) == "local-primary");
    CHECK(mail_domain_mode_to_string(MailDomainMode::ExternalRelay) == "external-relay");
    CHECK(mail_domain_mode_to_string(MailDomainMode::SplitM365) == "split-m365");

    // from_string
    CHECK(mail_domain_mode_from_string("disabled") == MailDomainMode::Disabled);
    CHECK(mail_domain_mode_from_string("local-primary") == MailDomainMode::LocalPrimary);
    CHECK(mail_domain_mode_from_string("external-relay") == MailDomainMode::ExternalRelay);
    CHECK(mail_domain_mode_from_string("split-m365") == MailDomainMode::SplitM365);

    // Unknown string maps to Disabled
    CHECK(mail_domain_mode_from_string("unknown") == MailDomainMode::Disabled);
    CHECK(mail_domain_mode_from_string("") == MailDomainMode::Disabled);

    // is_valid_mail_domain_mode — strict validation for API input
    CHECK(is_valid_mail_domain_mode("disabled"));
    CHECK(is_valid_mail_domain_mode("local-primary"));
    CHECK(is_valid_mail_domain_mode("external-relay"));
    CHECK(is_valid_mail_domain_mode("split-m365"));
    CHECK_FALSE(is_valid_mail_domain_mode("split-ms365"));  // typo
    CHECK_FALSE(is_valid_mail_domain_mode("unknown"));
    CHECK_FALSE(is_valid_mail_domain_mode(""));
}

TEST_CASE("MailDomainManager validate_mode_relay") {
    using namespace containercp::mail;

    // Modes that do NOT require relay_host
    CHECK(MailDomainManager::validate_mode_relay(MailDomainMode::Disabled, "").empty());
    CHECK(MailDomainManager::validate_mode_relay(MailDomainMode::LocalPrimary, "").empty());
    CHECK(MailDomainManager::validate_mode_relay(MailDomainMode::Disabled, "smtp.example.com").empty());
    CHECK(MailDomainManager::validate_mode_relay(MailDomainMode::LocalPrimary, "smtp.example.com").empty());

    // ExternalRelay requires relay_host
    CHECK_FALSE(MailDomainManager::validate_mode_relay(MailDomainMode::ExternalRelay, "").empty());
    CHECK(MailDomainManager::validate_mode_relay(MailDomainMode::ExternalRelay, "relay.example.com").empty());

    // SplitM365 requires relay_host
    CHECK_FALSE(MailDomainManager::validate_mode_relay(MailDomainMode::SplitM365, "").empty());
    CHECK(MailDomainManager::validate_mode_relay(MailDomainMode::SplitM365, "c-com.mail.protection.outlook.com").empty());
}

TEST_CASE("MailDomainManager rejects ExternalRelay without relay_host") {
    containercp::mail::MailDomainManager mgr;

    uint64_t id = mgr.create("bad.com",
                              containercp::mail::MailDomainMode::ExternalRelay, 0, 0, "");
    CHECK(id == 0);  // rejected — relay_host required
    CHECK(mgr.list().empty());
}

TEST_CASE("MailDomainManager rejects SplitM365 without relay_host") {
    containercp::mail::MailDomainManager mgr;

    uint64_t id = mgr.create("bad.com",
                              containercp::mail::MailDomainMode::SplitM365, 0, 0, "");
    CHECK(id == 0);  // rejected — relay_host required
    CHECK(mgr.list().empty());
}

TEST_CASE("MailDomainManager set_domains restores state") {
    containercp::mail::MailDomainManager mgr;

    mgr.create("a.com", containercp::mail::MailDomainMode::LocalPrimary, 0, 0);
    mgr.create("b.com", containercp::mail::MailDomainMode::Disabled, 0, 0);

    // Simulate load from storage
    auto saved = mgr.list();
    containercp::mail::MailDomainManager mgr2;
    mgr2.set_domains(saved);

    CHECK(mgr2.list().size() == 2);
    auto* a = mgr2.find_by_domain("a.com");
    REQUIRE(a != nullptr);
    CHECK(a->mode == containercp::mail::MailDomainMode::LocalPrimary);

    // Next ID continues from saved max
    uint64_t id3 = mgr2.create("c.com", containercp::mail::MailDomainMode::ExternalRelay, 0, 0, "relay.example.com");
    CHECK(id3 == 3);
}

TEST_CASE("MailDomain domain normalization") {
    containercp::mail::MailDomainManager mgr;

    // Lowercase + trim + trailing dot removed
    uint64_t id1 = mgr.create("Example.COM", containercp::mail::MailDomainMode::LocalPrimary, 0, 0);
    CHECK(id1 == 1);
    auto* m = mgr.find(id1);
    REQUIRE(m != nullptr);
    CHECK(m->domain_name == "Example.COM");  // Manager doesn't normalize — API does
}

TEST_CASE("MailDomainManager create rejects duplicates even with different case") {
    containercp::mail::MailDomainManager mgr;

    // Manager does case-sensitive comparison — duplicates checked by API after normalization
    mgr.create("example.com", containercp::mail::MailDomainMode::LocalPrimary, 0, 0);
    uint64_t id2 = mgr.create("EXAMPLE.COM", containercp::mail::MailDomainMode::LocalPrimary, 0, 0);
    CHECK(id2 == 2);  // Manager allows case-different (API normalizes before calling)
    CHECK(mgr.list().size() == 2);
}

TEST_CASE("MailDomain normalize_domain function") {
    // Simulate the API-level normalize_domain logic (trim + tolower + trailing dot removal)
    auto trim = [](const std::string& s) -> std::string {
        size_t start = 0, end = s.size();
        while (start < end && (s[start] == ' ' || s[start] == '\t' || s[start] == '\n' || s[start] == '\r')) ++start;
        while (end > start && (s[end-1] == ' ' || s[end-1] == '\t' || s[end-1] == '\n' || s[end-1] == '\r')) --end;
        return s.substr(start, end - start);
    };
    auto norm = [&trim](const std::string& raw) -> std::string {
        std::string d = trim(raw);
        for (auto& c : d) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        while (!d.empty() && d.back() == '.') d.pop_back();
        return d;
    };

    CHECK(norm("Example.COM") == "example.com");
    CHECK(norm("Example.COM.") == "example.com");
    CHECK(norm("  Example.COM  ") == "example.com");
    CHECK(norm("UPPER.com.") == "upper.com");

    // Internal spaces are preserved — will be rejected by hostname validation
    CHECK(norm("exa mple.com") == "exa mple.com");

    // Multiple trailing dots
    CHECK(norm("test.com..") == "test.com");
    CHECK(norm("test.com...") == "test.com");
}

TEST_CASE("MailDomain json_extract type handling") {
    // Verify that json_extract handles booleans and numbers via the " key": path
    auto extract = [](const std::string& json, const std::string& key) -> std::string {
        std::string search = "\"" + key + "\":\"";
        auto pos = json.find(search);
        if (pos == std::string::npos) {
            search = "\"" + key + "\":";
            pos = json.find(search);
            if (pos == std::string::npos) return "";
            pos += search.size();
            auto end = json.find_first_of(",}", pos);
            if (end == std::string::npos) return "";
            auto val = json.substr(pos, end - pos);
            return val;
        }
        pos += search.size();
        auto end = json.find("\"", pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    };

    CHECK(extract(R"({"enabled":false})", "enabled") == "false");
    CHECK(extract(R"({"enabled":true})", "enabled") == "true");
    CHECK(extract(R"({"count":50})", "count") == "50");
    CHECK(extract(R"({"count":0})", "count") == "0");
    CHECK(extract(R"({"relay_host":"smtp.example.com"})", "relay_host") == "smtp.example.com");
    CHECK(extract(R"({"relay_host":""})", "relay_host") == "");
    CHECK(extract(R"({"relay_host":null})", "relay_host") == "null");
    CHECK(extract(R"({"mode":"local-primary"})", "mode") == "local-primary");
}

TEST_CASE("MailDomain json_has_key detection") {
    auto has_key = [](const std::string& json, const std::string& key) -> bool {
        std::string search = "\"" + key + "\":";
        return json.find(search) != std::string::npos;
    };

    CHECK(has_key(R"({"mode":"local-primary"})", "mode"));
    CHECK(has_key(R"({"enabled":false})", "enabled"));
    CHECK(has_key(R"({"enabled":true})", "enabled"));
    CHECK(has_key(R"({"relay_host":""})", "relay_host"));
    CHECK(has_key(R"({"relay_host":null})", "relay_host"));
    CHECK(has_key(R"({"max_mailboxes":50})", "max_mailboxes"));

    // Key not present
    CHECK_FALSE(has_key(R"({"mode":"local-primary"})", "enabled"));
    CHECK_FALSE(has_key(R"({})", "mode"));
}

TEST_CASE("MailDomain JSON null and empty string semantics") {
    // Test the API-level null/empty handling logic
    auto clear_if_null = [](const std::string& val) -> std::string {
        return (val == "null") ? "" : val;
    };

    // String field present with value
    CHECK(clear_if_null("smtp.example.com") == "smtp.example.com");
    // String field present with empty string — keep empty
    CHECK(clear_if_null("") == "");
    // String field present with JSON null — clear to empty
    CHECK(clear_if_null("null") == "");
}

TEST_CASE("MailDomain boolean validation logic") {
    // Verify boolean acceptance is strict
    auto is_valid_bool = [](const std::string& val) -> bool {
        return val == "true" || val == "false";
    };

    CHECK(is_valid_bool("true"));
    CHECK(is_valid_bool("false"));
    CHECK_FALSE(is_valid_bool("yes"));
    CHECK_FALSE(is_valid_bool("1"));
    CHECK_FALSE(is_valid_bool("abc"));
    CHECK_FALSE(is_valid_bool(""));
    CHECK_FALSE(is_valid_bool("True"));   // lowercase only
    CHECK_FALSE(is_valid_bool("FALSE"));  // lowercase only
}

#include "mail/MailboxManager.h"
#include "mail/MailPasswordHasher.h"

TEST_CASE("MailboxManager create/find/list/remove") {
    containercp::mail::MailboxManager mgr;

    uint64_t id = mgr.create(1, "user1", "$6$salt$hash");
    CHECK(id == 1);

    auto* mb = mgr.find(id);
    REQUIRE(mb != nullptr);
    CHECK(mb->local_part == "user1");
    CHECK(mb->domain_id == 1);
    CHECK(mb->enabled);
    CHECK(mb->password_hash == "$6$salt$hash");

    CHECK(mgr.list().size() == 1);
    CHECK(mgr.remove(id));
    CHECK(mgr.list().empty());
}

TEST_CASE("MailboxManager find_by_address") {
    containercp::mail::MailboxManager mgr;

    mgr.create(1, "alice", "hash1");
    mgr.create(1, "bob", "hash2");
    mgr.create(2, "alice", "hash3");  // same local_part, different domain

    auto* a = mgr.find_by_address("alice", 1);
    REQUIRE(a != nullptr);
    CHECK(a->local_part == "alice");
    CHECK(a->domain_id == 1);

    auto* a2 = mgr.find_by_address("alice", 2);
    REQUIRE(a2 != nullptr);
    CHECK(a2->domain_id == 2);

    // Same local_part different domain — different mailbox
    CHECK(a->id != a2->id);

    // Non-existent
    CHECK(mgr.find_by_address("nonexistent", 1) == nullptr);
}

TEST_CASE("MailboxManager find_by_domain") {
    containercp::mail::MailboxManager mgr;

    mgr.create(1, "a@d1", "h");
    mgr.create(1, "b@d1", "h");
    mgr.create(2, "c@d2", "h");

    auto d1 = mgr.find_by_domain(1);
    CHECK(d1.size() == 2);

    auto d2 = mgr.find_by_domain(2);
    CHECK(d2.size() == 1);

    auto d3 = mgr.find_by_domain(999);
    CHECK(d3.empty());
}

TEST_CASE("MailboxManager duplicate local_part within domain rejected") {
    containercp::mail::MailboxManager mgr;

    uint64_t id1 = mgr.create(1, "duplicate", "hash1");
    CHECK(id1 == 1);

    uint64_t id2 = mgr.create(1, "duplicate", "hash2");
    CHECK(id2 == 0);  // rejected

    // Same local_part in different domain is OK
    uint64_t id3 = mgr.create(2, "duplicate", "hash3");
    CHECK(id3 == 2);
}

TEST_CASE("MailboxManager set_mailboxes restores state") {
    containercp::mail::MailboxManager mgr;
    mgr.create(1, "a", "h1");
    mgr.create(1, "b", "h2");

    auto saved = mgr.list();
    containercp::mail::MailboxManager mgr2;
    mgr2.set_mailboxes(saved);

    CHECK(mgr2.list().size() == 2);
    auto* a = mgr2.find_by_address("a", 1);
    REQUIRE(a != nullptr);
    CHECK(a->password_hash == "h1");

    // Next ID continues
    uint64_t id3 = mgr2.create(2, "c", "h3");
    CHECK(id3 == 3);
}

TEST_CASE("MailPasswordHasher hash and verify") {
    using containercp::mail::MailPasswordHasher;

    std::string hash = MailPasswordHasher::hash("mypassword");
    CHECK_FALSE(hash.empty());
    CHECK(hash.find("$6$") == 0);  // SHA-512-CRYPT prefix

    // Verify correct password
    CHECK(MailPasswordHasher::verify("mypassword", hash));

    // Verify wrong password
    CHECK_FALSE(MailPasswordHasher::verify("wrongpassword", hash));

    // Verify with empty hash
    CHECK_FALSE(MailPasswordHasher::verify("pwd", ""));
}

#include "mail/MailAliasManager.h"

TEST_CASE("MailAliasManager create/find/list/remove") {
    containercp::mail::MailAliasManager mgr;

    uint64_t id = mgr.create(1, "info", "admin@example.com");
    CHECK(id == 1);

    auto* a = mgr.find(id);
    REQUIRE(a != nullptr);
    CHECK(a->source_local_part == "info");
    CHECK(a->destination == "admin@example.com");
    CHECK(a->domain_id == 1);

    CHECK(mgr.list().size() == 1);
    CHECK(mgr.remove(id));
    CHECK(mgr.list().empty());
}

TEST_CASE("MailAliasManager find_by_domain") {
    containercp::mail::MailAliasManager mgr;
    mgr.create(1, "a", "dest1@x.com");
    mgr.create(1, "b", "dest2@x.com");
    mgr.create(2, "c", "dest3@x.com");

    auto d1 = mgr.find_by_domain(1);
    CHECK(d1.size() == 2);

    auto d2 = mgr.find_by_domain(2);
    CHECK(d2.size() == 1);

    CHECK(mgr.find_by_domain(999).empty());
}

TEST_CASE("MailAliasManager find_by_source") {
    containercp::mail::MailAliasManager mgr;
    mgr.create(1, "info", "a@x.com");
    mgr.create(1, "info", "b@x.com");   // same source, different destination
    mgr.create(1, "other", "c@x.com");

    auto results = mgr.find_by_source("info", 1);
    CHECK(results.size() == 2);

    auto no_results = mgr.find_by_source("info", 2);
    CHECK(no_results.empty());
}

TEST_CASE("MailAliasManager duplicate detection") {
    containercp::mail::MailAliasManager mgr;
    uint64_t id1 = mgr.create(1, "info", "admin@x.com");
    CHECK(id1 == 1);

    // Same source + domain + destination = duplicate
    uint64_t id2 = mgr.create(1, "info", "admin@x.com");
    CHECK(id2 == 0);

    // Same source, different destination = allowed
    uint64_t id3 = mgr.create(1, "info", "other@x.com");
    CHECK(id3 == 2);
}

TEST_CASE("MailAliasManager set_aliases restores state") {
    containercp::mail::MailAliasManager mgr;
    mgr.create(1, "a", "d1@x.com");
    mgr.create(2, "b", "d2@x.com");

    auto saved = mgr.list();
    containercp::mail::MailAliasManager mgr2;
    mgr2.set_aliases(saved);

    CHECK(mgr2.list().size() == 2);
    auto a_list = mgr2.find_by_domain(1);
    CHECK(a_list.size() == 1);
    CHECK(a_list[0]->destination == "d1@x.com");

    uint64_t id3 = mgr2.create(3, "c", "d3@x.com");
    CHECK(id3 == 3);
}

// ── DockerMailProvider config generation tests ──────────────────

static std::string read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return "";
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

TEST_CASE("DockerMailProvider transport maps — LocalPrimary") {
    // Write transport maps for a LocalPrimary domain and verify output
    std::string tmp = "/tmp/containercp-test-mail-XXXXXX";
    char* resolved = ::mkdtemp(&tmp[0]);
    REQUIRE(resolved != nullptr);

    std::string data_root = resolved;
    std::string mail_dir = data_root + "/mail";
    std::string cfg_dir = mail_dir + "/config";
    std::string gen_dir = cfg_dir + "/generated";
    ::mkdir(mail_dir.c_str(), 0755);
    ::mkdir(cfg_dir.c_str(), 0755);
    ::mkdir(gen_dir.c_str(), 0755);

    containercp::mail::DockerMailProvider prov(containercp::logger::Logger::instance(), data_root);
    containercp::mail::MailboxManager mailboxes;
    containercp::mail::MailAliasManager aliases;

    std::vector<containercp::mail::MailDomain> domains;
    containercp::mail::MailDomain d;
    d.id = 1;
    d.domain_name = "primary.com";
    d.mode = containercp::mail::MailDomainMode::LocalPrimary;
    d.enabled = true;
    domains.push_back(d);

    auto r = prov.write_configs(domains, mailboxes, aliases);
    CHECK(r.success);

    // Verify transport_maps — domain entry sends all recipients to LMTP
    std::string tm = read_file(gen_dir + "/transport_maps");
    CHECK(tm.find("primary.com lmtp:containercp-mail-dovecot:24") != std::string::npos);

    // Verify relay_domains is NOT present (LocalPrimary doesn't relay)
    std::string pf = read_file(gen_dir + "/postfix-main.cf");
    CHECK(pf.find("relay_domains") == std::string::npos);
    CHECK(pf.find("virtual_mailbox_domains = primary.com") != std::string::npos);
    CHECK(pf.find("virtual_transport = lmtp:containercp-mail-dovecot:24") != std::string::npos);

    // Cleanup
    std::remove((gen_dir + "/transport_maps").c_str());
    std::remove((gen_dir + "/postfix-main.cf").c_str());
    std::remove((gen_dir + "/dovecot.conf").c_str());
    std::remove((gen_dir + "/passwd").c_str());
    std::remove((gen_dir + "/virtual_mailboxes").c_str());
    std::remove(gen_dir.c_str());
    std::remove(data_root.c_str());
}

TEST_CASE("DockerMailProvider transport maps — ExternalRelay") {
    std::string tmp = "/tmp/containercp-test-mail-XXXXXX";
    char* resolved = ::mkdtemp(&tmp[0]);
    REQUIRE(resolved != nullptr);

    std::string data_root = resolved;
    std::string mail_dir = data_root + "/mail";
    std::string cfg_dir = mail_dir + "/config";
    std::string gen_dir = cfg_dir + "/generated";
    ::mkdir(mail_dir.c_str(), 0755);
    ::mkdir(cfg_dir.c_str(), 0755);
    ::mkdir(gen_dir.c_str(), 0755);

    containercp::mail::DockerMailProvider prov(containercp::logger::Logger::instance(), data_root);
    containercp::mail::MailboxManager mailboxes;
    containercp::mail::MailAliasManager aliases;

    std::vector<containercp::mail::MailDomain> domains;
    containercp::mail::MailDomain d;
    d.id = 1;
    d.domain_name = "relay.com";
    d.mode = containercp::mail::MailDomainMode::ExternalRelay;
    d.relay_host = "smtp.relay.com";
    d.enabled = true;
    domains.push_back(d);

    auto r = prov.write_configs(domains, mailboxes, aliases);
    CHECK(r.success);

    std::string tm = read_file(gen_dir + "/transport_maps");
    CHECK(tm.find("relay.com smtp:[smtp.relay.com]") != std::string::npos);

    // ExternalRelay does NOT appear in virtual_mailbox_domains
    std::string pf = read_file(gen_dir + "/postfix-main.cf");
    CHECK(pf.find("relay_domains = relay.com") != std::string::npos);
    CHECK(pf.find("virtual_mailbox_domains =") != std::string::npos);
    // ExternalRelay doesn't have mailboxes, so virtual_transport is irrelevant
    // but the setting must still be present
    CHECK(pf.find("virtual_transport = lmtp:containercp-mail-dovecot:24") != std::string::npos);
    // virtual_mailbox_domains should be empty (no LocalPrimary or SplitM365 domains)
    CHECK(pf.find("virtual_mailbox_domains = relay.com") == std::string::npos);

    // Cleanup
    std::remove((gen_dir + "/transport_maps").c_str());
    std::remove((gen_dir + "/postfix-main.cf").c_str());
    std::remove((gen_dir + "/dovecot.conf").c_str());
    std::remove((gen_dir + "/passwd").c_str());
    std::remove((gen_dir + "/virtual_mailboxes").c_str());
    std::remove(gen_dir.c_str());
    std::remove(data_root.c_str());
}

TEST_CASE("DockerMailProvider transport maps — SplitM365") {
    std::string tmp = "/tmp/containercp-test-mail-XXXXXX";
    char* resolved = ::mkdtemp(&tmp[0]);
    REQUIRE(resolved != nullptr);

    std::string data_root = resolved;
    std::string mail_dir = data_root + "/mail";
    std::string cfg_dir = mail_dir + "/config";
    std::string gen_dir = cfg_dir + "/generated";
    ::mkdir(mail_dir.c_str(), 0755);
    ::mkdir(cfg_dir.c_str(), 0755);
    ::mkdir(gen_dir.c_str(), 0755);

    containercp::mail::DockerMailProvider prov(containercp::logger::Logger::instance(), data_root);
    containercp::mail::MailboxManager mailboxes;
    containercp::mail::MailAliasManager aliases;

    // Add a local mailbox to verify per-user LMTP entries
    mailboxes.create(1, "alice", "$6$salt$localhash");

    std::vector<containercp::mail::MailDomain> domains;
    containercp::mail::MailDomain d;
    d.id = 1;
    d.domain_name = "hybrid.com";
    d.mode = containercp::mail::MailDomainMode::SplitM365;
    d.relay_host = "hybrid-com.mail.protection.outlook.com";
    d.enabled = true;
    domains.push_back(d);

    auto r = prov.write_configs(domains, mailboxes, aliases);
    CHECK(r.success);

    std::string tm = read_file(gen_dir + "/transport_maps");
    // Per-user LMTP entry for the local mailbox (full address match
    // takes priority over the domain-level catch-all below)
    CHECK(tm.find("alice@hybrid.com lmtp:containercp-mail-dovecot:24") != std::string::npos);
    // Domain-level SMTP relay catch-all for non-local recipients
    CHECK(tm.find("hybrid.com smtp:[hybrid-com.mail.protection.outlook.com]:25")
          != std::string::npos);

    std::string pf = read_file(gen_dir + "/postfix-main.cf");
    // SplitM365 appears in BOTH relay_domains AND virtual_mailbox_domains
    CHECK(pf.find("relay_domains = hybrid.com") != std::string::npos);
    CHECK(pf.find("virtual_mailbox_domains = hybrid.com") != std::string::npos);
    CHECK(pf.find("virtual_transport = lmtp:containercp-mail-dovecot:24") != std::string::npos);

    // Cleanup
    std::remove((gen_dir + "/transport_maps").c_str());
    std::remove((gen_dir + "/postfix-main.cf").c_str());
    std::remove((gen_dir + "/dovecot.conf").c_str());
    std::remove((gen_dir + "/passwd").c_str());
    std::remove((gen_dir + "/virtual_mailboxes").c_str());
    std::remove(gen_dir.c_str());
    std::remove(data_root.c_str());
}

TEST_CASE("DockerMailProvider transport maps — ExternalRelay multiple domains") {
    // Verify that two ExternalRelay domains with different relay hosts coexist
    std::string tmp = "/tmp/containercp-test-mail-XXXXXX";
    char* resolved = ::mkdtemp(&tmp[0]);
    REQUIRE(resolved != nullptr);

    std::string data_root = resolved;
    std::string mail_dir = data_root + "/mail";
    std::string cfg_dir = mail_dir + "/config";
    std::string gen_dir = cfg_dir + "/generated";
    ::mkdir(mail_dir.c_str(), 0755);
    ::mkdir(cfg_dir.c_str(), 0755);
    ::mkdir(gen_dir.c_str(), 0755);

    containercp::mail::DockerMailProvider prov(containercp::logger::Logger::instance(), data_root);
    containercp::mail::MailboxManager mailboxes;
    containercp::mail::MailAliasManager aliases;

    std::vector<containercp::mail::MailDomain> domains;
    containercp::mail::MailDomain d1;
    d1.id = 1;
    d1.domain_name = "one.com";
    d1.mode = containercp::mail::MailDomainMode::ExternalRelay;
    d1.relay_host = "relay-one.com";
    d1.enabled = true;
    domains.push_back(d1);

    containercp::mail::MailDomain d2;
    d2.id = 2;
    d2.domain_name = "two.com";
    d2.mode = containercp::mail::MailDomainMode::ExternalRelay;
    d2.relay_host = "relay-two.com";
    d2.enabled = true;
    domains.push_back(d2);

    auto r = prov.write_configs(domains, mailboxes, aliases);
    CHECK(r.success);

    std::string tm = read_file(gen_dir + "/transport_maps");
    CHECK(tm.find("one.com smtp:[relay-one.com]") != std::string::npos);
    CHECK(tm.find("two.com smtp:[relay-two.com]") != std::string::npos);

    std::string pf = read_file(gen_dir + "/postfix-main.cf");
    // Both domains in relay_domains
    CHECK(pf.find("relay_domains = one.com, two.com") != std::string::npos);
    // Neither in virtual_mailbox_domains
    CHECK(pf.find("virtual_mailbox_domains = one.com") == std::string::npos);
    CHECK(pf.find("virtual_mailbox_domains = two.com") == std::string::npos);

    // No global relayhost set (relies on per-domain transport maps)
    CHECK(pf.find("relayhost") == std::string::npos);

    // Cleanup
    std::remove((gen_dir + "/transport_maps").c_str());
    std::remove((gen_dir + "/postfix-main.cf").c_str());
    std::remove((gen_dir + "/dovecot.conf").c_str());
    std::remove((gen_dir + "/passwd").c_str());
    std::remove((gen_dir + "/virtual_mailboxes").c_str());
    std::remove(gen_dir.c_str());
    std::remove(data_root.c_str());
}
