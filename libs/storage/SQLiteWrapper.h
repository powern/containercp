#ifndef CONTAINERCP_STORAGE_SQLITE_WRAPPER_H
#define CONTAINERCP_STORAGE_SQLITE_WRAPPER_H

#include <cstdint>
#include <string>

struct sqlite3;
struct sqlite3_stmt;

namespace containercp::storage {

// Minimal RAII wrapper around a single SQLite connection and statement.
//
// Owns one sqlite3* handle and one sqlite3_stmt* handle at a time.
// Non-copyable, move-only.  Closes both handles on destruction.
//
// This is a low-level building block for the Storage subsystem.
// Managers, API handlers, and providers must never use this class
// directly — they must go through the Storage API.
class SQLiteDB {
public:
    SQLiteDB() = default;
    ~SQLiteDB();

    SQLiteDB(const SQLiteDB&) = delete;
    SQLiteDB& operator=(const SQLiteDB&) = delete;

    SQLiteDB(SQLiteDB&& other) noexcept;
    SQLiteDB& operator=(SQLiteDB&& other) noexcept;

    // Open a database file.  Creates the file if it does not exist.
    // Applies the standard PRAGMAs on success.
    // Returns true on success, false on error (check error_message()).
    bool open(const std::string& path);

    // Close the database and finalize any prepared statement.
    // Idempotent — safe to call multiple times.
    bool close();

    // Returns true if open() succeeded and close() has not been called.
    bool is_open() const;

    // Execute a single SQL statement that returns no rows
    // (e.g. CREATE TABLE, PRAGMA, INSERT without RETURNING).
    // Returns true on success.
    bool exec(const std::string& sql);

    // Prepare a SQL statement for repeated execution with binding.
    // Finalizes any previously prepared statement.
    // Returns true on success.
    bool prepare(const std::string& sql);

    // Bind parameters on the currently prepared statement.
    // Index starts at 1 (SQLite convention).
    bool bind_int(int index, int64_t value);
    bool bind_text(int index, const std::string& value);
    bool bind_null(int index);

    // Advance the prepared statement to the next row.
    // Returns true if a row is available (SQLITE_ROW).
    // Returns false if execution is complete (SQLITE_DONE).
    // On error, returns false and sets error state.
    bool step();

    // Returns the number of columns in the current result row.
    int column_count();

    // Access column values from the current result row.
    int64_t column_int(int index);
    std::string column_text(int index);
    bool column_is_null(int index);

    // Transactions.  Use BEGIN IMMEDIATE to acquire a write lock
    // immediately rather than waiting until the first write.
    bool begin_immediate();
    bool commit();
    bool rollback();

    // Apply the standard set of PRAGMAs:
    //   journal_mode = WAL
    //   synchronous = FULL
    //   foreign_keys = ON
    //   busy_timeout = 5000
    //   wal_autocheckpoint = 1000
    //   journal_size_limit = 67108864
    bool apply_pragmas();

    // Last error information (cleared on each new call).
    std::string error_message() const;
    int error_code() const;

    // Direct handle access for MigrationEngine (only).
    // Returns nullptr if not open.
    sqlite3* handle() const;

private:
    void finalize_stmt();

    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
    int last_error_code_ = 0;
    std::string last_error_message_;
};

} // namespace containercp::storage

#endif // CONTAINERCP_STORAGE_SQLITE_WRAPPER_H
