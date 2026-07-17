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
                if (rl->error_code() != 0) { r.error = "step_failed:" + rl->error_message(); return r; } \
                break; \
            } \
            cls rec; \
            if (!reader(rl.db(), rec)) { r.error = "row_convert_failed"; return r; } \
            r.records.push_back(std::move(rec)); \
        } \
        r.success = true; return r; \
    }

#define RD_NODE(db, n) (n.id=static_cast<uint64_t>(db.column_int(0)), n.name=db.column_text(1), n.type=db.column_text(2), true)
#define RD_PHP(db, pv) (pv.id=static_cast<uint64_t>(db.column_int(0)), pv.version=db.column_text(1), pv.image=db.column_text(2), pv.enabled=(db.column_int(3)!=0), pv.default_version=(db.column_int(4)!=0), pv.name=pv.version, true)
#define RD_PROFILE(db, p) (p.id=static_cast<uint64_t>(db.column_int(0)), p.profile_name=db.column_text(1), p.type=profile::profile_type_from_string(db.column_text(2)), p.web_server=db.column_text(3), p.runtime=db.column_text(4), p.template_path=db.column_text(5), p.description=db.column_text(6), p.enabled=(db.column_int(7)!=0), p.default_profile=(db.column_int(8)!=0), p.name=p.profile_name, true)
#define RD_USER(db, u) (u.id=static_cast<uint64_t>(db.column_int(0)), u.username=db.column_text(1), u.uid=static_cast<uint64_t>(db.column_int(2)), u.home_directory=db.column_text(3), u.shell=db.column_text(4), u.enabled=(db.column_int(5)!=0), u.name=u.username, true)
#define RD_SITE(db, s) (s.id=static_cast<uint64_t>(db.column_int(0)), s.domain=db.column_text(1), s.owner=db.column_text(2), s.node_id=static_cast<uint64_t>(db.column_int(3)), s.web_server=db.column_text(4), s.php_mail_enabled=(db.column_int(5)!=0), s.php_mail_enabled_present=true, s.name=s.domain, true)
#define RD_DOMAIN(db, d) (d.id=static_cast<uint64_t>(db.column_int(0)), d.fqdn=db.column_text(1), d.owner_id=static_cast<uint64_t>(db.column_int(2)), d.site_id=static_cast<uint64_t>(db.column_int(3)), d.php_version=db.column_text(4), d.ssl_enabled=(db.column_int(5)!=0), d.enabled=(db.column_int(6)!=0), d.type=db.column_text(7), d.target=db.column_text(8), d.name=d.fqdn, true)
#define RD_DB(db, d) (d.id=static_cast<uint64_t>(db.column_int(0)), d.db_name=db.column_text(1), d.db_user=db.column_text(2), d.db_password=db.column_text(3), d.engine=db.column_text(4), d.version=db.column_text(5), d.owner_id=static_cast<uint64_t>(db.column_int(6)), d.site_id=static_cast<uint64_t>(db.column_int(7)), d.enabled=(db.column_int(8)!=0), d.name=d.db_name, true)
#define RD_BACKUP(db, b) (b.id=static_cast<uint64_t>(db.column_int(0)), b.site_id=static_cast<uint64_t>(db.column_int(1)), b.owner_id=static_cast<uint64_t>(db.column_int(2)), b.filename=db.column_text(3), b.type=db.column_text(4), b.size=static_cast<uint64_t>(db.column_int(5)), b.created_at=db.column_text(6), b.status=db.column_text(7), b.file_path=db.column_text(8), b.compression=db.column_text(9), b.name=b.filename, true)
#define RD_PROXY(db, p) (p.id=static_cast<uint64_t>(db.column_int(0)), p.domain=db.column_text(1), p.site_id=static_cast<uint64_t>(db.column_int(2)), p.provider=db.column_text(3), p.config_path=db.column_text(4), p.upstream=db.column_text(5), p.enabled=(db.column_int(6)!=0), p.status=db.column_text(7), p.name=p.domain, true)
#define RD_AU(db, u) (u.id=static_cast<uint64_t>(db.column_int(0)), u.username=db.column_text(1), u.auth_type=db.column_text(2), u.password_hash=db.column_text(3), u.enabled=(db.column_int(4)!=0), u.name=u.username, true)
#define RD_AG(db, g) (g.id=static_cast<uint64_t>(db.column_int(0)), g.access_user_id=static_cast<uint64_t>(db.column_int(1)), g.site_id=static_cast<uint64_t>(db.column_int(2)), g.permission=access::permission_from_string(db.column_text(3)), g.name=std::to_string(g.access_user_id)+"-"+std::to_string(g.site_id), true)
#define RD_AUTHU(db, u) (u.id=static_cast<uint64_t>(db.column_int(0)), u.username=db.column_text(1), u.password_hash=db.column_text(2), u.must_change_password=(db.column_int(3)!=0), u.enabled=(db.column_int(4)!=0), u.role=db.column_text(5), u.name=u.username, true)
#define RD_SSL(db, c) (c.id=static_cast<uint64_t>(db.column_int(0)), c.domain_id=static_cast<uint64_t>(db.column_int(1)), c.domain=db.column_text(2), c.provider=db.column_text(3), c.certificate_path=db.column_text(4), c.key_path=db.column_text(5), c.chain_path=db.column_text(6), c.issued_at=db.column_text(7), c.expires_at=db.column_text(8), c.renew_after=db.column_text(9), c.status=db.column_text(10), c.auto_renew=(db.column_int(11)!=0), c.https_enabled=(db.column_int(12)!=0), c.redirect_enabled=(db.column_int(13)!=0), c.domains=db.column_text(14), c.challenge_type=db.column_text(15), c.last_error=db.column_text(16), c.last_validation=db.column_text(17), c.renew_attempts=static_cast<int>(db.column_int(18)), c.version=static_cast<int>(db.column_int(19)), c.name=c.domain, true)
#define RD_MD(db, m) (m.id=static_cast<uint64_t>(db.column_int(0)), m.domain_id=static_cast<uint64_t>(db.column_int(1)), m.site_id=static_cast<uint64_t>(db.column_int(2)), m.domain_name=db.column_text(3), m.mode=mail::mail_domain_mode_from_string(db.column_text(4)), m.relay_host=db.column_text(5), m.dkim_selector=db.column_text(6), m.dkim_private_key_path=db.column_text(7), m.dkim_public_key_dns=db.column_text(8), m.max_mailboxes=static_cast<uint64_t>(db.column_int(9)), m.max_aliases=static_cast<uint64_t>(db.column_int(10)), m.catch_all=db.column_text(11), m.enabled=(db.column_int(12)!=0), m.created_at=db.column_text(13), m.updated_at=db.column_text(14), m.name=m.domain_name, true)
#define RD_MB(db, mb) (mb.id=static_cast<uint64_t>(db.column_int(0)), mb.domain_id=static_cast<uint64_t>(db.column_int(1)), mb.local_part=db.column_text(2), mb.password_hash=db.column_text(3), mb.quota_bytes=static_cast<uint64_t>(db.column_int(4)), mb.quota_messages=static_cast<uint64_t>(db.column_int(5)), mb.enabled=(db.column_int(6)!=0), mb.display_name=db.column_text(7), mb.forward_to=db.column_text(8), mb.spam_enabled=(db.column_int(9)!=0), mb.last_login=db.column_text(10), mb.created_at=db.column_text(11), mb.updated_at=db.column_text(12), mb.name=mb.local_part, true)
#define RD_MA(db, a) (a.id=static_cast<uint64_t>(db.column_int(0)), a.domain_id=static_cast<uint64_t>(db.column_int(1)), a.source_local_part=db.column_text(2), a.destination=db.column_text(3), a.enabled=(db.column_int(4)!=0), a.created_at=db.column_text(5), a.updated_at=db.column_text(6), a.name=a.source_local_part, true)

    SNAPSHOT_DEF(nodes, node::Node, "SELECT id, name, type FROM nodes ORDER BY id", RD_NODE)
    SNAPSHOT_DEF(php_versions, php::PhpVersion, "SELECT id, version, image, enabled, default_version FROM php_versions ORDER BY id", RD_PHP)
    SNAPSHOT_DEF(profiles, profile::Profile, "SELECT id, profile_name, type, web_server, runtime, template_path, description, enabled, default_profile FROM profiles ORDER BY id", RD_PROFILE)
    SNAPSHOT_DEF(users, user::User, "SELECT id, username, uid, home_directory, shell, enabled FROM users ORDER BY id", RD_USER)
    SNAPSHOT_DEF(sites, site::Site, "SELECT id, domain, owner, node_id, web_server, php_mail_enabled FROM sites ORDER BY id", RD_SITE)
    SNAPSHOT_DEF(domains, domain::Domain, "SELECT id, fqdn, owner_id, site_id, php_version, ssl_enabled, enabled, type, target FROM domains ORDER BY id", RD_DOMAIN)
    SNAPSHOT_DEF(databases, database::Database, "SELECT id, db_name, db_user, db_password, engine, version, owner_id, site_id, enabled FROM databases ORDER BY id", RD_DB)
    SNAPSHOT_DEF(backups, backup::Backup, "SELECT id, site_id, owner_id, filename, type, size, created_at, status, file_path, compression FROM backups ORDER BY id", RD_BACKUP)
    SNAPSHOT_DEF(reverse_proxies, proxy::ReverseProxy, "SELECT id, domain, site_id, provider, config_path, upstream, enabled, status FROM reverse_proxies ORDER BY id", RD_PROXY)
    SNAPSHOT_DEF(access_users, access::AccessUser, "SELECT id, username, auth_type, password_hash, enabled FROM access_users ORDER BY id", RD_AU)
    SNAPSHOT_DEF(access_grants, access::AccessGrant, "SELECT id, access_user_id, site_id, permission FROM access_grants ORDER BY id", RD_AG)
    SNAPSHOT_DEF(auth_users, auth::AuthUser, "SELECT id, username, password_hash, must_change_password, enabled, role FROM auth_users ORDER BY id", RD_AUTHU)
    SNAPSHOT_DEF(ssl_certificates, ssl::SslCertificate, "SELECT id, domain_id, domain, provider, certificate_path, key_path, chain_path, issued_at, expires_at, renew_after, status, auto_renew, https_enabled, redirect_enabled, domains, challenge_type, last_error, last_validation, renew_attempts, version FROM ssl_certificates ORDER BY id", RD_SSL)
    SNAPSHOT_DEF(mail_domains, mail::MailDomain, "SELECT id, domain_id, site_id, domain_name, mode, relay_host, dkim_selector, dkim_private_key_path, dkim_public_key_dns, max_mailboxes, max_aliases, catch_all, enabled, created_at, updated_at FROM mail_domains ORDER BY id", RD_MD)
    SNAPSHOT_DEF(mail_mailboxes, mail::Mailbox, "SELECT id, domain_id, local_part, password_hash, quota_bytes, quota_messages, enabled, display_name, forward_to, spam_enabled, last_login, created_at, updated_at FROM mail_mailboxes ORDER BY id", RD_MB)
    SNAPSHOT_DEF(mail_aliases, mail::MailAlias, "SELECT id, domain_id, source_local_part, destination, enabled, created_at, updated_at FROM mail_aliases ORDER BY id", RD_MA)

    // mail_config: returns CheckedOptionalValue per key
    CheckedOptionalValue read_mail_config_key(const std::string& key) const {
        CheckedOptionalValue r;
        ReadLease rl(pool_);
        if (!rl.is_valid()) { r.error = "no_lease"; return r; }
        std::string sql = "SELECT value FROM mail_config WHERE key = ?";
        if (!rl->prepare(sql)) { r.error = "prepare:" + key; return r; }
        if (!rl->bind_text(1, key)) { r.error = "bind:" + key; return r; }
        if (rl->step()) {
            r.present = true; r.value = rl->column_text(0);
            if (rl->step()) { r.error = "unexpected_second_row:" + key; return r; }
        } else {
            if (rl->error_code() != 0) { r.error = "step_error:" + key; return r; }
            // DONE — key absent
        }
        r.success = true; return r;
    }

private:
    ConnectionPool& pool_;
};

} // namespace containercp::storage
#endif
