#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <hvc/persistence/sqlite_session_repository.hpp>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#ifdef _WIN32
#include <winsqlite/winsqlite3.h>
#else
#include <sqlite3.h>
#endif

namespace hvc::persistence
{
namespace
{
struct DatabaseCloser final
{
    void operator()(sqlite3* database) const noexcept
    {
        if (database != nullptr)
        {
            static_cast<void>(sqlite3_close(database));
        }
    }
};

using Database = std::unique_ptr<sqlite3, DatabaseCloser>;

struct StatementFinalizer final
{
    void operator()(sqlite3_stmt* statement) const noexcept
    {
        if (statement != nullptr)
        {
            static_cast<void>(sqlite3_finalize(statement));
        }
    }
};

using Statement = std::unique_ptr<sqlite3_stmt, StatementFinalizer>;

[[noreturn]] void throwDatabaseError(sqlite3* database, std::string_view operation)
{
    std::ostringstream message;
    message << operation << ": " << sqlite3_errmsg(database);
    throw PersistenceError{message.str()};
}

void execute(sqlite3* database, const char* sql)
{
    char* error_message = nullptr;
    const auto result = sqlite3_exec(database, sql, nullptr, nullptr, &error_message);
    if (result == SQLITE_OK)
    {
        return;
    }

    std::string message{"SQLite statement failed"};
    if (error_message != nullptr)
    {
        message.append(": ").append(error_message);
        sqlite3_free(error_message);
    }
    throw PersistenceError{message};
}

auto prepare(sqlite3* database, const char* sql) -> Statement
{
    sqlite3_stmt* raw_statement = nullptr;
    if (sqlite3_prepare_v2(database, sql, -1, &raw_statement, nullptr) != SQLITE_OK)
    {
        throwDatabaseError(database, "Could not prepare SQLite statement");
    }
    return Statement{raw_statement};
}

void bindText(sqlite3* database, sqlite3_stmt* statement, int index, std::string_view value)
{
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        throw PersistenceError{"A persisted identifier exceeds SQLite's text length limit."};
    }
    if (sqlite3_bind_text(statement, index, value.data(), static_cast<int>(value.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK)
    {
        throwDatabaseError(database, "Could not bind SQLite text value");
    }
}

auto readText(sqlite3_stmt* statement, int column) -> std::string
{
    const auto* value = sqlite3_column_text(statement, column);
    const auto size = sqlite3_column_bytes(statement, column);
    if (value == nullptr || size <= 0)
    {
        throw PersistenceError{"The session database contains an invalid empty identifier."};
    }
    return {reinterpret_cast<const char*>(value), static_cast<std::size_t>(size)};
}

auto toUnixMilliseconds(application::TimePoint time_point) -> std::int64_t
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(time_point.time_since_epoch())
        .count();
}

auto fromUnixMilliseconds(std::int64_t milliseconds) -> application::TimePoint
{
    return application::TimePoint{std::chrono::milliseconds{milliseconds}};
}

struct Migration final
{
    std::uint32_t version;
    const char* name;
    const char* sql;
};

constexpr std::array migrations{
    Migration{
        1,
        "create_sessions",
        R"sql(
CREATE TABLE schema_migrations (
    version INTEGER PRIMARY KEY NOT NULL,
    name TEXT NOT NULL,
    applied_at_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE sessions (
    session_id TEXT PRIMARY KEY NOT NULL CHECK(length(session_id) > 0),
    player_id TEXT NOT NULL CHECK(length(player_id) > 0),
    device_id TEXT NOT NULL CHECK(length(device_id) > 0),
    expires_at_unix_ms INTEGER NOT NULL
) WITHOUT ROWID;
CREATE INDEX sessions_by_player ON sessions(player_id);
CREATE INDEX sessions_by_expiration ON sessions(expires_at_unix_ms);
)sql",
    },
};

auto readSchemaVersion(sqlite3* database) -> std::uint32_t
{
    auto statement = prepare(database, "PRAGMA user_version;");
    if (sqlite3_step(statement.get()) != SQLITE_ROW)
    {
        throwDatabaseError(database, "Could not read the SQLite schema version");
    }

    const auto version = sqlite3_column_int64(statement.get(), 0);
    if (version < 0 ||
        version > static_cast<sqlite3_int64>(std::numeric_limits<std::uint32_t>::max()))
    {
        throw PersistenceError{"The SQLite schema version is outside the supported range."};
    }
    return static_cast<std::uint32_t>(version);
}

void migrate(sqlite3* database)
{
    auto current_version = readSchemaVersion(database);
    if (current_version > SqliteSessionRepository::latest_schema_version)
    {
        std::ostringstream message;
        message << "Database schema version " << current_version
                << " is newer than the supported version "
                << SqliteSessionRepository::latest_schema_version << '.';
        throw PersistenceError{message.str()};
    }

    for (const auto& migration : migrations)
    {
        if (migration.version <= current_version)
        {
            continue;
        }
        if (migration.version != current_version + 1)
        {
            throw PersistenceError{"SQLite schema migrations are not contiguous."};
        }

        execute(database, "BEGIN IMMEDIATE;");
        try
        {
            execute(database, migration.sql);
            auto ledger =
                prepare(database, "INSERT INTO schema_migrations(version, name) VALUES(?1, ?2);");
            if (sqlite3_bind_int64(ledger.get(), 1,
                                   static_cast<sqlite3_int64>(migration.version)) != SQLITE_OK)
            {
                throwDatabaseError(database, "Could not bind the migration version");
            }
            bindText(database, ledger.get(), 2, migration.name);
            if (sqlite3_step(ledger.get()) != SQLITE_DONE)
            {
                throwDatabaseError(database, "Could not record the schema migration");
            }

            const auto version_sql =
                "PRAGMA user_version = " + std::to_string(migration.version) + ';';
            execute(database, version_sql.c_str());
            execute(database, "COMMIT;");
            current_version = migration.version;
        }
        catch (...)
        {
            static_cast<void>(sqlite3_exec(database, "ROLLBACK;", nullptr, nullptr, nullptr));
            throw;
        }
    }
}

auto openDatabase(const std::filesystem::path& path) -> Database
{
    if (path.empty())
    {
        throw PersistenceError{"The SQLite database path must not be empty."};
    }

    const auto parent = path.parent_path();
    if (!parent.empty())
    {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error)
        {
            throw PersistenceError{"Could not create the SQLite database directory: " +
                                   error.message()};
        }
    }

