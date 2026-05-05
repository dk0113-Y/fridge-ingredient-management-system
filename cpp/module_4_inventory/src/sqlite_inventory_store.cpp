#include "sqlite_inventory_store.hpp"

#include <sqlite3.h>

#include <filesystem>
#include <sstream>
#include <utility>

namespace fridge {

namespace {

class Statement {
public:
    Statement(sqlite3* db, const char* sql, std::string& error_message)
        : db_(db) {
        if (sqlite3_prepare_v2(db_, sql, -1, &statement_, nullptr) != SQLITE_OK) {
            error_message = sqlite3_errmsg(db_);
        }
    }

    ~Statement() {
        if (statement_ != nullptr) {
            sqlite3_finalize(statement_);
        }
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    bool ready() const {
        return statement_ != nullptr;
    }

    sqlite3_stmt* get() const {
        return statement_;
    }

    bool step_done(std::string& error_message) {
        const int rc = sqlite3_step(statement_);
        if (rc == SQLITE_DONE) {
            sqlite3_reset(statement_);
            sqlite3_clear_bindings(statement_);
            return true;
        }
        error_message = sqlite3_errmsg(db_);
        return false;
    }

private:
    sqlite3* db_ = nullptr;
    sqlite3_stmt* statement_ = nullptr;
};

bool exec_sql(sqlite3* db, const char* sql, std::string& error_message) {
    char* raw_error = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &raw_error);
    if (rc == SQLITE_OK) {
        return true;
    }

