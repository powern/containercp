#include "ssl/CertificateStore.h"
#include "logger/Logger.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "doctest/doctest.h"

static std::string test_dir() {
    return "/tmp/containercp_test_ssl_" + std::to_string(::getpid());
}

static void cleanup(const std::string& dir) {
    ::unlink((dir + "/1/metadata.json").c_str());
    ::unlink((dir + "/1/fullchain.pem").c_str());
    ::unlink((dir + "/1/privkey.pem").c_str());
    ::unlink((dir + "/1/chain.pem").c_str());
    ::rmdir((dir + "/1").c_str());
    ::rmdir(dir.c_str());
}

TEST_CASE("CertificateStore save and load metadata") {
    auto& log = containercp::logger::Logger::instance();
    std::string dir = test_dir();
    ::mkdir(dir.c_str(), 0700);

    containercp::ssl::CertificateStore store(log, dir);

    containercp::ssl::CertificateStore::Metadata meta;
    meta.site_id = 1;
    meta.provider_id = "letsencrypt";
    meta.status = "active";
    meta.domains = {"example.com", "www.example.com"};
    meta.issued_at = "2025-07-08T12:00:00Z";
    meta.expires_at = "2025-10-06T12:00:00Z";
    meta.renew_after = "2025-09-06T12:00:00Z";
    meta.https_enabled = true;
    meta.auto_renew = true;
    meta.fingerprint_sha256 = "abc123";
    meta.issuer = "CN=R3,O=Let's Encrypt,C=US";
    meta.challenge_type = "http-01";

    CHECK(store.save_metadata(1, meta));

    auto loaded = store.load_metadata(1);
    CHECK(loaded.site_id == 1);
    CHECK(loaded.provider_id == "letsencrypt");
    CHECK(loaded.status == "active");
    CHECK(loaded.domains.size() == 2);
    CHECK(loaded.domains[0] == "example.com");
    CHECK(loaded.domains[1] == "www.example.com");
    CHECK(loaded.issued_at == "2025-07-08T12:00:00Z");
    CHECK(loaded.expires_at == "2025-10-06T12:00:00Z");
    CHECK(loaded.https_enabled);
    CHECK(loaded.auto_renew);
    CHECK(loaded.fingerprint_sha256 == "abc123");

    cleanup(dir);
}

TEST_CASE("CertificateStore save and load certificate files") {
    auto& log = containercp::logger::Logger::instance();
    std::string dir = test_dir();
    ::mkdir(dir.c_str(), 0700);

    containercp::ssl::CertificateStore store(log, dir);

    std::string cert_pem = "-----BEGIN CERTIFICATE-----\nTEST\n-----END CERTIFICATE-----\n";
    std::string key_pem = "-----BEGIN PRIVATE KEY-----\nTESTKEY\n-----END PRIVATE KEY-----\n";

    CHECK(store.save_fullchain(1, cert_pem));
    CHECK(store.save_privkey(1, key_pem));
    CHECK(store.save_chain(1, cert_pem));

    CHECK(store.certificate_files_exist(1));

    std::string loaded_cert = store.load_fullchain(1);
    CHECK(loaded_cert == cert_pem);

    std::string loaded_key = store.load_privkey(1);
    CHECK(loaded_key == key_pem);

    // Check permissions
    struct stat st;
    ::stat(store.fullchain_path(1).c_str(), &st);
    CHECK((st.st_mode & 0777) == 0644);

    ::stat(store.privkey_path(1).c_str(), &st);
    CHECK((st.st_mode & 0777) == 0600);

    cleanup(dir);
}

TEST_CASE("CertificateStore atomic replace") {
    auto& log = containercp::logger::Logger::instance();
    std::string dir = test_dir();
    ::mkdir(dir.c_str(), 0700);

    containercp::ssl::CertificateStore store(log, dir);

    containercp::ssl::CertificateStore::Metadata meta;
    meta.site_id = 1;
    meta.status = "active";
    meta.domains = {"example.com"};
    meta.issued_at = "2025-07-08T12:00:00Z";

    CHECK(store.save_metadata(1, meta));

    // Overwrite with new data
    meta.domains = {"example.com", "www.example.com"};
    meta.expires_at = "2025-10-06T12:00:00Z";
    CHECK(store.save_metadata(1, meta));

    auto loaded = store.load_metadata(1);
    CHECK(loaded.domains.size() == 2);
    CHECK(loaded.expires_at == "2025-10-06T12:00:00Z");

    // Verify no .tmp file remains
    std::string tmp_path = store.metadata_path(1) + ".tmp";
    struct stat st;
    CHECK(::stat(tmp_path.c_str(), &st) != 0);

    cleanup(dir);
}

TEST_CASE("CertificateStore save_all") {
    auto& log = containercp::logger::Logger::instance();
    std::string dir = test_dir();
    ::mkdir(dir.c_str(), 0700);

    containercp::ssl::CertificateStore store(log, dir);

    containercp::ssl::CertificateStore::Metadata meta;
    meta.site_id = 1;
    meta.status = "active";
    meta.domains = {"example.com"};

    auto result = store.save_all(1, meta, "fullchain-data", "privkey-data", "chain-data");
    CHECK(result.success);

    CHECK(store.metadata_exists(1));
    CHECK(store.certificate_files_exist(1));

    cleanup(dir);
}

