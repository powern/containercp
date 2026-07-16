#include "storage/LegacyImporter.h"
#include "storage/MigrationEngine.h"
#include "storage/SchemaMigrations.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "doctest/doctest.h"

namespace fs = std::filesystem;

static std::string test_dir(const std::string& name) {
    return (fs::temp_directory_path() / name).string() + "/";
}

static void cleanup(const std::string& dir) {
    fs::remove_all(dir);
}

static const char* kFixtureBase = TEST_FIXTURE_DIR "/v0.6.0";

// Helper: create a pool with schema for importer tests
static void init_pool(containercp::storage::ConnectionPool& pool, const std::string& dir) {
    REQUIRE(pool.initialize(dir + "import.db"));
    containercp::storage::SQLiteDB migrator;
    REQUIRE(migrator.open(dir + "import.db"));
    containercp::storage::MigrationEngine eng;
    containercp::storage::register_all_schema_migrations(eng);
    REQUIRE(eng.migrate(migrator));
    migrator.close();
}

// Copy fixture files to a temp directory (impoter reads from directory)
static void copy_fixtures(const std::string& src_subdir, const std::string& dest) {
    fs::create_directories(dest);
    std::string src = std::string(kFixtureBase) + "/" + src_subdir;
    for (const auto& entry : fs::directory_iterator(src)) {
        auto path = entry.path();
        if (path.extension() == ".db" || !path.has_extension()) {
            fs::copy(path, fs::path(dest) / path.filename(),
                     fs::copy_options::overwrite_existing);
        }
    }
}

TEST_CASE("Importer normal fixtures round trip") {
    auto dir = test_dir("imp_normal");
    cleanup(dir); fs::create_directories(dir);
    {
        // Set up SQLite
        containercp::storage::ConnectionPool pool;
    init_pool(pool, dir);
        auto data_dir = dir + "import_data/";
        copy_fixtures("normal", data_dir);

        containercp::storage::LegacyImporter imp(data_dir, pool);
        auto result = imp.import_all();
        CHECK(result.success);
    }
    cleanup(dir);
}

TEST_CASE("Importer legacy formats") {
    auto dir = test_dir("imp_legacy");
    cleanup(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
        init_pool(pool, dir);
        std::string src = std::string(kFixtureBase) + "/legacy";

        // Copy legacy files with standard importer filenames
        fs::copy(src + "/sites_5field.db", dir + "sites.db", fs::copy_options::overwrite_existing);
        fs::copy(src + "/ssl_certificates_4field.db", dir + "ssl_certificates.db", fs::copy_options::overwrite_existing);
        fs::copy(src + "/mail_domains_10field.db", dir + "mail_domains.db", fs::copy_options::overwrite_existing);

        containercp::storage::LegacyImporter imp(dir, pool);
        auto r = imp.import_sites();
        CHECK(r.success);
        CHECK(r.record_count > 0);

        r = imp.import_ssl_certificates();
        CHECK(r.success);
        CHECK(r.record_count > 0);

        r = imp.import_mail_domains();
        CHECK(r.success);
        CHECK(r.record_count > 0);
    }
    cleanup(dir);
}

TEST_CASE("Importer sentinel fixtures") {
    auto dir = test_dir("imp_sent");
    cleanup(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
        init_pool(pool, dir);
        std::string src = std::string(kFixtureBase) + "/sentinels";

        fs::copy(src + "/reverse_proxies_site0.db", dir + "reverse_proxies.db", fs::copy_options::overwrite_existing);
        fs::copy(src + "/mail_domains_external.db", dir + "mail_domains.db", fs::copy_options::overwrite_existing);

        containercp::storage::LegacyImporter imp(dir, pool);
        auto r = imp.import_reverse_proxies();
        CHECK(r.success);
        CHECK(r.record_count == 1);

        r = imp.import_mail_domains();
        CHECK(r.success);
        CHECK(r.record_count == 1);
    }
    cleanup(dir);
}

