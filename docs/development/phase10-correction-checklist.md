# Phase 10 Corrective Work Checklist

## Scope and boundaries

Corrective work for Phase 10 (immutable legacy TXT archive) implementation.
Phase 11 explicitly excluded. No daemon startup integration. No source TXT
deletion. SQLite remains explicit-only. Manual archive invocation only.

## Baseline

- Reviewed commit: 5ee7890839216610b740c7623c842d0b5de32de9
- Current known test count: 524+ (approximate)
- Current build state: clean
- Phase 11: NOT STARTED

## Correction items

### P10-C01 — Fix semantic version validation
- [ ] fix
- Problem: `safe_version()` rejects period characters, invalidating real versions like `v0.6.0`
- Files: `libs/storage/LegacyArchive.h`, `libs/storage/LegacyArchive.cpp`
- Implementation: allow individual semantic-version periods; reject "..", slashes, backslashes, whitespace, control chars
- Criteria: `v0.6.0`, `v0.7.0` accepted; `../v0.6.0`, `v0..6`, `v0/6/0` rejected
- Tests: safe_version() acceptance + rejection test suite
- Commit SHA: ___________

### P10-C02 — Strict canonical UUID contract
- [ ] fix
- Problem: `valid_migration_id()` accepts uppercase, any UUID version; `generate_uuid()` doesn't set v4 bits
- Files: `libs/storage/LegacyArchive.h`, `libs/storage/LegacyArchive.cpp`
- Implementation: enforce lowercase UUID v4 (version nibble=4, variant y=8/9/a/b)
- Criteria: generated UUID validates; canonical accepted; uppercase/wrong version rejected
- Tests: UUID validation + generation test suite
- Commit SHA: ___________

### P10-C03 — Make physical inventory truly shared
- [ ] refactor
- Problem: `legacy_file_inventory()` lives in LegacyArchive.h only
- Files: new `libs/storage/LegacyFileInventory.h`
- Implementation: extract to neutral component; use from archive, importer, tests
- Criteria: one authoritative list; archive has no private duplicate
- Tests: inventory uniqueness and exact filenames test
- Commit SHA: ___________

### P10-C04 — Preserve parse success separately from count
- [ ] fix
- Problem: `count_records()` returns uint64_t only, conflating valid-empty and parse failure
- Files: `libs/storage/LegacyArchive.h`, `libs/storage/LegacyArchive.cpp`
- Implementation: `LegacyRecordCountResult` with success/error; remove line-count heuristic
- Criteria: valid empty succeeds; malformed fails; no raw line-count fallback
- Tests: empty valid, malformed required, malformed optional
- Commit SHA: ___________

### P10-C05 — Separate profiles physical record counts
- [ ] fix
- Problem: both profiles.db and template_profiles.db get combined count from `read_combined_profiles()`
- Files: `libs/storage/LegacyArchive.cpp`
- Implementation: use per-file reader for physical counts; combined logical count optional
- Criteria: profiles.db=2, template_profiles.db=1 reported separately
- Tests: fixture with separate profile counts
- Commit SHA: ___________

### P10-C06 — Define mail config physical count semantics
- [ ] fix
- Problem: mail_state.db and mail_smarthost.db counts undocumented
- Files: `libs/storage/LegacyArchive.cpp`, `docs/development/legacy-archive-api.md`
- Implementation: define count=1 when non-empty, count=0 when empty present, count=0 when absent
- Criteria: both missing, state present-empty, smarthost present-empty, both present with values
- Tests: mail_config count test suite
- Commit SHA: ___________

### P10-C07 — Create and validate archive root safely
- [ ] fix
- Problem: `directory_iterator(archive_root_)` may run before root exists and is safe
- Files: `libs/storage/LegacyArchive.cpp`
- Implementation: ensure root exists/is directory/not symlink; reject unsafe; handle errors
- Criteria: missing root, symlink root, unreadable root, source==root detection
- Tests: root safety test suite
- Commit SHA: ___________

### P10-C08 — Implement verified idempotency
- [ ] fix
- Problem: substring search in manifest text for migration_id
- Files: `libs/storage/LegacyArchive.cpp`
- Implementation: parse manifest, compare exact migration_id, verify archive, apply documented rule
- Criteria: valid same-ID detected; corrupted same-ID rejected; different-ID skipped
- Tests: idempotency test suite
- Commit SHA: ___________

