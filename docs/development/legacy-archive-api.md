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
    static bool safe_version(const std::string& v);
    static bool valid_migration_id(const std::string& id);
    static bool valid_timestamp(const std::string& ts);
    static std::string json_escape(const std::string& s);
    static std::string normalize_archive_identity_path(const std::string& path);
    static RecordCountResult count_records(const std::string& source_dir,
                                           const std::string& filename);

private:
    bool verify_archive_internal(const std::string& physical_archive_path,
                                 const std::string* expected_manifest_archive_path,
                                 ArchiveManifest* verified_manifest);
};
```

### verify_archive — Public API (preserved)

```cpp
bool verify_archive(const std::string& archive_path,
                    ArchiveManifest* verified_manifest = nullptr);
```

Validates a complete archive: parses SHA256SUMS, verifies each file checksum independently, parses and validates manifest.json including all 19 file entries, checks archive directory content, rejects symlinks/sockets/FIFOs, validates manifest fields semantically (timestamp format, UUID, versions, archive_directory identity).

When `verified_manifest` is provided, fills the complete parsed manifest.

The `archive_directory` field in the manifest is compared against `normalize_archive_identity_path(archive_path)`. Paths with or without trailing slash are equivalent.

**Two-argument call remains source-compatible:**
```cpp
ArchiveManifest manifest;
archive.verify_archive(path, &manifest);
```

### verify_archive_internal — Private helper

```cpp
bool verify_archive_internal(physical_path, expected_manifest_archive_path*, manifest*);
```

Used internally by `create_archive()` for pre-publication and post-publication verification. When `expected_manifest_archive_path` is provided, the manifest's `archive_directory` is compared against it (the final normalized archive path), while file operations use the physical path. This allows pre-publication verification where the manifest identifies the future final location but files reside in a temporary directory.

### normalize_archive_identity_path — Public static helper

```cpp
static std::string normalize_archive_identity_path(const std::string& path);
```

Normalizes an archive identity path for comparison:
- Rejects empty paths → empty string
- Rejects `..` components (before and after `lexically_normal()`) → empty string  
- Removes trailing `/` and `\`
- Normalizes `.` components via `std::filesystem::path::lexically_normal()`

**Does not require the path to exist on disk.** Used in the constructor, manifest `archive_directory`, and verification identity comparison.

## Manifest format (manifest.json)

```json
{
    "manifest_version": "1.0",
    "migration_id": "<UUID v4 lowercase>",
    "source_version": "v<major>.<minor>.<patch>[-suffix]",
    "target_version": "v<major>.<minor>.<patch>[-suffix]",
    "migration_timestamp": "YYYYMMDDTHHMMSSZ",
    "source_directory": "<non-empty>",
    "archive_directory": "<normalized absolute path>",
    "checksum_match": true,
    "initial_integrity_check": "ok",
    "reopened_integrity_check": "ok",
    "initial_fk_violations": 0,
    "reopened_fk_violations": 0,
    "verification_result": "success",
    "files": [ ... 19 entries ... ]
}
```

### Manifest file entries

Each file entry has 6 required fields:
```json
{
    "filename": "nodes.db",
    "sha256": "<64-char lowercase hex>",
    "size": <uint64>,
    "record_count": <uint64>,
    "optional": <bool>,
    "present": <bool>
}
```

All 6 fields are validated for presence. Duplicate, unknown, or missing fields are rejected.

### Manifest JSON parser

Strict JSON parser in `LegacyArchive.cpp`:
- No leading/trailing commas
- Known keys only (10 top-level string, 1 bool, 2 int, 6 per file entry)
- Typed value parsing per key (strings/ints/bools checked against key type)
- Rejects leading-zero integers (JSON grammar: only `0` or non-zero lead digit)
- Rejects lone surrogate code points (U+D800–U+DFFF) in `\uXXXX` escapes
- INT64_MIN safe: explicit check for `-(INT64_MAX+1)` case

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
    std::string initial_integrity_check, reopened_integrity_check;
    int initial_fk_violations, reopened_fk_violations;
    std::string verification_result;
};

struct ArchiveResult {
    bool success; std::string archive_path, error, diagnostics;
    ArchiveManifest manifest;
};
```

## Archive layout

```
<archive_root>/legacy-v<version>-<timestamp>-<uuid>/
    manifest.json
    SHA256SUMS
    nodes.db
    php_versions.db
    ...
```

`archive_directory` in manifest stores the normalized absolute final path (no trailing slash).

## File inventory (19 files)

Required: nodes.db, php_versions.db, profiles.db, users.db, sites.db, domains.db, databases.db, backups.db, reverse_proxies.db

Optional: template_profiles.db, access_users.db, access_grants.db, auth_users.db, ssl_certificates.db, mail_domains.db, mail_mailboxes.db, mail_aliases.db, mail_state.db, mail_smarthost.db

## Atomic publication

1. Validate inputs + disk space
2. Create exclusive temp directory (`.tmp` suffix), fail if exists
3. Copy all files via durable_copy:
   - Open source with O_NOFOLLOW (rejects symlinks atomically)
   - fstat for inode/mtime/size
   - Fast-forward read with EINTR retry to verify size
   - Exclusive O_EXCL create destination
   - Stream copy with EINTR + partial-write handling (write()==0 rejected)
   - fsync + close destination
   - Recheck source via O_NOFOLLOW fd: SHA + fstat (no path-following)
   - close(src_fd) return value checked
4. Write manifest.json + SHA256SUMS
5. fsync all files and directories
6. Pre-publication verification: verify_archive_internal(temp_path, &manifest_final_path, nullptr)
7. Set permissions: dir 0700, files 0440
8. Atomic rename temp → final path
9. Archive-root fsync
10. Post-publication verification: verify_archive_internal(final_path, &manifest_final_path, nullptr)

Failure → temp removed, source untouched.

After rename failure → quarantine (rename final to .corrupted.). Source untouched.

## verify_archive

Parses SHA256SUMS, validates each file checksum independently, parses manifest with strict JSON parser, validates:
- All 10 string fields semantically (including timestamp format YYYYMMDDTHHMMSSZ)
- Boolean checksum_match
- Integer FK violation counts
- All 19 file entries cross-checked against physical files and SHA256SUMS
- Exact directory content (no extra files, no symlinks, no directories)
- `archive_directory` matches normalized archive path

## Permissions

Archive directory: 0700. Files: 0440.

## Idempotency

Creating an archive with an existing migration ID returns `migration_id_already_archived` if the existing archive passes verification, or `existing_archive_invalid` if it fails. Creates a distinct archive for a different migration ID. Corrupt manifest in idempotency check returns `existing_archive_corrupt`.

## No source deletion

Source TXT files are never modified or deleted. Phase 11 not started.

## Known limitations

- Unicode surrogate PAIRS not supported in \uXXXX (lone surrogates rejected)
- Phase 11 startup migration gate not implemented
- Archive retention/deletion not implemented
- Manual invocation only
- SQLite explicit-only (no implicit activation)