    sqlite3* raw_database = nullptr;
#ifdef _WIN32
    const auto result = sqlite3_open16(path.native().c_str(), &raw_database);
#else
    const auto result = sqlite3_open(path.string().c_str(), &raw_database);
#endif
    Database database{raw_database};
    if (result != SQLITE_OK)
    {
        if (database == nullptr)
        {
            throw PersistenceError{"Could not allocate a SQLite database connection."};
        }
        throwDatabaseError(database.get(), "Could not open the SQLite database");
    }
    return database;
}
} // namespace

class SqliteSessionRepository::Implementation final
{
  public:
    explicit Implementation(const std::filesystem::path& database_path)
        : database(openDatabase(database_path))
    {
        execute(database.get(), "PRAGMA foreign_keys = ON;");
        execute(database.get(), "PRAGMA busy_timeout = 5000;");
        migrate(database.get());
    }

    mutable std::mutex mutex;
    Database database;
};

SqliteSessionRepository::SqliteSessionRepository(std::filesystem::path database_path)
    : implementation_(std::make_unique<Implementation>(database_path))
{
}

SqliteSessionRepository::~SqliteSessionRepository() = default;

auto SqliteSessionRepository::find(const domain::SessionId& session_id) const
    -> std::optional<application::AuthenticatedSession>
{
    std::scoped_lock lock{implementation_->mutex};
    auto statement = prepare(implementation_->database.get(),
                             "SELECT session_id, player_id, device_id, expires_at_unix_ms "
                             "FROM sessions WHERE session_id = ?1;");
    bindText(implementation_->database.get(), statement.get(), 1, session_id.value());

    const auto result = sqlite3_step(statement.get());
    if (result == SQLITE_DONE)
    {
        return std::nullopt;
    }
    if (result != SQLITE_ROW)
    {
        throwDatabaseError(implementation_->database.get(), "Could not load the session");
    }

    return application::AuthenticatedSession{
        domain::SessionId{readText(statement.get(), 0)},
        domain::PlayerId{readText(statement.get(), 1)},
        domain::DeviceId{readText(statement.get(), 2)},
        fromUnixMilliseconds(sqlite3_column_int64(statement.get(), 3))};
}

void SqliteSessionRepository::upsert(application::AuthenticatedSession session)
{
    std::scoped_lock lock{implementation_->mutex};
    auto statement =
        prepare(implementation_->database.get(),
                "INSERT INTO sessions(session_id, player_id, device_id, expires_at_unix_ms) "
                "VALUES(?1, ?2, ?3, ?4) "
                "ON CONFLICT(session_id) DO UPDATE SET "
                "player_id = excluded.player_id, device_id = excluded.device_id, "
                "expires_at_unix_ms = excluded.expires_at_unix_ms;");

    bindText(implementation_->database.get(), statement.get(), 1, session.session_id.value());
    bindText(implementation_->database.get(), statement.get(), 2, session.player_id.value());
    bindText(implementation_->database.get(), statement.get(), 3, session.device_id.value());
    if (sqlite3_bind_int64(statement.get(), 4, toUnixMilliseconds(session.expires_at)) != SQLITE_OK)
    {
        throwDatabaseError(implementation_->database.get(), "Could not bind session expiration");
    }
    if (sqlite3_step(statement.get()) != SQLITE_DONE)
    {
        throwDatabaseError(implementation_->database.get(), "Could not persist the session");
    }
}

auto SqliteSessionRepository::erase(const domain::SessionId& session_id) -> bool
{
    std::scoped_lock lock{implementation_->mutex};
    auto statement =
        prepare(implementation_->database.get(), "DELETE FROM sessions WHERE session_id = ?1;");
    bindText(implementation_->database.get(), statement.get(), 1, session_id.value());
    if (sqlite3_step(statement.get()) != SQLITE_DONE)
    {
        throwDatabaseError(implementation_->database.get(), "Could not erase the session");
    }
    return sqlite3_changes(implementation_->database.get()) != 0;
}

auto SqliteSessionRepository::schemaVersion() const -> std::uint32_t
{
    std::scoped_lock lock{implementation_->mutex};
    return readSchemaVersion(implementation_->database.get());
}
} // namespace hvc::persistence
