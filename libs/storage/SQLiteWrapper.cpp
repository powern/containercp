#include "SQLiteWrapper.h"

#include <sqlite3.h>

#include <cstring>

namespace containercp::storage {

SQLiteDB::~SQLiteDB() {
    close();
}

SQLiteDB::SQLiteDB(SQLiteDB&& other) noexcept
    : db_(other.db_)
    , stmt_(other.stmt_)
    , last_error_code_(other.last_error_code_)
    , last_error_message_(std::move(other.last_error_message_))
{
    other.db_ = nullptr;
    other.stmt_ = nullptr;
    other.last_error_code_ = 0;
}

SQLiteDB& SQLiteDB::operator=(SQLiteDB&& other) noexcept {
    if (this != &other) {
        close();
        db_ = other.db_;
        stmt_ = other.stmt_;
        last_error_code_ = other.last_error_code_;
        last_error_message_ = std::move(other.last_error_message_);
        other.db_ = nullptr;
        other.stmt_ = nullptr;
        other.last_error_code_ = 0;
    }
    return *this;
}

bool SQLiteDB::open(const std::string& path) {
    if (db_) return true;  // already open, idempotent

    int rc = sqlite3_open(path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        last_error_code_ = rc;
        if (db_) {
            last_error_message_ = sqlite3_errmsg(db_);
            sqlite3_close(db_);
            db_ = nullptr;
        } else {
            last_error_message_ = sqlite3_errstr(rc);
        }
        return false;
    }

    if (!apply_pragmas()) {
        close();
        return false;
    }

    return true;
}

bool SQLiteDB::close() {
    finalize_stmt();
    if (db_) {
        int rc = sqlite3_close(db_);
        db_ = nullptr;
        if (rc != SQLITE_OK) {
            last_error_code_ = rc;
            last_error_message_ = sqlite3_errstr(rc);
            return false;
        }
    }
    return true;
}

bool SQLiteDB::is_open() const {
    return db_ != nullptr;
}

bool SQLiteDB::exec(const std::string& sql) {
    if (!db_) {
        last_error_code_ = SQLITE_MISUSE;
        last_error_message_ = "Database not open";
        return false;
    }

    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        last_error_code_ = rc;
        last_error_message_ = err_msg ? err_msg : sqlite3_errstr(rc);
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

bool SQLiteDB::prepare(const std::string& sql) {
    if (!db_) {
        last_error_code_ = SQLITE_MISUSE;
        last_error_message_ = "Database not open";
        return false;
    }

    finalize_stmt();

    int rc = sqlite3_prepare_v2(db_, sql.c_str(), static_cast<int>(sql.size()),
                                &stmt_, nullptr);
    if (rc != SQLITE_OK) {
        last_error_code_ = rc;
        last_error_message_ = sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

bool SQLiteDB::bind_int(int index, int64_t value) {
    if (!stmt_) {
        last_error_code_ = SQLITE_MISUSE;
        last_error_message_ = "No prepared statement";
        return false;
    }
    int rc = sqlite3_bind_int64(stmt_, index, value);
    if (rc != SQLITE_OK) {
        last_error_code_ = rc;
        last_error_message_ = sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

bool SQLiteDB::bind_text(int index, const std::string& value) {
    if (!stmt_) {
        last_error_code_ = SQLITE_MISUSE;
        last_error_message_ = "No prepared statement";
        return false;
    }
    // SQLITE_TRANSIENT makes SQLite copy the data
    int rc = sqlite3_bind_text(stmt_, index, value.c_str(),
                               static_cast<int>(value.size()),
                               SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        last_error_code_ = rc;
        last_error_message_ = sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

bool SQLiteDB::bind_null(int index) {
    if (!stmt_) {
        last_error_code_ = SQLITE_MISUSE;
        last_error_message_ = "No prepared statement";
        return false;
    }
    int rc = sqlite3_bind_null(stmt_, index);
    if (rc != SQLITE_OK) {
        last_error_code_ = rc;
        last_error_message_ = sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

bool SQLiteDB::step() {
    if (!stmt_) {
        last_error_code_ = SQLITE_MISUSE;
        last_error_message_ = "No prepared statement";
        return false;
    }
    int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;

    last_error_code_ = rc;
    last_error_message_ = sqlite3_errmsg(db_);
    return false;
}

int SQLiteDB::column_count() {
    if (!stmt_) return 0;
    return sqlite3_column_count(stmt_);
}

int64_t SQLiteDB::column_int(int index) {
    if (!stmt_) return 0;
    return sqlite3_column_int64(stmt_, index);
}

std::string SQLiteDB::column_text(int index) {
    if (!stmt_) return {};
    const unsigned char* text = sqlite3_column_text(stmt_, index);
    if (!text) return {};
    int bytes = sqlite3_column_bytes(stmt_, index);
    return std::string(reinterpret_cast<const char*>(text), bytes);
}

bool SQLiteDB::column_is_null(int index) {
    if (!stmt_) return true;
    return sqlite3_column_type(stmt_, index) == SQLITE_NULL;
}

bool SQLiteDB::begin_immediate() {
    return exec("BEGIN IMMEDIATE");
}

bool SQLiteDB::commit() {
    return exec("COMMIT");
}

bool SQLiteDB::rollback() {
    return exec("ROLLBACK");
}

bool SQLiteDB::apply_pragmas() {
    if (!db_) return false;

    const char* pragmas[] = {
        "PRAGMA journal_mode = WAL",
        "PRAGMA synchronous = FULL",
        "PRAGMA foreign_keys = ON",
        "PRAGMA busy_timeout = 5000",
        "PRAGMA wal_autocheckpoint = 1000",
        "PRAGMA journal_size_limit = 67108864",
        nullptr
    };

    for (int i = 0; pragmas[i] != nullptr; ++i) {
        char* err_msg = nullptr;
        int rc = sqlite3_exec(db_, pragmas[i], nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            last_error_code_ = rc;
            last_error_message_ = err_msg ? err_msg : sqlite3_errstr(rc);
            sqlite3_free(err_msg);
            return false;
        }
    }
    return true;
}

std::string SQLiteDB::error_message() const {
    return last_error_message_;
}

int SQLiteDB::error_code() const {
    return last_error_code_;
}

sqlite3* SQLiteDB::handle() const {
    return db_;
}

void SQLiteDB::finalize_stmt() {
    if (stmt_) {
        sqlite3_finalize(stmt_);
        stmt_ = nullptr;
    }
}

} // namespace containercp::storage
