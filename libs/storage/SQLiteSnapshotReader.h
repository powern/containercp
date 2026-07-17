#ifndef CONTAINERCP_STORAGE_SQLITE_SNAPSHOT_READER_H
#define CONTAINERCP_STORAGE_SQLITE_SNAPSHOT_READER_H

#include "ConnectionPool.h"
#include "access/AccessGrant.h"
#include "access/AccessUser.h"
#include "auth/AuthUser.h"
#include "backup/Backup.h"
#include "database/Database.h"
#include "domain/Domain.h"
#include "mail/MailAlias.h"
#include "mail/MailDomain.h"
#include "mail/Mailbox.h"
#include "node/Node.h"
#include "php/PhpVersion.h"
#include "profile/Profile.h"
#include "proxy/ReverseProxy.h"
#include "site/Site.h"
#include "ssl/SslCertificate.h"
#include "user/User.h"

#include <string>
#include <vector>

namespace containercp::storage {

template<typename T>
struct CheckedSnapshot {
    bool success = false;
    std::vector<T> records;
    std::string error;
};

struct CheckedOptionalValue {
    bool success = false;
    bool present = false;
    std::string value;
    std::string error;
};

// Checked typed SQLite snapshot reader.
// Every load distinguishes connection/prepare/step/row-conversion/DONE failure
// from a successful empty table result.
struct SQLiteSnapshotReader {
    explicit SQLiteSnapshotReader(ConnectionPool& pool) : pool_(pool) {}

#define SNAPSHOT_DEF(type, cls, sql, reader) \
    CheckedSnapshot<cls> read_##type() { \
        CheckedSnapshot<cls> r; \
        ReadLease rl(pool_); \
        if (!rl.is_valid()) { r.error = "no_lease"; return r; } \
        if (!rl->prepare(sql)) { r.error = "prepare_failed"; return r; } \
        while (true) { \
            if (!rl->step()) { \
                if (rl->error_code() != 0) { r.error = "step_failed"; return r; } \
                break; \
            } \
            cls rec; \
            if (!reader(rl.db(), rec)) { r.error = "row_convert_failed"; return r; } \
            r.records.push_back(std::move(rec)); \
        } \
        r.success = true; return r; \
    }


    // Row conversion with validation
    static bool read_uint64_col(SQLiteDB& db, int col, uint64_t& out) {
        if (db.column_is_null(col)) return false;
        int64_t v = db.column_int(col);
        if (v < 0) return false;
        out = static_cast<uint64_t>(v);
        return true;
    }
    static bool read_bool_col(SQLiteDB& db, int col, bool& out) {
        int64_t v = db.column_int(col);
        if (v == 0) { out = false; return true; }
        if (v == 1) { out = true; return true; }
        return false; // invalid boolean
    }
    static bool read_string_col(SQLiteDB& db, int col, std::string& out, bool allow_null = false) {
        if (db.column_is_null(col) && !allow_null) return false;
        out = db.column_text(col); return true;
    }
    static bool valid_profile_type(const std::string& s) {
        return s=="web_server"||s=="php"||s=="docker"||s=="ssl"||s=="backup"||s=="mail"||s=="dns";
    }
    static bool valid_permission(const std::string& s) {
        return s=="read_only"||s=="read_write"||s=="deploy";
    }
    static bool valid_mail_mode(const std::string& s) {
        return s=="disabled"||s=="local-primary"||s=="external-relay"||s=="split-m365";
    }

