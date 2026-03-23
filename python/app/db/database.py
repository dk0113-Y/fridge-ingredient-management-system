import sqlite3

from ..category_mapping import normalize_category


def get_connection(db_path):
    connection = sqlite3.connect(db_path)
    connection.row_factory = sqlite3.Row
    connection.execute("PRAGMA foreign_keys = ON")
    return connection


def initialize_database(db_path):
    with get_connection(db_path) as connection:
        _ensure_inventory_items_schema(connection)
        connection.executescript(
            """
            CREATE TABLE IF NOT EXISTS events (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL UNIQUE,
                timestamp TEXT NOT NULL,
                event_type TEXT NOT NULL,
                roi_id TEXT NOT NULL,
                confidence REAL NOT NULL,
                before_frame TEXT NOT NULL,
                after_frame TEXT NOT NULL,
                need_user_confirm INTEGER NOT NULL DEFAULT 0,
                raw_json TEXT NOT NULL,
                source_file TEXT NOT NULL,
                created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
            );

            CREATE TABLE IF NOT EXISTS pending_confirmations (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                event_id INTEGER NOT NULL,
                session_id TEXT NOT NULL UNIQUE,
                status TEXT NOT NULL DEFAULT 'pending',
                item_name TEXT NOT NULL DEFAULT 'unknown',
                category TEXT NOT NULL DEFAULT 'other',
                remain_level REAL,
                note TEXT NOT NULL DEFAULT '',
                created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
                resolved_at TEXT,
                FOREIGN KEY(event_id) REFERENCES events(id)
            );
            """
        )
        _synchronize_categories(connection)
        connection.commit()


def _ensure_inventory_items_schema(connection):
    table_exists = connection.execute(
        """
        SELECT name
        FROM sqlite_master
        WHERE type = 'table' AND name = 'inventory_items'
        """
    ).fetchone()

    if table_exists is None:
        _create_inventory_items_table(connection)
        return

    if _has_inventory_unique_key(connection, ("name", "category", "remain_level")):
        return

    connection.execute("ALTER TABLE inventory_items RENAME TO inventory_items_legacy")
    _create_inventory_items_table(connection)
    connection.execute(
        """
        INSERT INTO inventory_items (id, name, category, count, remain_level, updated_at)
        SELECT id, name, category, count, remain_level, updated_at
        FROM inventory_items_legacy
        ORDER BY id
        """
    )
    connection.execute("DROP TABLE inventory_items_legacy")


def _create_inventory_items_table(connection):
    connection.execute(
        """
        CREATE TABLE IF NOT EXISTS inventory_items (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            category TEXT NOT NULL DEFAULT 'other',
            count INTEGER NOT NULL DEFAULT 0,
            remain_level REAL NOT NULL DEFAULT 0.0,
            updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
            UNIQUE(name, category, remain_level)
        )
        """
    )


def _has_inventory_unique_key(connection, expected_columns):
    indices = connection.execute("PRAGMA index_list('inventory_items')").fetchall()

    for index in indices:
        if not index["unique"]:
            continue

        columns = connection.execute(f"PRAGMA index_info('{index['name']}')").fetchall()
        column_names = tuple(column["name"] for column in columns)
        if column_names == expected_columns:
            return True

    return False


def _synchronize_categories(connection):
    _normalize_inventory_categories(connection)
    _normalize_pending_categories(connection)


def _normalize_inventory_categories(connection):
    table_exists = connection.execute(
        """
        SELECT name
        FROM sqlite_master
        WHERE type = 'table' AND name = 'inventory_items'
        """
    ).fetchone()

    if table_exists is None:
        return

    rows = connection.execute(
        """
        SELECT name, category, count, remain_level, updated_at
        FROM inventory_items
        ORDER BY id
        """
    ).fetchall()

    merged_rows = {}
    for row in rows:
        normalized_category = normalize_category(row["category"])
        remain_level = _normalize_remain_level(row["remain_level"])
        key = (row["name"], normalized_category, remain_level)

        existing = merged_rows.get(key)
        if existing is None:
            merged_rows[key] = {
                "name": row["name"],
                "category": normalized_category,
                "count": row["count"],
                "remain_level": remain_level,
                "updated_at": row["updated_at"],
            }
            continue

        existing["count"] += row["count"]
        existing["updated_at"] = max(existing["updated_at"], row["updated_at"])

    connection.execute("DELETE FROM inventory_items")
    for item in sorted(merged_rows.values(), key=lambda current: (current["updated_at"], current["name"])):
        connection.execute(
            """
            INSERT INTO inventory_items (name, category, count, remain_level, updated_at)
            VALUES (?, ?, ?, ?, ?)
            """,
            (
                item["name"],
                item["category"],
                item["count"],
                item["remain_level"],
                item["updated_at"],
            ),
        )


def _normalize_pending_categories(connection):
    table_exists = connection.execute(
        """
        SELECT name
        FROM sqlite_master
        WHERE type = 'table' AND name = 'pending_confirmations'
        """
    ).fetchone()

    if table_exists is None:
        return

    rows = connection.execute(
        """
        SELECT id, category
        FROM pending_confirmations
        """
    ).fetchall()

    for row in rows:
        connection.execute(
            """
            UPDATE pending_confirmations
            SET category = ?
            WHERE id = ?
            """,
            (normalize_category(row["category"]), row["id"]),
        )


def _normalize_remain_level(remain_level):
    return round(float(remain_level), 4)
