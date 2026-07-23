#ifndef CONTAINERCP_STORAGE_SCHEMA_MIGRATIONS_H
#define CONTAINERCP_STORAGE_SCHEMA_MIGRATIONS_H

#include "MigrationEngine.h"

namespace containercp::storage {

// Register all approved business schema migrations with the engine.
// Call this during storage initialization to create the SQLite schema.
//
// Current migration versions:
//   v1 — Initial business tables (18 tables)
//   v2 — SSH public keys (access_keys)
//   v3 — System account mappings (system_accounts)
void register_all_schema_migrations(MigrationEngine& engine);

} // namespace containercp::storage

#endif // CONTAINERCP_STORAGE_SCHEMA_MIGRATIONS_H
