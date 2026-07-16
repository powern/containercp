#include "SchemaMigrations.h"

#include <sstream>

namespace containercp::storage {

namespace {

// Canonical DDL for the entire v1 schema.
// This string is used as both the migration descriptor and the
// migration callback, so any DDL modification changes the checksum.
const char* kSchemaV1DDL = R"SQL(
CREATE TABLE IF NOT EXISTS nodes (
    id          INTEGER PRIMARY KEY,
    name        TEXT NOT NULL,
    type        TEXT NOT NULL DEFAULT 'local',
    created_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);

CREATE TABLE IF NOT EXISTS sites (
    id                INTEGER PRIMARY KEY,
    domain            TEXT NOT NULL,
    owner             TEXT NOT NULL DEFAULT '',
    node_id           INTEGER NOT NULL DEFAULT 0,
    web_server        TEXT NOT NULL DEFAULT 'apache',
    php_mail_enabled  INTEGER NOT NULL DEFAULT 0,
    created_at        TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at        TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
CREATE INDEX IF NOT EXISTS idx_sites_node_id ON sites(node_id);

CREATE TABLE IF NOT EXISTS users (
    id              INTEGER PRIMARY KEY,
    username        TEXT NOT NULL,
    uid             INTEGER NOT NULL DEFAULT 0,
    home_directory  TEXT NOT NULL DEFAULT '',
    shell           TEXT NOT NULL DEFAULT '/usr/sbin/nologin',
    enabled         INTEGER NOT NULL DEFAULT 1,
    created_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);

CREATE TABLE IF NOT EXISTS domains (
    id          INTEGER PRIMARY KEY,
    fqdn        TEXT NOT NULL,
    owner_id    INTEGER NOT NULL DEFAULT 0,
    site_id     INTEGER NOT NULL DEFAULT 0,
    php_version TEXT NOT NULL DEFAULT '8.4',
    ssl_enabled INTEGER NOT NULL DEFAULT 0,
    enabled     INTEGER NOT NULL DEFAULT 1,
    type        TEXT NOT NULL DEFAULT 'primary',
    target      TEXT NOT NULL DEFAULT '',
    created_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
CREATE INDEX IF NOT EXISTS idx_domains_site_id ON domains(site_id);
CREATE INDEX IF NOT EXISTS idx_domains_owner_id ON domains(owner_id);

CREATE TABLE IF NOT EXISTS php_versions (
    id              INTEGER PRIMARY KEY,
    version         TEXT NOT NULL,
    image           TEXT NOT NULL,
    enabled         INTEGER NOT NULL DEFAULT 1,
    default_version INTEGER NOT NULL DEFAULT 0,
    created_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);

CREATE TABLE IF NOT EXISTS databases (
    id          INTEGER PRIMARY KEY,
    db_name     TEXT NOT NULL,
    db_user     TEXT NOT NULL DEFAULT '',
    db_password TEXT NOT NULL DEFAULT '',
    engine      TEXT NOT NULL DEFAULT 'mariadb',
    version     TEXT NOT NULL DEFAULT 'lts',
    owner_id    INTEGER NOT NULL DEFAULT 0,
    site_id     INTEGER NOT NULL DEFAULT 0,
    enabled     INTEGER NOT NULL DEFAULT 1,
    created_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
CREATE INDEX IF NOT EXISTS idx_databases_site_id ON databases(site_id);

CREATE TABLE IF NOT EXISTS backups (
    id          INTEGER PRIMARY KEY,
    site_id     INTEGER NOT NULL DEFAULT 0,
    owner_id    INTEGER NOT NULL DEFAULT 0,
    filename    TEXT NOT NULL,
    type        TEXT NOT NULL DEFAULT 'manual',
    size        INTEGER NOT NULL DEFAULT 0,
    created_at  TEXT NOT NULL,
    status      TEXT NOT NULL DEFAULT 'completed',
    file_path   TEXT NOT NULL DEFAULT '',
    compression TEXT NOT NULL DEFAULT 'gzip',
    updated_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
CREATE INDEX IF NOT EXISTS idx_backups_site_id ON backups(site_id);

CREATE TABLE IF NOT EXISTS ssl_certificates (
    id                INTEGER PRIMARY KEY,
    domain_id         INTEGER NOT NULL DEFAULT 0,
    domain            TEXT NOT NULL,
    provider          TEXT NOT NULL DEFAULT 'placeholder',
    certificate_path  TEXT NOT NULL DEFAULT '',
    key_path          TEXT NOT NULL DEFAULT '',
    chain_path        TEXT NOT NULL DEFAULT '',
    issued_at         TEXT NOT NULL DEFAULT '',
    expires_at        TEXT NOT NULL DEFAULT '',
    renew_after       TEXT NOT NULL DEFAULT '',
    status            TEXT NOT NULL DEFAULT 'http_only',
    auto_renew        INTEGER NOT NULL DEFAULT 1,
    https_enabled     INTEGER NOT NULL DEFAULT 0,
    redirect_enabled  INTEGER NOT NULL DEFAULT 0,
    domains           TEXT NOT NULL DEFAULT '',
    challenge_type    TEXT NOT NULL DEFAULT '',
    last_error        TEXT NOT NULL DEFAULT '',
    last_validation   TEXT NOT NULL DEFAULT '',
    renew_attempts    INTEGER NOT NULL DEFAULT 0,
    version           INTEGER NOT NULL DEFAULT 1,
    created_at        TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at        TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
CREATE INDEX IF NOT EXISTS idx_ssl_domain_id ON ssl_certificates(domain_id);

CREATE TABLE IF NOT EXISTS mail_domains (
    id                  INTEGER PRIMARY KEY,
    domain_id           INTEGER NOT NULL DEFAULT 0,
    site_id             INTEGER NOT NULL DEFAULT 0,
    domain_name         TEXT NOT NULL,
    mode                TEXT NOT NULL DEFAULT 'disabled',
    relay_host          TEXT NOT NULL DEFAULT '',
    dkim_selector       TEXT NOT NULL DEFAULT 'dkim',
    dkim_private_key_path TEXT NOT NULL DEFAULT '',
    dkim_public_key_dns TEXT NOT NULL DEFAULT '',
    max_mailboxes       INTEGER NOT NULL DEFAULT 0,
    max_aliases         INTEGER NOT NULL DEFAULT 0,
    catch_all           TEXT NOT NULL DEFAULT '',
    enabled             INTEGER NOT NULL DEFAULT 1,
    created_at          TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at          TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
CREATE INDEX IF NOT EXISTS idx_mail_domains_domain_id ON mail_domains(domain_id);
CREATE INDEX IF NOT EXISTS idx_mail_domains_site_id ON mail_domains(site_id);

CREATE TABLE IF NOT EXISTS mail_mailboxes (
    id              INTEGER PRIMARY KEY,
    domain_id       INTEGER NOT NULL REFERENCES mail_domains(id) ON DELETE RESTRICT,
    local_part      TEXT NOT NULL,
    password_hash   TEXT NOT NULL DEFAULT '',
    quota_bytes     INTEGER NOT NULL DEFAULT 0,
    quota_messages  INTEGER NOT NULL DEFAULT 0,
    enabled         INTEGER NOT NULL DEFAULT 1,
    display_name    TEXT NOT NULL DEFAULT '',
    forward_to      TEXT NOT NULL DEFAULT '',
    spam_enabled    INTEGER NOT NULL DEFAULT 1,
    last_login      TEXT NOT NULL DEFAULT '',
    created_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
CREATE INDEX IF NOT EXISTS idx_mailboxes_domain_id ON mail_mailboxes(domain_id);

CREATE TABLE IF NOT EXISTS mail_aliases (
    id                INTEGER PRIMARY KEY,
    domain_id         INTEGER NOT NULL REFERENCES mail_domains(id) ON DELETE RESTRICT,
    source_local_part TEXT NOT NULL,
    destination       TEXT NOT NULL,
    enabled           INTEGER NOT NULL DEFAULT 1,
    created_at        TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at        TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
CREATE INDEX IF NOT EXISTS idx_aliases_domain_id ON mail_aliases(domain_id);

CREATE TABLE IF NOT EXISTS access_users (
    id            INTEGER PRIMARY KEY,
    username      TEXT NOT NULL,
    auth_type     TEXT NOT NULL DEFAULT 'password',
    password_hash TEXT NOT NULL DEFAULT '',
    enabled       INTEGER NOT NULL DEFAULT 1,
    created_at    TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at    TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);

CREATE TABLE IF NOT EXISTS access_grants (
    id              INTEGER PRIMARY KEY,
    access_user_id  INTEGER NOT NULL REFERENCES access_users(id) ON DELETE RESTRICT,
    site_id         INTEGER NOT NULL REFERENCES sites(id) ON DELETE RESTRICT,
    permission      TEXT NOT NULL DEFAULT 'read_write',
    created_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
CREATE INDEX IF NOT EXISTS idx_grants_user_id ON access_grants(access_user_id);
CREATE INDEX IF NOT EXISTS idx_grants_site_id ON access_grants(site_id);

CREATE TABLE IF NOT EXISTS reverse_proxies (
    id          INTEGER PRIMARY KEY,
    domain      TEXT NOT NULL,
    site_id     INTEGER NOT NULL DEFAULT 0,
    provider    TEXT NOT NULL DEFAULT 'nginx',
    config_path TEXT NOT NULL DEFAULT '',
    upstream    TEXT NOT NULL DEFAULT '',
    enabled     INTEGER NOT NULL DEFAULT 1,
    status      TEXT NOT NULL DEFAULT 'active',
    created_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at  TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);
CREATE INDEX IF NOT EXISTS idx_proxies_site_id ON reverse_proxies(site_id);

CREATE TABLE IF NOT EXISTS profiles (
    id              INTEGER PRIMARY KEY,
    profile_name    TEXT NOT NULL,
    type            TEXT NOT NULL DEFAULT 'web_server',
    web_server      TEXT NOT NULL DEFAULT 'apache',
    runtime         TEXT NOT NULL DEFAULT 'docker',
    template_path   TEXT NOT NULL DEFAULT '',
    description     TEXT NOT NULL DEFAULT '',
    enabled         INTEGER NOT NULL DEFAULT 1,
    default_profile INTEGER NOT NULL DEFAULT 0,
    created_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at      TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);

CREATE TABLE IF NOT EXISTS auth_users (
    id                  INTEGER PRIMARY KEY,
    username            TEXT NOT NULL,
    password_hash       TEXT NOT NULL DEFAULT '',
    must_change_password INTEGER NOT NULL DEFAULT 0,
    enabled             INTEGER NOT NULL DEFAULT 1,
    role                TEXT NOT NULL DEFAULT 'admin',
    created_at          TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now')),
    updated_at          TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ','now'))
);

CREATE TABLE IF NOT EXISTS mail_config (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
)SQL";

// Storage metadata values set by this migration.
// These describe the schema state, NOT the active backend.
// TXT remains the active Storage backend until Phase 11.
const char* kMetaSchemaVersion = "1";
const char* kMetaStorageBackend = "sqlite_schema";  // schema exists; TXT is active
const char* kMetaMigrationState = "schema_created";

} // anonymous namespace

void register_all_schema_migrations(MigrationEngine& engine) {
    Migration v1;
    v1.version = 1;
    v1.name = "initial_business_schema";
    // Descriptor is the canonical DDL itself — any DDL change
    // produces a different checksum.
    v1.descriptor = kSchemaV1DDL;
    v1.up = [](SQLiteDB& db, std::string& diagnostics) -> bool {
        // Create all tables and indices
        if (!db.exec(kSchemaV1DDL)) {
            diagnostics = db.error_message();
            return false;
        }

        // Record schema metadata
        auto set_meta = [&](const std::string& key, const std::string& value) -> bool {
            std::string sql = "INSERT OR REPLACE INTO storage_meta (key, value) VALUES ('"
                + key + "', '" + value + "')";
            if (!db.exec(sql)) {
                diagnostics = "Failed to set " + key + ": " + db.error_message();
                return false;
            }
            return true;
        };

        if (!set_meta("storage_backend", kMetaStorageBackend)) return false;
        if (!set_meta("schema_version", kMetaSchemaVersion)) return false;
        if (!set_meta("migration_state", kMetaMigrationState)) return false;

        return true;
    };

    engine.register_migration(std::move(v1));
}

} // namespace containercp::storage