    // Actual row readers with validation
    static bool read_node_row(SQLiteDB& db, node::Node& n) {
        if (!read_uint64_col(db, 0, n.id)) return false;
        n.name = db.column_text(1); n.type = db.column_text(2); return true; }
    static bool read_profile_row(SQLiteDB& db, profile::Profile& p) {
        if (!read_uint64_col(db, 0, p.id)) return false;
        p.profile_name = db.column_text(1);
        std::string ts = db.column_text(2); if (!valid_profile_type(ts)) return false;
        p.type = profile::profile_type_from_string(ts);
        p.web_server = db.column_text(3); p.runtime = db.column_text(4); p.template_path = db.column_text(5);
        p.description = db.column_text(6); p.enabled = (db.column_int(7) != 0); p.default_profile = (db.column_int(8) != 0);
        p.name = p.profile_name; return true; }
    static bool read_ag_row(SQLiteDB& db, access::AccessGrant& g) {
        if (!read_uint64_col(db, 0, g.id)) return false;
        if (!read_uint64_col(db, 1, g.access_user_id)) return false;
        if (!read_uint64_col(db, 2, g.site_id)) return false;
        std::string ps = db.column_text(3); if (!valid_permission(ps)) return false;
        g.permission = access::permission_from_string(ps);
        g.name = std::to_string(g.access_user_id)+"-"+std::to_string(g.site_id); return true; }
    static bool read_md_row(SQLiteDB& db, mail::MailDomain& m) {
        if (!read_uint64_col(db, 0, m.id)) return false;
        if (!read_uint64_col(db, 1, m.domain_id)) return false;
        if (!read_uint64_col(db, 2, m.site_id)) return false;
        m.domain_name = db.column_text(3);
        std::string ms = db.column_text(4); if (!valid_mail_mode(ms)) return false;
        m.mode = mail::mail_domain_mode_from_string(ms);
        m.relay_host = db.column_text(5); m.dkim_selector = db.column_text(6);
        m.dkim_private_key_path = db.column_text(7); m.dkim_public_key_dns = db.column_text(8);
        if (!read_uint64_col(db, 9, m.max_mailboxes)) return false;
        if (!read_uint64_col(db, 10, m.max_aliases)) return false;
        m.catch_all = db.column_text(11); m.enabled = (db.column_int(12) != 0);
        m.created_at = db.column_text(13); m.updated_at = db.column_text(14);
        m.name = m.domain_name; return true; }