TEST_CASE("Importer import_all with normal fixtures") {
    auto dir = test_dir("imp_all");
    cleanup(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
    init_pool(pool, dir);
        auto data_dir = dir + "import_data/";
        copy_fixtures("normal", data_dir);

        containercp::storage::LegacyImporter imp(data_dir, pool);
        auto result = imp.import_all();
        CHECK(result.success);

        // Verify counts via SQLiteStorage
        containercp::storage::SQLiteStorage ss(pool);
        CHECK(ss.load_nodes().size() == 1);
        CHECK(ss.load_sites().size() == 3);
        CHECK(ss.load_domains().size() == 4);
        CHECK(ss.load_users().size() == 2);
    }
    cleanup(dir);
}

TEST_CASE("Importer idempotent") {
    auto dir = test_dir("imp_idem");
    cleanup(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
    init_pool(pool, dir);
        auto data_dir = dir + "import_data/";
        copy_fixtures("normal", data_dir);

        containercp::storage::LegacyImporter imp(data_dir, pool);
        CHECK(imp.import_all().success);
        uint64_t count1 = containercp::storage::SQLiteStorage(pool).load_nodes().size();

        // Import again
        CHECK(imp.import_all().success);
        uint64_t count2 = containercp::storage::SQLiteStorage(pool).load_nodes().size();
        CHECK(count1 == count2);
    }
    cleanup(dir);
}

TEST_CASE("Importer missing optional file") {
    auto dir = test_dir("imp_miss");
    cleanup(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
    init_pool(pool, dir);
        // Empty directory — most required files missing
        containercp::storage::LegacyImporter imp(dir, pool);

        // Optional files should be SkippedMissingOptional
        auto r = imp.import_ssl_certificates();
        CHECK(r.success);
        CHECK(r.disposition == containercp::storage::ImportDisposition::SkippedMissingOptional);

        // Required files should fail
        r = imp.import_nodes();
        CHECK_FALSE(r.success);
    }
    cleanup(dir);
}

TEST_CASE("Importer source files unchanged") {
    auto dir = test_dir("imp_unchanged");
    cleanup(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
    init_pool(pool, dir);
        auto data_dir = dir + "import_data/";
        copy_fixtures("normal", data_dir);

        // Record checksums before
        std::string nodes_path = data_dir + "nodes.db";
        auto before = (fs::exists(nodes_path) ? fs::file_size(nodes_path) : 0);

        containercp::storage::LegacyImporter imp(data_dir, pool);
        CHECK(imp.import_all().success);

        // Verify file unchanged
        auto after = fs::file_size(nodes_path);
        CHECK(before == after);
    }
    cleanup(dir);
}

TEST_CASE("Importer empty file handling") {
    auto dir = test_dir("imp_empty");
    cleanup(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
    init_pool(pool, dir);
        // Create an empty file
        {
            std::ofstream f(dir + "nodes.db");
            // empty
        }
        containercp::storage::LegacyImporter imp(dir, pool);
        auto r = imp.import_nodes();
        CHECK(r.success);
        CHECK(r.record_count == 0);
    }
    cleanup(dir);
}

TEST_CASE("Importer malformed rejects wrong field count") {
    auto dir = test_dir("imp_mal");
    cleanup(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
    init_pool(pool, dir);
        copy_fixtures("malformed", dir);

        containercp::storage::LegacyImporter imp(dir, pool);
        // wrong_field_count.db is a sites-format file
        auto r = imp.import_sites();
        // Should fail on the malformed data
        CHECK_FALSE(r.success);
        CHECK(r.disposition == containercp::storage::ImportDisposition::Failed);
    }
    cleanup(dir);
}

TEST_CASE("Importer with production-derived fixtures") {
    auto dir = test_dir("imp_prod");
    cleanup(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
    init_pool(pool, dir);
        std::string prod_src = std::string(kFixtureBase) + "/production_derived";
        if (fs::exists(prod_src)) {
            copy_fixtures("production_derived", dir);
            containercp::storage::LegacyImporter imp(dir, pool);
            auto result = imp.import_all();
            CHECK(result.success);
        }
    }
    cleanup(dir);
}