    if (raw_error != nullptr) {
        error_message = raw_error;
        sqlite3_free(raw_error);
    } else {
        error_message = sqlite3_errmsg(db);
    }
    return false;
}

bool begin_transaction(sqlite3* db, std::string& error_message) {
    return exec_sql(db, "BEGIN IMMEDIATE TRANSACTION;", error_message);
}

bool commit_transaction(sqlite3* db, std::string& error_message) {
    return exec_sql(db, "COMMIT;", error_message);
}

void rollback_transaction(sqlite3* db) {
    std::string ignored;
    exec_sql(db, "ROLLBACK;", ignored);
}

bool bind_text(sqlite3_stmt* statement, int index, const std::string& value) {
    return sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
}

bool bind_int(sqlite3_stmt* statement, int index, int value) {
    return sqlite3_bind_int(statement, index, value) == SQLITE_OK;
}

bool bind_double(sqlite3_stmt* statement, int index, double value) {
    return sqlite3_bind_double(statement, index, value) == SQLITE_OK;
}

std::string column_text(sqlite3_stmt* statement, int index) {
    const unsigned char* value = sqlite3_column_text(statement, index);
    return value == nullptr ? std::string{} : reinterpret_cast<const char*>(value);
}

EventType event_type_from_string(const std::string& value) {
    if (value == "capture_recorded") {
        return EventType::CaptureRecorded;
    }
    if (value == "not_evaluated") {
        return EventType::NotEvaluated;
    }
    if (value == "reorganize") {
        return EventType::Reorganize;
    }
    if (value == "put_in") {
        return EventType::PutIn;
    }
    if (value == "take_out") {
        return EventType::TakeOut;
    }
    if (value == "partial_take_out_candidate") {
        return EventType::PartialTakeOutCandidate;
    }
    if (value == "uncertain") {
        return EventType::Uncertain;
    }
    return EventType::NoChange;
}

bool save_inventory(sqlite3* db, const std::vector<InventoryItem>& records, std::string& error_message) {
    Statement statement(
        db,
        "INSERT INTO inventory "
        "(item_id, item_name, category, count, remain_level, expire_date, last_update_time) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);",
        error_message
    );
    if (!statement.ready()) {
        return false;
    }

    for (const auto& record : records) {
        if (!bind_int(statement.get(), 1, record.item_id) ||
            !bind_text(statement.get(), 2, record.item_name) ||
            !bind_text(statement.get(), 3, record.category) ||
            !bind_int(statement.get(), 4, record.count) ||
            !bind_double(statement.get(), 5, record.remain_level) ||
            !bind_text(statement.get(), 6, record.expire_date) ||
            !bind_text(statement.get(), 7, record.last_update_time)) {
            error_message = sqlite3_errmsg(db);
            return false;
        }
        if (!statement.step_done(error_message)) {
            return false;
        }
    }
    return true;
}

bool save_event_log(sqlite3* db, const std::vector<InventoryEventRecord>& records, std::string& error_message) {
    Statement statement(
        db,
        "INSERT INTO event_log "
        "(event_id, session_id, event_type, coarse_class, fine_name, quantity_delta, "
        "yolo_confidence, llm_confidence, review_required, review_reason, timestamp) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
        error_message
    );
    if (!statement.ready()) {
        return false;
    }

    for (const auto& record : records) {
        if (!bind_int(statement.get(), 1, record.event_id) ||
            !bind_text(statement.get(), 2, record.session_id) ||
            !bind_text(statement.get(), 3, to_string(record.event_type)) ||
            !bind_text(statement.get(), 4, record.coarse_class) ||
            !bind_text(statement.get(), 5, record.fine_name) ||
            !bind_int(statement.get(), 6, record.quantity_delta) ||
            !bind_double(statement.get(), 7, record.yolo_confidence) ||
            !bind_double(statement.get(), 8, record.llm_confidence) ||
            !bind_int(statement.get(), 9, record.review_required ? 1 : 0) ||
            !bind_text(statement.get(), 10, record.review_reason) ||
            !bind_text(statement.get(), 11, record.timestamp)) {
            error_message = sqlite3_errmsg(db);
            return false;
        }
        if (!statement.step_done(error_message)) {
            return false;
        }
    }
    return true;
}

bool save_pending_reviews(sqlite3* db, const std::vector<PendingReviewRecord>& records, std::string& error_message) {
    Statement statement(
        db,
        "INSERT INTO pending_review "
        "(pending_id, session_id, event_type, category, item_name, reason, quantity_delta, remain_level, timestamp) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);",
        error_message
    );
    if (!statement.ready()) {
        return false;
    }

    for (const auto& record : records) {
        if (!bind_int(statement.get(), 1, record.pending_id) ||
            !bind_text(statement.get(), 2, record.session_id) ||
            !bind_text(statement.get(), 3, to_string(record.event_type)) ||
            !bind_text(statement.get(), 4, record.category) ||
            !bind_text(statement.get(), 5, record.item_name) ||
            !bind_text(statement.get(), 6, record.reason) ||
            !bind_int(statement.get(), 7, record.count_delta) ||
            !bind_double(statement.get(), 8, record.remain_level) ||
            !bind_text(statement.get(), 9, record.timestamp)) {
            error_message = sqlite3_errmsg(db);
            return false;
        }
        if (!statement.step_done(error_message)) {
            return false;
        }
    }
    return true;
}

bool save_change_log(sqlite3* db, const std::vector<InventoryChangeRecord>& records, std::string& error_message) {
    Statement statement(
        db,
        "INSERT INTO inventory_change_log "
        "(change_id, session_id, item_name, category, quantity_delta, remain_level, timestamp, source) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?);",
        error_message
    );
    if (!statement.ready()) {
        return false;
    }

    for (const auto& record : records) {
        if (!bind_int(statement.get(), 1, record.change_id) ||
            !bind_text(statement.get(), 2, record.session_id) ||
            !bind_text(statement.get(), 3, record.item_name) ||
            !bind_text(statement.get(), 4, record.category) ||
            !bind_int(statement.get(), 5, record.count_delta) ||
            !bind_double(statement.get(), 6, record.remain_level) ||
            !bind_text(statement.get(), 7, record.timestamp) ||
            !bind_text(statement.get(), 8, record.source)) {
            error_message = sqlite3_errmsg(db);
            return false;
        }
        if (!statement.step_done(error_message)) {
            return false;
        }
    }
    return true;
}

bool load_inventory(sqlite3* db, InventorySnapshot& snapshot, std::string& error_message) {
    Statement statement(
        db,
        "SELECT item_id, item_name, category, count, remain_level, expire_date, last_update_time "
        "FROM inventory ORDER BY item_id;",
        error_message
    );
    if (!statement.ready()) {
        return false;
    }

    while (true) {
        const int rc = sqlite3_step(statement.get());
        if (rc == SQLITE_DONE) {
            return true;
        }
        if (rc != SQLITE_ROW) {
            error_message = sqlite3_errmsg(db);
            return false;
        }

        snapshot.inventory_items.push_back(InventoryItem{
            sqlite3_column_int(statement.get(), 0),
            column_text(statement.get(), 1),
            column_text(statement.get(), 2),
            sqlite3_column_int(statement.get(), 3),
            sqlite3_column_double(statement.get(), 4),
            column_text(statement.get(), 5),
            column_text(statement.get(), 6)
        });
    }
}

bool load_event_log(sqlite3* db, InventorySnapshot& snapshot, std::string& error_message) {
    Statement statement(
        db,
        "SELECT event_id, session_id, event_type, coarse_class, fine_name, quantity_delta, "
        "yolo_confidence, llm_confidence, review_required, review_reason, timestamp "
        "FROM event_log ORDER BY event_id;",
        error_message
    );
    if (!statement.ready()) {
        return false;
    }

    while (true) {
        const int rc = sqlite3_step(statement.get());
        if (rc == SQLITE_DONE) {
            return true;
        }
        if (rc != SQLITE_ROW) {
            error_message = sqlite3_errmsg(db);
            return false;
        }

        snapshot.event_log.push_back(InventoryEventRecord{
            sqlite3_column_int(statement.get(), 0),
            column_text(statement.get(), 1),
            event_type_from_string(column_text(statement.get(), 2)),
            column_text(statement.get(), 3),
            column_text(statement.get(), 4),
            sqlite3_column_int(statement.get(), 5),
            sqlite3_column_double(statement.get(), 6),
            sqlite3_column_double(statement.get(), 7),
            sqlite3_column_int(statement.get(), 8) != 0,
            column_text(statement.get(), 9),
            column_text(statement.get(), 10)
        });
    }
}

bool load_pending_reviews(sqlite3* db, InventorySnapshot& snapshot, std::string& error_message) {
    Statement statement(
        db,
        "SELECT pending_id, session_id, event_type, category, item_name, reason, quantity_delta, remain_level, timestamp "
        "FROM pending_review ORDER BY pending_id;",
        error_message
    );
    if (!statement.ready()) {
        return false;
    }

    while (true) {
        const int rc = sqlite3_step(statement.get());
        if (rc == SQLITE_DONE) {
            return true;
        }
        if (rc != SQLITE_ROW) {
            error_message = sqlite3_errmsg(db);
            return false;
        }

        snapshot.pending_reviews.push_back(PendingReviewRecord{
            sqlite3_column_int(statement.get(), 0),
            column_text(statement.get(), 1),
            event_type_from_string(column_text(statement.get(), 2)),
            column_text(statement.get(), 3),
            column_text(statement.get(), 4),
            column_text(statement.get(), 5),
            sqlite3_column_int(statement.get(), 6),
            sqlite3_column_double(statement.get(), 7),
            column_text(statement.get(), 8)
        });
    }
}

bool load_change_log(sqlite3* db, InventorySnapshot& snapshot, std::string& error_message) {
    Statement statement(
        db,
        "SELECT change_id, session_id, item_name, category, quantity_delta, remain_level, timestamp, source "
        "FROM inventory_change_log ORDER BY change_id;",
        error_message
    );
    if (!statement.ready()) {
        return false;
    }

    while (true) {
        const int rc = sqlite3_step(statement.get());
        if (rc == SQLITE_DONE) {
            return true;
        }
        if (rc != SQLITE_ROW) {
            error_message = sqlite3_errmsg(db);
            return false;
        }

        snapshot.inventory_change_log.push_back(InventoryChangeRecord{
            sqlite3_column_int(statement.get(), 0),
            column_text(statement.get(), 1),
            column_text(statement.get(), 2),
            column_text(statement.get(), 3),
            sqlite3_column_int(statement.get(), 4),
            sqlite3_column_double(statement.get(), 5),
            column_text(statement.get(), 6),
            column_text(statement.get(), 7)
        });
    }
}

}  // namespace

SQLiteInventoryStore::SQLiteInventoryStore(std::filesystem::path database_path)
    : database_path_(std::move(database_path)) {}

SQLiteInventoryStore::~SQLiteInventoryStore() {
    close();
}

bool SQLiteInventoryStore::open(std::string& error_message) {
    if (db_ != nullptr) {
        return true;
    }

    if (!database_path_.parent_path().empty()) {
        std::filesystem::create_directories(database_path_.parent_path());
    }

    const int rc = sqlite3_open(database_path_.string().c_str(), &db_);
    if (rc == SQLITE_OK) {
        return true;
    }

    error_message = db_ == nullptr ? "failed to allocate sqlite handle" : sqlite3_errmsg(db_);
    close();
    return false;
}

void SQLiteInventoryStore::close() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool SQLiteInventoryStore::db_ready() const {
    return db_ != nullptr;
}

const std::filesystem::path& SQLiteInventoryStore::database_path() const {
    return database_path_;
}

bool SQLiteInventoryStore::initialize_schema(std::string& error_message) {
    if (db_ == nullptr) {
        error_message = "sqlite database is not open";
        return false;
    }

    return exec_sql(
        db_,
        "PRAGMA foreign_keys = ON;"
        "CREATE TABLE IF NOT EXISTS inventory ("
        "  item_id INTEGER PRIMARY KEY,"
        "  item_name TEXT NOT NULL,"
        "  category TEXT NOT NULL,"
        "  count INTEGER NOT NULL,"
        "  remain_level REAL NOT NULL,"
        "  expire_date TEXT,"
        "  last_update_time TEXT"
        ");"
        "CREATE TABLE IF NOT EXISTS event_log ("
        "  event_id INTEGER PRIMARY KEY,"
        "  session_id TEXT NOT NULL,"
        "  event_type TEXT NOT NULL,"
        "  coarse_class TEXT NOT NULL,"
        "  fine_name TEXT NOT NULL,"
        "  quantity_delta INTEGER NOT NULL,"
        "  yolo_confidence REAL NOT NULL,"
        "  llm_confidence REAL NOT NULL,"
        "  review_required INTEGER NOT NULL,"
        "  review_reason TEXT,"
        "  timestamp TEXT"
        ");"
        "CREATE TABLE IF NOT EXISTS pending_review ("
        "  pending_id INTEGER PRIMARY KEY,"
        "  session_id TEXT NOT NULL,"
        "  event_type TEXT NOT NULL,"
        "  category TEXT NOT NULL,"
        "  item_name TEXT NOT NULL,"
        "  reason TEXT,"
        "  quantity_delta INTEGER NOT NULL,"
        "  remain_level REAL NOT NULL,"
        "  timestamp TEXT"
        ");"
        "CREATE TABLE IF NOT EXISTS inventory_change_log ("
        "  change_id INTEGER PRIMARY KEY,"
        "  session_id TEXT NOT NULL,"
        "  item_name TEXT NOT NULL,"
        "  category TEXT NOT NULL,"
        "  quantity_delta INTEGER NOT NULL,"
        "  remain_level REAL NOT NULL,"
        "  timestamp TEXT,"
        "  source TEXT NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_inventory_item ON inventory(category, item_name);"
        "CREATE INDEX IF NOT EXISTS idx_event_log_session ON event_log(session_id);"
        "CREATE INDEX IF NOT EXISTS idx_event_log_timestamp ON event_log(timestamp);"
        "CREATE INDEX IF NOT EXISTS idx_pending_review_session ON pending_review(session_id);"
        "CREATE INDEX IF NOT EXISTS idx_change_log_session ON inventory_change_log(session_id);",
        error_message
    );
}

bool SQLiteInventoryStore::reset(std::string& error_message) {
    if (db_ == nullptr) {
        error_message = "sqlite database is not open";
        return false;
    }

    return exec_sql(
        db_,
        "DELETE FROM inventory_change_log;"
        "DELETE FROM pending_review;"
        "DELETE FROM event_log;"
        "DELETE FROM inventory;",
        error_message
    );
}

bool SQLiteInventoryStore::save_snapshot(const InventoryEngine& engine, std::string& error_message) {
    if (db_ == nullptr) {
        error_message = "sqlite database is not open";
        return false;
    }

    const InventorySnapshot snapshot = engine.export_snapshot();
    if (!begin_transaction(db_, error_message)) {
        return false;
    }

    if (!reset(error_message) ||
        !save_inventory(db_, snapshot.inventory_items, error_message) ||
        !save_event_log(db_, snapshot.event_log, error_message) ||
        !save_pending_reviews(db_, snapshot.pending_reviews, error_message) ||
        !save_change_log(db_, snapshot.inventory_change_log, error_message)) {
        rollback_transaction(db_);
        return false;
    }

    if (!commit_transaction(db_, error_message)) {
        rollback_transaction(db_);
        return false;
    }
    return true;
}

bool SQLiteInventoryStore::load_snapshot(InventoryEngine& engine, std::string& error_message) const {
    if (db_ == nullptr) {
        error_message = "sqlite database is not open";
        return false;
    }

    InventorySnapshot snapshot;
    if (!load_inventory(db_, snapshot, error_message) ||
        !load_event_log(db_, snapshot, error_message) ||
        !load_pending_reviews(db_, snapshot, error_message) ||
        !load_change_log(db_, snapshot, error_message)) {
        return false;
    }

    engine.replace_state_for_persistence(snapshot);
    return true;
}

}  // namespace fridge