### P10-C09 — Complete Phase 9 prerequisite
- [ ] fix
- Problem: checks are present but missing reopened inventory validation
- Files: `libs/storage/LegacyArchive.cpp`
- Implementation: add exact 17-resource reopened inventory validation
- Criteria: all conditions enforced; missing/duplicate resource rejected
- Tests: Phase 9 prerequisite test suite
- Commit SHA: ___________

### P10-C10 — Capture a real source snapshot
- [ ] fix
- Problem: no device/inode capture; mtime not captured
- Files: `libs/storage/LegacyArchive.cpp`
- Implementation: capture mtime, device, inode; compare after copy; reject inode change
- Criteria: same-size replacement detected; source replaced by another inode detected
- Tests: deterministic mutation test using injectable seam
- Commit SHA: ___________

### P10-C11 — Implement exclusive, streamed durable copy
- [ ] fix
- Problem: whole-file memory load; no exclusive destination create; no O_EXCL
- Files: `libs/storage/LegacyArchive.cpp`
- Implementation: fixed-size buffer; O_CREAT | O_EXCL; check every read/write; fsync
- Criteria: existing dest rejected; partial write handled; large file copied
- Tests: durable copy test suite
- Commit SHA: ___________

### P10-C12 — Check every fsync, open and rename
- [ ] fix
- Problem: fsync return values not checked; fopen failures silently ignored
- Files: `libs/storage/LegacyArchive.cpp`
- Implementation: check every fsync/fopen/rename result; produce safe error category
- Criteria: every failure produces explicit error
- Tests: deterministic fsync/open/rename failure tests
- Commit SHA: ___________

### P10-C13 — Use a real manifest JSON parser
- [ ] fix
- Problem: `verify_archive()` uses substring searches
- Files: `libs/storage/LegacyArchive.cpp`
- Implementation: strict JSON parser; reconstruct complete ArchiveManifest
- Criteria: rejects invalid JSON/duplicate keys/missing fields/wrong types
- Tests: JSON parser test suite
- Commit SHA: ___________

### P10-C14 — Validate manifest semantics
- [ ] fix
- Problem: no field validation after parsing
- Files: `libs/storage/LegacyArchive.cpp`
- Implementation: validate all manifest fields (version, counts, versions, presence, etc.)
- Criteria: unsupported version rejected; wrong counts rejected; absent entry has zero fields
- Tests: manifest semantics test suite
- Commit SHA: ___________

### P10-C15 — Cross-check manifest, SHA256SUMS and disk
- [ ] fix
- Problem: no cross-check between manifest and SHA256SUMS; disk not verified against manifest
- Files: `libs/storage/LegacyArchive.cpp`
- Implementation: verify all three sources agree; reject divergence
- Criteria: manifest SHA changed detected; checksum entry removed detected; disk file modified detected
- Tests: cross-check tampering test suite
- Commit SHA: ___________

### P10-C16 — Strict SHA256SUMS parser
- [ ] fix
- Problem: parser doesn't validate hash case/length/separator format
- Files: `libs/storage/LegacyArchive.cpp`
- Implementation: require exact 64 lowercase hex + two spaces + filename + newline
- Criteria: uppercase/reject; short/long reject; tab reject; one-space reject; empty reject
- Tests: SHA256SUMS parser test suite
- Commit SHA: ___________

### P10-C17 — Verify exact archive directory contents
- [ ] fix
- Problem: only unknown .db files rejected; sockets/FIFO/devices not rejected
- Files: `libs/storage/LegacyArchive.cpp`
- Implementation: reject all non-recognized entries (sockets, FIFOs, devices, temp files, etc.)
- Criteria: socket rejected; nested dir rejected; hidden file rejected
- Tests: directory content verification test suite
- Commit SHA: ___________

### P10-C18 — Verify exact permissions and ownership
- [ ] fix
- Problem: `set_permissions()` doesn't read permissions back to verify
- Files: `libs/storage/LegacyArchive.cpp`
- Implementation: read back modes; verify 0700 (dir), 0440 (files); document ownership
- Criteria: mode mismatch detected; chmod failure handled
- Tests: permissions verification test suite
- Commit SHA: ___________

