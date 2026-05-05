#pragma once

#include <filesystem>
#include <string>

#include "inventory_engine.hpp"

struct sqlite3;

namespace fridge {

class SQLiteInventoryStore {
public:
    explicit SQLiteInventoryStore(std::filesystem::path database_path);
    ~SQLiteInventoryStore();

    SQLiteInventoryStore(const SQLiteInventoryStore&) = delete;
    SQLiteInventoryStore& operator=(const SQLiteInventoryStore&) = delete;

    bool open(std::string& error_message);
    void close();
    bool db_ready() const;
    const std::filesystem::path& database_path() const;

    bool initialize_schema(std::string& error_message);
    bool reset(std::string& error_message);
    bool save_snapshot(const InventoryEngine& engine, std::string& error_message);
    bool load_snapshot(InventoryEngine& engine, std::string& error_message) const;

private:
    std::filesystem::path database_path_;
    sqlite3* db_ = nullptr;
};

}  // namespace fridge
