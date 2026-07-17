# Legacy Importer API — Phase 9 updates

## Baseline capture (ResourceBaseline)

Before each import, `LegacyImporter::capture_baseline(type)` captures the current SQLite state using `SQLiteSnapshotReader` (checked typed loads). On any snapshot failure, import stops with `baseline_capture_failed` before parsing or writing.

```cpp
struct ResourceBaseline {
    bool success = false;
    uint64_t record_count = 0;
    std::string canonical_checksum;  // SHA-256 via StorageCanonicalizer
    std::string error;
};
```

Canonical format matches `Verification::canonical_*()` exactly via `StorageCanonicalizer`.

## MailConfigResult (presence-aware)

`LegacyDatasetReader::read_mail_config()` returns presence flags determined by file existence, not content emptiness:

```cpp
struct MailConfigResult {
    bool success = false;
    bool module_state_present = false;  // file exists → true
    std::string module_state;
    bool smarthost_present = false;
    std::string smarthost;
    std::string error;
};
```

- Missing file → `present=false`
- Present empty file → `present=true, value=""`
- Present non-empty → `present=true, value=content`

## Shared parsing

`LegacyDatasetReader` is the single authoritative TXT parser for all 17 resource types. Used by both `LegacyImporter` and `Verification`.

## Manual invocation only

Importer and verification are never wired into daemon startup. Phase 10/11 not started.