    static bool read_php_row(SQLiteDB& db, php::PhpVersion& pv) {
        if (!read_uint64_col(db, 0, pv.id)) return false;
        if (!read_string_col(db, 1, pv.version)) return false;
        if (!read_string_col(db, 2, pv.image)) return false;
        if (!read_bool_col(db, 3, pv.enabled)) return false;
        if (!read_bool_col(db, 4, pv.default_version)) return false;
        pv.name = pv.version; return true; }
    static bool read_user_row(SQLiteDB& db, user::User& u) {
        if (!read_uint64_col(db, 0, u.id)) return false;
        if (!read_string_col(db, 1, u.username)) return false;
        if (!read_uint64_col(db, 2, u.uid)) return false;
        if (!read_string_col(db, 3, u.home_directory)) return false;
        if (!read_string_col(db, 4, u.shell)) return false;
        if (!read_bool_col(db, 5, u.enabled)) return false;
        u.name = u.username; return true; }
    static bool read_site_row(SQLiteDB& db, site::Site& s) {
        if (!read_uint64_col(db, 0, s.id)) return false;
        if (!read_string_col(db, 1, s.domain)) return false;
        if (!read_string_col(db, 2, s.owner)) return false;
        if (!read_uint64_col(db, 3, s.node_id)) return false;
        if (!read_string_col(db, 4, s.web_server)) return false;
        if (!read_bool_col(db, 5, s.php_mail_enabled)) return false;
        s.php_mail_enabled_present = true; s.name = s.domain; return true; }
    static bool read_domain_row(SQLiteDB& db, domain::Domain& d) {
        if (!read_uint64_col(db, 0, d.id)) return false;
        if (!read_string_col(db, 1, d.fqdn)) return false;
        if (!read_uint64_col(db, 2, d.owner_id)) return false;
        if (!read_uint64_col(db, 3, d.site_id)) return false;
        if (!read_string_col(db, 4, d.php_version)) return false;
        if (!read_bool_col(db, 5, d.ssl_enabled)) return false;
        if (!read_bool_col(db, 6, d.enabled)) return false;
        if (!read_string_col(db, 7, d.type)) return false;
        if (!read_string_col(db, 8, d.target, true)) return false;
        d.name = d.fqdn; return true; }
    static bool read_db_row(SQLiteDB& db, database::Database& d) {
        if (!read_uint64_col(db, 0, d.id)) return false;
        if (!read_string_col(db, 1, d.db_name)) return false;
        if (!read_string_col(db, 2, d.db_user)) return false;
        if (!read_string_col(db, 3, d.db_password)) return false;
        if (!read_string_col(db, 4, d.engine)) return false;
        if (!read_string_col(db, 5, d.version)) return false;
        if (!read_uint64_col(db, 6, d.owner_id)) return false;
        if (!read_uint64_col(db, 7, d.site_id)) return false;
        if (!read_bool_col(db, 8, d.enabled)) return false;
        d.name = d.db_name; return true; }
    static bool read_backup_row(SQLiteDB& db, backup::Backup& b) {
        if (!read_uint64_col(db, 0, b.id)) return false;
        if (!read_uint64_col(db, 1, b.site_id)) return false;
        if (!read_uint64_col(db, 2, b.owner_id)) return false;
        if (!read_string_col(db, 3, b.filename)) return false;
        if (!read_string_col(db, 4, b.type)) return false;
        if (!read_uint64_col(db, 5, b.size)) return false;
        if (!read_string_col(db, 6, b.created_at)) return false;
        if (!read_string_col(db, 7, b.status)) return false;
        if (!read_string_col(db, 8, b.file_path)) return false;
        if (!read_string_col(db, 9, b.compression)) return false;
        b.name = b.filename; return true; }
    static bool read_proxy_row(SQLiteDB& db, proxy::ReverseProxy& p) {
        if (!read_uint64_col(db, 0, p.id)) return false;
        if (!read_string_col(db, 1, p.domain)) return false;
        if (!read_uint64_col(db, 2, p.site_id)) return false;
        if (!read_string_col(db, 3, p.provider)) return false;
        if (!read_string_col(db, 4, p.config_path)) return false;
        if (!read_string_col(db, 5, p.upstream)) return false;
        if (!read_bool_col(db, 6, p.enabled)) return false;
        if (!read_string_col(db, 7, p.status)) return false;
        p.name = p.domain; return true; }
    static bool read_au_row(SQLiteDB& db, access::AccessUser& u) {
        if (!read_uint64_col(db, 0, u.id)) return false;
        if (!read_string_col(db, 1, u.username)) return false;
        if (!read_string_col(db, 2, u.auth_type)) return false;
        if (!read_string_col(db, 3, u.password_hash)) return false;
        if (!read_bool_col(db, 4, u.enabled)) return false;
        u.name = u.username; return true; }
    static bool read_authu_row(SQLiteDB& db, auth::AuthUser& u) {
        if (!read_uint64_col(db, 0, u.id)) return false;
        if (!read_string_col(db, 1, u.username)) return false;
        if (!read_string_col(db, 2, u.password_hash)) return false;
        if (!read_bool_col(db, 3, u.must_change_password)) return false;
        if (!read_bool_col(db, 4, u.enabled)) return false;
        if (!read_string_col(db, 5, u.role)) return false;
        u.name = u.username; return true; }
    static bool read_ssl_row(SQLiteDB& db, ssl::SslCertificate& c) {
        if (!read_uint64_col(db, 0, c.id)) return false;
        if (!read_uint64_col(db, 1, c.domain_id)) return false;
        if (!read_string_col(db, 2, c.domain)) return false;
        if (!read_string_col(db, 3, c.provider)) return false;
        if (!read_string_col(db, 4, c.certificate_path)) return false;
        if (!read_string_col(db, 5, c.key_path)) return false;
        if (!read_string_col(db, 6, c.chain_path)) return false;
        c.issued_at = db.column_text(7); c.expires_at = db.column_text(8);
        c.renew_after = db.column_text(9); c.status = db.column_text(10);
        if (!read_bool_col(db, 11, c.auto_renew)) return false;
        if (!read_bool_col(db, 12, c.https_enabled)) return false;
        if (!read_bool_col(db, 13, c.redirect_enabled)) return false;
        if (!read_string_col(db, 14, c.domains)) return false;
        c.challenge_type = db.column_text(15); c.last_error = db.column_text(16);
        c.last_validation = db.column_text(17);
        c.renew_attempts = static_cast<int>(db.column_int(18));
        c.version = static_cast<int>(db.column_int(19));
        c.name = c.domain; return true; }
    static bool read_mb_row(SQLiteDB& db, mail::Mailbox& mb) {
        if (!read_uint64_col(db, 0, mb.id)) return false;
        if (!read_uint64_col(db, 1, mb.domain_id)) return false;
        if (!read_string_col(db, 2, mb.local_part)) return false;
        if (!read_string_col(db, 3, mb.password_hash)) return false;
        if (!read_uint64_col(db, 4, mb.quota_bytes)) return false;
        if (!read_uint64_col(db, 5, mb.quota_messages)) return false;
        if (!read_bool_col(db, 6, mb.enabled)) return false;
        if (!read_string_col(db, 7, mb.display_name, true)) return false;
        if (!read_string_col(db, 8, mb.forward_to, true)) return false;
        if (!read_bool_col(db, 9, mb.spam_enabled)) return false;
        mb.last_login = db.column_text(10); mb.created_at = db.column_text(11);
        mb.updated_at = db.column_text(12); mb.name = mb.local_part; return true; }
    static bool read_ma_row(SQLiteDB& db, mail::MailAlias& a) {
        if (!read_uint64_col(db, 0, a.id)) return false;
        if (!read_uint64_col(db, 1, a.domain_id)) return false;
        if (!read_string_col(db, 2, a.source_local_part)) return false;
        if (!read_string_col(db, 3, a.destination)) return false;
        if (!read_bool_col(db, 4, a.enabled)) return false;
        a.created_at = db.column_text(5); a.updated_at = db.column_text(6);
        a.name = a.source_local_part; return true; }

