# Legacy Archive API — Phase 10

## Purpose

The `LegacyArchive` class creates a verified immutable archive of legacy TXT storage files after successful migration verification. Source files are never modified or deleted.

Phase 11 (startup migration gate) is not implemented. Manual invocation only.

## File

`libs/storage/LegacyArchive.h` / `.cpp`

## Public API

```cpp
class LegacyArchive {
public:
    LegacyArchive(const std::string& source_directory,
                  const std::string& archive_root);

    ArchiveResult create_archive(migration_id, source_version, target_version,
                                  DatabaseVerificationResult);

    bool verify_archive(const std::string& archive_path,
                        ArchiveManifest* verified_manifest = nullptr);

    bool set_permissions(const std::string& archive_path);

    static std::string sha256_file(const std::string& path);
    static std::string generate_uuid();
    static std::string timestamp_utc();
};
```

## Result types

```cpp
struct ArchiveFileEntry {
    std::string filename; bool optional, present;
    uint64_t size, record_count;
    std::string sha256;
};

struct ArchiveManifest {
    std::string manifest_version, migration_id;
    std::string source_version, target_version;
    std::string migration_timestamp, source_directory, archive_directory;
    std::vector<ArchiveFileEntry> files;
    bool checksum_match;
    std::string integrity_check, foreign_key_violations, verification_result;
};

struct ArchiveResult {
    bool success; std::string archive_path, error, diagnostics;
    ArchiveManifest manifest;
};
```

## Archive layout

```
<archive_root>/legacy-v0.6.0-20260717T102500Z-<uuid>/
    manifest.json
    SHA256SUMS
    nodes.db
    php_versions.db
    ...
```

## File inventory (19 files)

Required: nodes.db, php_versions.db, profiles.db, users.db, sites.db, domains.db, databases.db, backups.db, reverse_proxies.db

Optional: template_profiles.db, access_users.db, access_grants.db, auth_users.db, ssl_certificates.db, mail_domains.db, mail_mailboxes.db, mail_aliases.db, mail_state.db, mail_smarthost.db

## Atomic publication

1. Validate inputs + disk space
2. Create temp directory (.tmp suffix)
3. Copy all files + verify checksums + detect source mutation
4. Write manifest.json + SHA256SUMS
5. fsync
6. Atomic rename temp → final path

Failure → temp removed, source untouched.

## verify_archive

Parses SHA256SUMS, validates each file checksum independently, rejects unknown .db files.

## Permissions

Archive directory: 0700. Files: 0440.

## No source deletion

Source TXT files are never modified or deleted. Phase 11 not started.