TEST_CASE("CertificateStore remove_all") {
    auto& log = containercp::logger::Logger::instance();
    std::string dir = test_dir();
    ::mkdir(dir.c_str(), 0700);

    containercp::ssl::CertificateStore store(log, dir);

    containercp::ssl::CertificateStore::Metadata meta;
    meta.site_id = 1;
    meta.domains = {"example.com"};
    store.save_all(1, meta, "cert", "key", "chain");

    CHECK(store.metadata_exists(1));
    CHECK(store.remove_all(1));
    CHECK_FALSE(store.metadata_exists(1));

    cleanup(dir);
}

TEST_CASE("CertificateStore enumerate") {
    auto& log = containercp::logger::Logger::instance();
    std::string dir = test_dir();
    ::mkdir(dir.c_str(), 0700);

    containercp::ssl::CertificateStore store(log, dir);

    containercp::ssl::CertificateStore::Metadata meta;
    meta.domains = {"example.com"};

    store.save_metadata(1, meta);
    store.save_metadata(2, meta);
    store.save_metadata(3, meta);

    auto ids = store.enumerate();
    CHECK(ids.size() == 3);
    CHECK(ids[0] == 1);
    CHECK(ids[1] == 2);
    CHECK(ids[2] == 3);

    // Cleanup
    store.remove_all(1);
    store.remove_all(2);
    store.remove_all(3);
    cleanup(dir);
}

TEST_CASE("CertificateStore validate active certificate") {
    auto& log = containercp::logger::Logger::instance();
    std::string dir = test_dir();
    ::mkdir(dir.c_str(), 0700);

    containercp::ssl::CertificateStore store(log, dir);

    containercp::ssl::CertificateStore::Metadata meta;
    meta.site_id = 1;
    meta.status = "active";
    meta.domains = {"example.com"};
    meta.provider_id = "letsencrypt";

    store.save_all(1, meta, "fullchain-data", "privkey-data", "chain-data");

    auto result = store.validate(1);
    CHECK(result.valid);

    cleanup(dir);
}

TEST_CASE("CertificateStore validate missing files") {
    auto& log = containercp::logger::Logger::instance();
    std::string dir = test_dir();
    ::mkdir(dir.c_str(), 0700);

    containercp::ssl::CertificateStore store(log, dir);

    // Validate non-existent site
    auto result = store.validate(999);
    CHECK_FALSE(result.valid);
    CHECK(result.errors.size() > 0);

    cleanup(dir);
}

TEST_CASE("CertificateStore invalid JSON metadata") {
    auto& log = containercp::logger::Logger::instance();
    std::string dir = test_dir();
    ::mkdir(dir.c_str(), 0700);
    ::mkdir((dir + "/1").c_str(), 0700);

    // Write invalid JSON
    std::ofstream f(dir + "/1/metadata.json");
    f << "{invalid json}";
    f.close();

    containercp::ssl::CertificateStore store(log, dir);
    auto meta = store.load_metadata(1);
    // Should return default metadata without throwing
    CHECK(meta.version == 1);
    CHECK(meta.status == "http_only");

    cleanup(dir);
}

TEST_CASE("CertificateStore metadata version compatibility") {
    auto& log = containercp::logger::Logger::instance();
    std::string dir = test_dir();
    ::mkdir(dir.c_str(), 0700);

    containercp::ssl::CertificateStore store(log, dir);

    containercp::ssl::CertificateStore::Metadata meta;
    meta.site_id = 1;
    meta.version = 1;
    meta.domains = {"example.com"};
    meta.issuer = "CN=R3,O=Let's Encrypt,C=US";

    CHECK(store.save_metadata(1, meta));

    auto loaded = store.load_metadata(1);
    CHECK(loaded.version == 1);
    CHECK(loaded.issuer == "CN=R3,O=Let's Encrypt,C=US");

    cleanup(dir);
}

TEST_CASE("CertificateStore timestamp_utc format") {
    auto ts = containercp::ssl::CertificateStore::timestamp_utc();
    // ISO-8601 format: 2025-07-08T12:00:00Z
    CHECK(ts.size() == 20);
    CHECK(ts[10] == 'T');
    CHECK(ts[19] == 'Z');
}

TEST_CASE("CertificateStore domains helpers") {
    auto domains = containercp::ssl::CertificateStore::string_to_domains("a.com,b.com,c.com");
    CHECK(domains.size() == 3);
    CHECK(domains[0] == "a.com");
    CHECK(domains[1] == "b.com");
    CHECK(domains[2] == "c.com");

    std::string str = containercp::ssl::CertificateStore::domains_to_string(domains);
    CHECK(str == "a.com,b.com,c.com");

    // Empty
    auto empty = containercp::ssl::CertificateStore::string_to_domains("");
    CHECK(empty.empty());
}