    SNAPSHOT_DEF(nodes, node::Node, "SELECT id, name, type FROM nodes ORDER BY id", read_node_row)
    SNAPSHOT_DEF(php_versions, php::PhpVersion, "SELECT id, version, image, enabled, default_version FROM php_versions ORDER BY id", read_php_row)
    SNAPSHOT_DEF(profiles, profile::Profile, "SELECT id, profile_name, type, web_server, runtime, template_path, description, enabled, default_profile FROM profiles ORDER BY id", read_profile_row)
    SNAPSHOT_DEF(users, user::User, "SELECT id, username, uid, home_directory, shell, enabled FROM users ORDER BY id", read_user_row)
    SNAPSHOT_DEF(sites, site::Site, "SELECT id, domain, owner, node_id, web_server, php_mail_enabled FROM sites ORDER BY id", read_site_row)
    SNAPSHOT_DEF(domains, domain::Domain, "SELECT id, fqdn, owner_id, site_id, php_version, ssl_enabled, enabled, type, target FROM domains ORDER BY id", read_domain_row)
    SNAPSHOT_DEF(databases, database::Database, "SELECT id, db_name, db_user, db_password, engine, version, owner_id, site_id, enabled FROM databases ORDER BY id", read_db_row)
    SNAPSHOT_DEF(backups, backup::Backup, "SELECT id, site_id, owner_id, filename, type, size, created_at, status, file_path, compression FROM backups ORDER BY id", read_backup_row)
    SNAPSHOT_DEF(reverse_proxies, proxy::ReverseProxy, "SELECT id, domain, site_id, provider, config_path, upstream, enabled, status FROM reverse_proxies ORDER BY id", read_proxy_row)
    SNAPSHOT_DEF(access_users, access::AccessUser, "SELECT id, username, auth_type, password_hash, enabled FROM access_users ORDER BY id", read_au_row)
    SNAPSHOT_DEF(access_grants, access::AccessGrant, "SELECT id, access_user_id, site_id, permission FROM access_grants ORDER BY id", read_ag_row)
    SNAPSHOT_DEF(auth_users, auth::AuthUser, "SELECT id, username, password_hash, must_change_password, enabled, role FROM auth_users ORDER BY id", read_authu_row)
    SNAPSHOT_DEF(ssl_certificates, ssl::SslCertificate, "SELECT id, domain_id, domain, provider, certificate_path, key_path, chain_path, issued_at, expires_at, renew_after, status, auto_renew, https_enabled, redirect_enabled, domains, challenge_type, last_error, last_validation, renew_attempts, version FROM ssl_certificates ORDER BY id", read_ssl_row)
    SNAPSHOT_DEF(mail_domains, mail::MailDomain, "SELECT id, domain_id, site_id, domain_name, mode, relay_host, dkim_selector, dkim_private_key_path, dkim_public_key_dns, max_mailboxes, max_aliases, catch_all, enabled, created_at, updated_at FROM mail_domains ORDER BY id", read_md_row)
    SNAPSHOT_DEF(mail_mailboxes, mail::Mailbox, "SELECT id, domain_id, local_part, password_hash, quota_bytes, quota_messages, enabled, display_name, forward_to, spam_enabled, last_login, created_at, updated_at FROM mail_mailboxes ORDER BY id", read_mb_row)
    SNAPSHOT_DEF(mail_aliases, mail::MailAlias, "SELECT id, domain_id, source_local_part, destination, enabled, created_at, updated_at FROM mail_aliases ORDER BY id", read_ma_row)

    // mail_config: returns CheckedOptionalValue per key
    CheckedOptionalValue read_mail_config_key(const std::string& key) const {
        CheckedOptionalValue r;
        ReadLease rl(pool_);
        if (!rl.is_valid()) { r.error = "no_lease"; return r; }
        if (!rl->prepare("SELECT value FROM mail_config WHERE key = ?")) { r.error = "prepare_failed"; return r; }
        if (!rl->bind_text(1, key)) { r.error = "bind_failed"; return r; }
        if (rl->step()) {
            r.present = true; r.value = rl->column_text(0);
            if (rl->step()) {
                if (rl->error_code() != 0) { r.error = "step_error_second"; return r; }
                r.error = "unexpected_second_row"; return r;
            }
            if (rl->error_code() != 0) { r.error = "step_error_done"; return r; }
        } else {
            if (rl->error_code() != 0) { r.error = "step_error_first"; return r; }
        }
        r.success = true; return r;
    }

private:
    ConnectionPool& pool_;
};

} // namespace containercp::storage
#endif
