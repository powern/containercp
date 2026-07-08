#include "ssl/CertificateStore.h"
#include "logger/Logger.h"

#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "doctest/doctest.h"

static std::string test_root() {
    return "/tmp/containercp_test_ssl_" + std::to_string(::getpid());
}

static void cleanup_dir(const std::string& path) {
    DIR* d = ::opendir(path.c_str());
    if (!d) return;
    struct dirent* entry;
    while ((entry = ::readdir(d)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        std::string full = path + "/" + name;
        if (entry->d_type == DT_DIR) {
            cleanup_dir(full);
        } else {
            ::unlink(full.c_str());
        }
    }
    ::closedir(d);
    ::rmdir(path.c_str());
}

TEST_CASE("CertificateStore save and load metadata") {
    auto& log = containercp::logger::Logger::instance();
    std::string dir = test_root();
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

    auto result = store.load_metadata(1);
    CHECK(result.success);
    CHECK(result.error == containercp::ssl::CertificateStore::LoadError::NONE);
    CHECK(result.metadata.site_id == 1);
    CHECK(result.metadata.provider_id == "letsencrypt");
    CHECK(result.metadata.status == "active");
    CHECK(result.metadata.domains.size() == 2);
    CHECK(result.metadata.domains[0] == "example.com");
    CHECK(result.metadata.domains[1] == "www.example.com");
    CHECK(result.metadata.https_enabled);
    CHECK(result.metadata.auto_renew);

    cleanup_dir(dir);
}

TEST_CASE("CertificateStore load_metadata returns NOT_FOUND") {
    auto& log = containercp::logger::Logger::instance();
    std::string dir = test_root();
    ::mkdir(dir.c_str(), 0700);

    containercp::ssl::CertificateStore store(log, dir);
    auto result = store.load_metadata(999);
    CHECK_FALSE(result.success);
    CHECK(result.error == containercp::ssl::CertificateStore::LoadError::NOT_FOUND);

    cleanup_dir(dir);
}

TEST_CASE("CertificateStore load_metadata returns INVALID_JSON") {
    auto& log = containercp::logger::Logger::instance();
    std::string dir = test_root();
    ::mkdir(dir.c_str(), 0700);
    ::mkdir((dir + "/1").c_str(), 0700);

    {
        std::ofstream f(dir + "/1/metadata.json");
        f << "{invalid json}";
    }

    containercp::ssl::CertificateStore store(log, dir);
    auto result = store.load_metadata(1);
    CHECK_FALSE(result.success);
    CHECK(result.error == containercp::ssl::CertificateStore::LoadError::INVALID_JSON);

    cleanup_dir(dir);
}

TEST_CASE("CertificateStore load_metadata returns IO_ERROR for empty file") {
    auto& log = containercp::logger::Logger::instance();
    std::string dir = test_root();
    ::mkdir(dir.c_str(), 0700);
    ::mkdir((dir + "/1").c_str(), 0700);

    {
        std::ofstream f(dir + "/1/metadata.json");
        f << "";
    }

    containercp::ssl::CertificateStore store(log, dir);
    auto result = store.load_metadata(1);
    CHECK_FALSE(result.success);
    CHECK(result.error == containercp::ssl::CertificateStore::LoadError::IO_ERROR);

    cleanup_dir(dir);
}

TEST_CASE("CertificateStore load_metadata returns INVALID_SCHEMA for site_id mismatch") {
    auto& log = containercp::logger::Logger::instance();
    std::string dir = test_root();
    ::mkdir(dir.c_str(), 0700);
    ::mkdir((dir + "/1").c_str(), 0700);

    containercp::ssl::CertificateStore store(log, dir);
    containercp::ssl::CertificateStore::Metadata meta;
    meta.site_id = 2;
    meta.domains = {"x.com"};
    store.save_metadata(1, meta);

    auto result = store.load_metadata(1);
    CHECK_FALSE(result.success);
    CHECK(result.error == containercp::ssl::CertificateStore::LoadError::INVALID_SCHEMA);

    cleanup_dir(dir);
}

TEST_CASE("CertificateStore save_all transactional success") {
    auto& log = containercp::logger::Logger::instance();
    std::string dir = test_root();
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

    // Verify no staging directories remain
    DIR* d = ::opendir(store.site_dir(1).c_str());
    REQUIRE(d != nullptr);
    struct dirent* entry;
    while ((entry = ::readdir(d)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        CHECK(name.find(".staging-") == std::string::npos);
    }
    ::closedir(d);

    cleanup_dir(dir);
}

TEST_CASE("CertificateStore save_all rollback on write failure") {
    auto& log = containercp::logger::Logger::instance();
    std::string dir = test_root();
    ::mkdir(dir.c_str(), 0700);
    ::mkdir((dir + "/2").c_str(), 0700);
    // Make fullchain.pem a directory so writing it fails
    ::mkdir((dir + "/2/fullchain.pem").c_str(), 0700);

    containercp::ssl::CertificateStore store(log, dir);

    containercp::ssl::CertificateStore::Metadata meta;
    meta.site_id = 2;
    meta.status = "active";
    meta.domains = {"rollback-test.com"};

    auto result = store.save_all(2, meta, "fullchain-data", "privkey-data", "chain-data");
    CHECK_FALSE(result.success);

    // Verify no staging directory remains
    DIR* d = ::opendir(store.site_dir(2).c_str());
    REQUIRE(d != nullptr);
    struct dirent* entry;
    bool staging_found = false;
    while ((entry = ::readdir(d)) != nullptr) {
        std::string name = entry->d_name;
        if (name.find(".staging-") == 0) {
            staging_found = true;
        }
    }
    ::closedir(d);
    CHECK_FALSE(staging_found);

    // Cleanup the directory we created as a file-blocker
    ::rmdir((dir + "/2/fullchain.pem").c_str());
    cleanup_dir(dir);
}

TEST_CASE("CertificateStore save and load certificate files") {
    auto& log = containercp::logger::Logger::instance();
    std::string dir = test_root();
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

    struct stat st;
    ::stat(store.fullchain_path(1).c_str(), &st);
    CHECK((st.st_mode & 0777) == 0644);

    ::stat(store.privkey_path(1).c_str(), &st);
    CHECK((st.st_mode & 0777) == 0600);

    cleanup_dir(dir);
}

TEST_CASE("CertificateStore atomic replace") {
    auto& log = containercp::logger::Logger::instance();
    std::string dir = test_root();
    ::mkdir(dir.c_str(), 0700);

    containercp::ssl::CertificateStore store(log, dir);

    containercp::ssl::CertificateStore::Metadata meta;
    meta.site_id = 1;
    meta.status = "active";
    meta.domains = {"example.com"};
    meta.issued_at = "2025-07-08T12:00:00Z";

    CHECK(store.save_metadata(1, meta));

    meta.domains = {"example.com", "www.example.com"};
    meta.expires_at = "2025-10-06T12:00:00Z";
    CHECK(store.save_metadata(1, meta));

    auto result = store.load_metadata(1);
    CHECK(result.metadata.domains.size() == 2);
    CHECK(result.metadata.expires_at == "2025-10-06T12:00:00Z");

    std::string tmp_path = store.metadata_path(1) + ".tmp";
    struct stat st;
    CHECK(::stat(tmp_path.c_str(), &st) != 0);

    cleanup_dir(dir);
}

TEST_CASE("CertificateStore save_all then remove_all") {
    auto& log = containercp::logger::Logger::instance();
    std::string dir = test_root();
    ::mkdir(dir.c_str(), 0700);

    containercp::ssl::CertificateStore store(log, dir);

    containercp::ssl::CertificateStore::Metadata meta;
    meta.site_id = 1;
    meta.domains = {"example.com"};
    store.save_all(1, meta, "cert", "key", "chain");

    CHECK(store.metadata_exists(1));
    CHECK(store.remove_all(1));
    CHECK_FALSE(store.metadata_exists(1));

    cleanup_dir(dir);
}

TEST_CASE("CertificateStore enumerate") {
    auto& log = containercp::logger::Logger::instance();
    std::string dir = test_root();
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

    store.remove_all(1);
    store.remove_all(2);
    store.remove_all(3);
    cleanup_dir(dir);
}

TEST_CASE("CertificateStore validate active certificate") {
    auto& log = containercp::logger::Logger::instance();
    std::string dir = test_root();
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

    cleanup_dir(dir);
}

TEST_CASE("CertificateStore validate missing files") {
    auto& log = containercp::logger::Logger::instance();
    std::string dir = test_root();
    ::mkdir(dir.c_str(), 0700);

    containercp::ssl::CertificateStore store(log, dir);
    auto result = store.validate(999);
    CHECK_FALSE(result.valid);
    CHECK(result.errors.size() > 0);

    cleanup_dir(dir);
}

TEST_CASE("CertificateStore metadata version compatibility") {
    auto& log = containercp::logger::Logger::instance();
    std::string dir = test_root();
    ::mkdir(dir.c_str(), 0700);

    containercp::ssl::CertificateStore store(log, dir);

    containercp::ssl::CertificateStore::Metadata meta;
    meta.site_id = 1;
    meta.version = 1;
    meta.domains = {"example.com"};
    meta.issuer = "CN=R3,O=Let's Encrypt,C=US";

    CHECK(store.save_metadata(1, meta));
    auto result = store.load_metadata(1);
    CHECK(result.metadata.version == 1);
    CHECK(result.metadata.issuer == "CN=R3,O=Let's Encrypt,C=US");

    cleanup_dir(dir);
}

TEST_CASE("CertificateStore timestamp_utc format") {
    auto ts = containercp::ssl::CertificateStore::timestamp_utc();
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

    auto empty = containercp::ssl::CertificateStore::string_to_domains("");
    CHECK(empty.empty());
}

TEST_CASE("CertificateStore load_error_string") {
    using LoadError = containercp::ssl::CertificateStore::LoadError;
    CHECK(containercp::ssl::CertificateStore::load_error_string(LoadError::NONE) == "OK");
    CHECK(containercp::ssl::CertificateStore::load_error_string(LoadError::NOT_FOUND) == "NOT_FOUND");
    CHECK(containercp::ssl::CertificateStore::load_error_string(LoadError::INVALID_JSON) == "INVALID_JSON");
    CHECK(containercp::ssl::CertificateStore::load_error_string(LoadError::UNSUPPORTED_VERSION) == "UNSUPPORTED_VERSION");
    CHECK(containercp::ssl::CertificateStore::load_error_string(LoadError::IO_ERROR) == "IO_ERROR");
    CHECK(containercp::ssl::CertificateStore::load_error_string(LoadError::INVALID_SCHEMA) == "INVALID_SCHEMA");
}