### P10-C19 — Define post-publication failure policy
- [ ] fix
- Problem: failed final archive left in place with final name
- Files: `libs/storage/LegacyArchive.cpp`, `docs/development/legacy-archive-api.md`
- Implementation: rename to quarantine name on failure; document policy
- Criteria: quarantine rename succeeds; retry not blocked; source unchanged
- Tests: post-publication failure + quarantine test
- Commit SHA: ___________

### P10-C20 — Make temp ownership per invocation
- [ ] fix
- Problem: `temp_owned_` and `temp_path_` are mutable members, risky on reuse
- Files: `libs/storage/LegacyArchive.cpp`
- Implementation: RAII cleanup per invocation; reset after success/failure
- Criteria: same instance reuse after success and failure; existing temp preserved
- Tests: instance reuse test suite
- Commit SHA: ___________

### P10-C21 — Handle filesystem exceptions fail-closed
- [ ] fix
- Problem: non-error_code filesystem calls may throw
- Files: `libs/storage/LegacyArchive.cpp`
- Implementation: use error_code overloads or narrow try/catch; safe error categories
- Criteria: no uncaught exception; every expected error mapped to safe category
- Tests: inaccessible paths and invalid types test
- Commit SHA: ___________

### P10-C22 — Disk space arithmetic and overhead
- [ ] fix
- Problem: uint64 overflow not detected; fixed 64 KiB overhead
- Files: `libs/storage/LegacyArchive.cpp`
- Implementation: checked overflow; conservative margin; document policy
- Criteria: overflow detected; exact boundary; insufficient space; fs::space error fatal
- Tests: disk space test suite
- Commit SHA: ___________

### P10-C23 — Test archive naming and single timestamp
- [ ] test
- Problem: no tests for naming/timestamp behavior
- Files: `tests/test_migration.cpp`
- Implementation: verify archive name contains version/migration_id; one timestamp
- Criteria: naming matches contract; timestamp format documented
- Tests: naming + timestamp test
- Commit SHA: ___________

### P10-C24 — Complete security and secret-safety tests
- [ ] test
- Problem: no tests prove manifest/SHA256SUMS/errors don't contain secrets
- Files: `tests/test_migration.cpp`
- Implementation: verify secret-bearing fields not present in manifest, SHA256SUMS, diagnostics
- Criteria: passwords, hashes, smarthost, DKIM paths absent
- Tests: redaction test suite
- Commit SHA: ___________

### P10-C25 — Update permanent documentation
- [ ] docs
- Problem: contracts changed but no Markdown updated
- Files: `docs/development/legacy-archive-api.md`, `docs/development/legacy-importer-api.md`, `docs/development/migration-verification-api.md`
- Implementation: document all new contracts
- Criteria: every new/modified API and contract documented
- Tests: N/A (documentation)
- Commit SHA: ___________

### P10-C26 — Expand focused test coverage
- [ ] test
- Problem: traceability from correction IDs to tests missing
- Files: this checklist
- Implementation: populate traceability table below
- Criteria: every P10-C01 through P10-C25 has test or documented reason
- Tests: see traceability table
- Commit SHA: ___________

## Traceability table

| Correction ID | Test name(s) | Result | Commit |
|---------------|--------------|--------|--------|
| P10-C01 | ___________ | ___________ | ___________ |
| P10-C02 | ___________ | ___________ | ___________ |
... (to be completed during implementation)

## Final validation section

- [ ] Every P10-C01 through P10-C26 marked complete
- [ ] Every completed item has commit SHA
- [ ] Every item has test evidence
- [ ] Documentation updated
- [ ] Clean containercpd build
- [ ] Clean containercp_tests build
- [ ] Focused Phase 10 tests pass
- [ ] Complete project tests pass
- [ ] Fixture pipelines pass
- [ ] Source TXT files unchanged
- [ ] No source TXT deleted
- [ ] SQLite explicit-only
- [ ] Archive manual-only
- [ ] Phase 11 not started

Date: ___________
Build type: ___________
Focused test result: ___________
Full test result: ___________
Remaining limitations: ___________
Phase 10 readiness: ___________
