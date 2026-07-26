#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <hvc/persistence/sqlite_control_plane_repository.hpp>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

auto readString(sqlite3_stmt* statement, int column) -> std::string
{
    const auto* value = sqlite3_column_text(statement, column);
    const auto size = sqlite3_column_bytes(statement, column);
    if (value == nullptr || size < 0)
    {
        throw PersistenceError{"The database contains an invalid text value."};
    }
    return {reinterpret_cast<const char*>(value), static_cast<std::size_t>(size)};
}

void bindInteger(sqlite3* database, sqlite3_stmt* statement, int index, std::int64_t value)
{
    if (sqlite3_bind_int64(statement, index, value) != SQLITE_OK)
    {
        throwDatabaseError(database, "Could not bind SQLite integer value");
    }
}

void bindNull(sqlite3* database, sqlite3_stmt* statement, int index)
{
    if (sqlite3_bind_null(statement, index) != SQLITE_OK)
    {
        throwDatabaseError(database, "Could not bind SQLite null value");
    }
}

template <typename Identifier>
void bindOptionalIdentifier(sqlite3* database, sqlite3_stmt* statement, int index,
                            const std::optional<Identifier>& identifier)
{
    if (identifier)
    {
        bindText(database, statement, index, identifier->value());
    }
    else
    {
        bindNull(database, statement, index);
    }
}

template <typename Identifier>
auto readOptionalIdentifier(sqlite3_stmt* statement, int column) -> std::optional<Identifier>
{
    if (sqlite3_column_type(statement, column) == SQLITE_NULL)
    {
        return std::nullopt;
    }
    return Identifier{readText(statement, column)};
}

auto readBoolean(sqlite3_stmt* statement, int column) -> bool
{
    const auto value = sqlite3_column_int64(statement, column);
    if (value != 0 && value != 1)
    {
        throw PersistenceError{"The membership database contains an invalid boolean value."};
    }
    return value != 0;
}

auto scopeToInteger(domain::VoiceScope scope) noexcept -> std::int64_t
{
    return static_cast<std::int64_t>(scope);
}

auto scopeFromInteger(sqlite3_int64 value) -> domain::VoiceScope
{
    if (value < static_cast<sqlite3_int64>(domain::VoiceScope::team) ||
        value > static_cast<sqlite3_int64>(domain::VoiceScope::group))
    {
        throw PersistenceError{"The membership database contains an invalid voice scope."};
    }
    return static_cast<domain::VoiceScope>(value);
}

auto auditEventTypeFromInteger(sqlite3_int64 value) -> application::TransmissionAuditEventType
{
    if (value < static_cast<sqlite3_int64>(application::TransmissionAuditEventType::started) ||
        value > static_cast<sqlite3_int64>(
                    application::TransmissionAuditEventType::forcibly_interrupted))
    {
        throw PersistenceError{"The audit database contains an invalid event type."};
    }
    return static_cast<application::TransmissionAuditEventType>(value);
}

auto auditOperationFromInteger(sqlite3_int64 value) -> application::TransmissionAuditOperation
{
    if (value < static_cast<sqlite3_int64>(application::TransmissionAuditOperation::start) ||
        value >
            static_cast<sqlite3_int64>(application::TransmissionAuditOperation::membership_change))
    {
        throw PersistenceError{"The audit database contains an invalid operation."};
    }
    return static_cast<application::TransmissionAuditOperation>(value);
}

auto stopReasonFromInteger(sqlite3_int64 value) -> domain::TransmissionStopReason
{
    if (value < static_cast<sqlite3_int64>(domain::TransmissionStopReason::none) ||
        value > static_cast<sqlite3_int64>(domain::TransmissionStopReason::disconnected))
    {
        throw PersistenceError{"The audit database contains an invalid stop reason."};
    }
    return static_cast<domain::TransmissionStopReason>(value);
}

auto auditRejectionFromInteger(sqlite3_int64 value) -> application::TransmissionAuditRejectionReason
{
    if (value < static_cast<sqlite3_int64>(
                    application::TransmissionAuditRejectionReason::session_not_found) ||
        value > static_cast<sqlite3_int64>(
                    application::TransmissionAuditRejectionReason::voice_control_unavailable))
    {
        throw PersistenceError{"The audit database contains an invalid rejection reason."};
    }
    return static_cast<application::TransmissionAuditRejectionReason>(value);
}

auto voiceBanFromInteger(sqlite3_int64 value) -> domain::VoiceBanStatus
{
    if (value < static_cast<sqlite3_int64>(domain::VoiceBanStatus::none) ||
        value > static_cast<sqlite3_int64>(domain::VoiceBanStatus::permanent))
    {
        throw PersistenceError{"The membership database contains an invalid voice-ban status."};
    }
    return static_cast<domain::VoiceBanStatus>(value);
}

auto versionToText(std::uint64_t version) -> std::string
{
    std::array<char, 20> buffer{};
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), version);
    if (error != std::errc{})
    {
        throw PersistenceError{"Could not serialize a membership version."};
    }
    return {buffer.data(), end};
}

auto versionFromText(std::string_view text) -> std::uint64_t
{
    std::uint64_t version{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), version);
    if (error != std::errc{} || end != text.data() + text.size() || version == 0)
    {
        throw PersistenceError{"The membership database contains an invalid version."};
    }
    return version;
}

auto unsignedFromText(std::string_view text, std::string_view field) -> std::uint64_t
{
    std::uint64_t value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
    {
        throw PersistenceError{"The audit database contains an invalid " + std::string{field} +
                               '.'};
    }
    return value;
}

auto countToText(std::size_t count) -> std::string
{
    return versionToText(static_cast<std::uint64_t>(count));
}

auto countFromText(std::string_view text) -> std::size_t
{
    const auto count = unsignedFromText(text, "recipient count");
    if (count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
    {
        throw PersistenceError{"The audit database contains an invalid recipient count."};
    }
    return static_cast<std::size_t>(count);
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

auto limitToInteger(std::size_t limit) -> sqlite3_int64
{
    if (limit > static_cast<std::size_t>(std::numeric_limits<sqlite3_int64>::max()))
    {
        throw PersistenceError{"The requested SQLite result limit is too large."};
    }
    return static_cast<sqlite3_int64>(limit);
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
    Migration{
        2,
        "create_authoritative_memberships",
        R"sql(
CREATE TABLE membership_contexts (
    owner_player_id TEXT PRIMARY KEY NOT NULL CHECK(length(owner_player_id) > 0),
    version TEXT NOT NULL CHECK(length(version) > 0),
    hierarchy_id TEXT NOT NULL CHECK(length(hierarchy_id) > 0)
) WITHOUT ROWID;
CREATE TABLE membership_scopes (
    owner_player_id TEXT NOT NULL,
    scope INTEGER NOT NULL CHECK(scope BETWEEN 0 AND 2),
    display_name TEXT NOT NULL CHECK(length(display_name) > 0),
    priority INTEGER NOT NULL,
    max_concurrent_speakers INTEGER CHECK(max_concurrent_speakers >= 0),
    PRIMARY KEY(owner_player_id, scope),
    FOREIGN KEY(owner_player_id) REFERENCES membership_contexts(owner_player_id) ON DELETE CASCADE
) WITHOUT ROWID;
CREATE TABLE membership_groups (
    owner_player_id TEXT NOT NULL,
    group_id TEXT NOT NULL CHECK(length(group_id) > 0),
    display_name TEXT NOT NULL,
    active INTEGER NOT NULL CHECK(active IN (0, 1)),
    PRIMARY KEY(owner_player_id, group_id),
    FOREIGN KEY(owner_player_id) REFERENCES membership_contexts(owner_player_id) ON DELETE CASCADE
) WITHOUT ROWID;
CREATE TABLE membership_specializations (
    owner_player_id TEXT NOT NULL,
    specialization_id TEXT NOT NULL CHECK(length(specialization_id) > 0),
    group_id TEXT NOT NULL CHECK(length(group_id) > 0),
    display_name TEXT NOT NULL,
    PRIMARY KEY(owner_player_id, specialization_id),
    FOREIGN KEY(owner_player_id, group_id)
        REFERENCES membership_groups(owner_player_id, group_id) ON DELETE CASCADE
) WITHOUT ROWID;
CREATE TABLE membership_teams (
    owner_player_id TEXT NOT NULL,
    team_id TEXT NOT NULL CHECK(length(team_id) > 0),
    specialization_id TEXT NOT NULL CHECK(length(specialization_id) > 0),
    display_name TEXT NOT NULL,
    PRIMARY KEY(owner_player_id, team_id),
    FOREIGN KEY(owner_player_id, specialization_id)
        REFERENCES membership_specializations(owner_player_id, specialization_id) ON DELETE CASCADE
) WITHOUT ROWID;
CREATE TABLE memberships (
    owner_player_id TEXT NOT NULL,
    player_id TEXT NOT NULL CHECK(length(player_id) > 0),
    group_id TEXT NOT NULL,
    specialization_id TEXT NOT NULL,
    team_id TEXT NOT NULL,
    connected INTEGER NOT NULL CHECK(connected IN (0, 1)),
    can_receive_voice INTEGER NOT NULL CHECK(can_receive_voice IN (0, 1)),
    transmit_muted INTEGER NOT NULL CHECK(transmit_muted IN (0, 1)),
    voice_ban_status INTEGER NOT NULL CHECK(voice_ban_status BETWEEN 0 AND 2),
    PRIMARY KEY(owner_player_id, player_id),
    FOREIGN KEY(owner_player_id, group_id)
        REFERENCES membership_groups(owner_player_id, group_id) ON DELETE CASCADE,
    FOREIGN KEY(owner_player_id, specialization_id)
        REFERENCES membership_specializations(owner_player_id, specialization_id) ON DELETE CASCADE,
    FOREIGN KEY(owner_player_id, team_id)
        REFERENCES membership_teams(owner_player_id, team_id) ON DELETE CASCADE
) WITHOUT ROWID;
CREATE TABLE membership_role_assignments (
    owner_player_id TEXT NOT NULL,
    player_id TEXT NOT NULL,
    role_id TEXT NOT NULL CHECK(length(role_id) > 0),
    PRIMARY KEY(owner_player_id, player_id, role_id),
    FOREIGN KEY(owner_player_id, player_id)
        REFERENCES memberships(owner_player_id, player_id) ON DELETE CASCADE
) WITHOUT ROWID;
CREATE TABLE role_permissions (
    owner_player_id TEXT NOT NULL,
    role_id TEXT NOT NULL CHECK(length(role_id) > 0),
    PRIMARY KEY(owner_player_id, role_id),
    FOREIGN KEY(owner_player_id) REFERENCES membership_contexts(owner_player_id) ON DELETE CASCADE
) WITHOUT ROWID;
CREATE TABLE role_transmit_scopes (
    owner_player_id TEXT NOT NULL,
    role_id TEXT NOT NULL,
    scope INTEGER NOT NULL CHECK(scope BETWEEN 0 AND 2),
    PRIMARY KEY(owner_player_id, role_id, scope),
    FOREIGN KEY(owner_player_id, role_id)
        REFERENCES role_permissions(owner_player_id, role_id) ON DELETE CASCADE
) WITHOUT ROWID;
CREATE TABLE role_receive_scopes (
    owner_player_id TEXT NOT NULL,
    role_id TEXT NOT NULL,
    scope INTEGER NOT NULL CHECK(scope BETWEEN 0 AND 2),
    PRIMARY KEY(owner_player_id, role_id, scope),
    FOREIGN KEY(owner_player_id, role_id)
        REFERENCES role_permissions(owner_player_id, role_id) ON DELETE CASCADE
) WITHOUT ROWID;
)sql",
    },
    Migration{
        3,
        "create_transmission_audit_events",
        R"sql(
CREATE TABLE transmission_audit_events (
    sequence INTEGER PRIMARY KEY AUTOINCREMENT,
    event_type INTEGER NOT NULL CHECK(event_type BETWEEN 0 AND 3),
    operation INTEGER NOT NULL CHECK(operation BETWEEN 0 AND 5),
    occurred_at_unix_ms INTEGER NOT NULL,
    correlation_id TEXT NOT NULL CHECK(length(correlation_id) > 0),
    session_id TEXT,
    device_id TEXT,
    client_transmission_id TEXT,
    transmission_id TEXT,
    actor_player_id TEXT,
    sender_player_id TEXT,
    scope INTEGER CHECK(scope BETWEEN 0 AND 2),
    membership_version TEXT,
    recipient_count TEXT,
    stop_reason INTEGER CHECK(stop_reason BETWEEN 0 AND 9),
    rejection_reason INTEGER CHECK(rejection_reason BETWEEN 0 AND 17),
    CHECK(session_id IS NULL OR length(session_id) > 0),
    CHECK(device_id IS NULL OR length(device_id) > 0),
    CHECK(client_transmission_id IS NULL OR length(client_transmission_id) > 0),
    CHECK(transmission_id IS NULL OR length(transmission_id) > 0),
    CHECK(actor_player_id IS NULL OR length(actor_player_id) > 0),
    CHECK(sender_player_id IS NULL OR length(sender_player_id) > 0),
    CHECK(membership_version IS NULL OR length(membership_version) > 0),
    CHECK(recipient_count IS NULL OR length(recipient_count) > 0)
);
CREATE INDEX transmission_audit_events_by_time
    ON transmission_audit_events(occurred_at_unix_ms, sequence);
)sql",
    },
    Migration{
        4,
        "extend_transmission_audit_rejections",
        R"sql(
ALTER TABLE transmission_audit_events RENAME TO transmission_audit_events_v3;
CREATE TABLE transmission_audit_events (
    sequence INTEGER PRIMARY KEY AUTOINCREMENT,
    event_type INTEGER NOT NULL CHECK(event_type BETWEEN 0 AND 3),
    operation INTEGER NOT NULL CHECK(operation BETWEEN 0 AND 5),
    occurred_at_unix_ms INTEGER NOT NULL,
    correlation_id TEXT NOT NULL CHECK(length(correlation_id) > 0),
    session_id TEXT,
    device_id TEXT,
    client_transmission_id TEXT,
    transmission_id TEXT,
    actor_player_id TEXT,
    sender_player_id TEXT,
    scope INTEGER CHECK(scope BETWEEN 0 AND 2),
    membership_version TEXT,
    recipient_count TEXT,
    stop_reason INTEGER CHECK(stop_reason BETWEEN 0 AND 9),
    rejection_reason INTEGER CHECK(rejection_reason BETWEEN 0 AND 19),
    CHECK(session_id IS NULL OR length(session_id) > 0),
    CHECK(device_id IS NULL OR length(device_id) > 0),
    CHECK(client_transmission_id IS NULL OR length(client_transmission_id) > 0),
    CHECK(transmission_id IS NULL OR length(transmission_id) > 0),
    CHECK(actor_player_id IS NULL OR length(actor_player_id) > 0),
    CHECK(sender_player_id IS NULL OR length(sender_player_id) > 0),
    CHECK(membership_version IS NULL OR length(membership_version) > 0),
    CHECK(recipient_count IS NULL OR length(recipient_count) > 0)
);
INSERT INTO transmission_audit_events
SELECT * FROM transmission_audit_events_v3;
DROP TABLE transmission_audit_events_v3;
CREATE INDEX transmission_audit_events_by_time
    ON transmission_audit_events(occurred_at_unix_ms, sequence);
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
    if (current_version > SqliteControlPlaneRepository::latest_schema_version)
    {
        std::ostringstream message;
        message << "Database schema version " << current_version
                << " is newer than the supported version "
                << SqliteControlPlaneRepository::latest_schema_version << '.';
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

void reset(sqlite3* database, sqlite3_stmt* statement)
{
    if (sqlite3_reset(statement) != SQLITE_OK || sqlite3_clear_bindings(statement) != SQLITE_OK)
    {
        throwDatabaseError(database, "Could not reset SQLite statement");
    }
}

void requireDone(sqlite3* database, sqlite3_stmt* statement, std::string_view operation)
{
    if (sqlite3_step(statement) != SQLITE_DONE)
    {
        throwDatabaseError(database, operation);
    }
}

template <typename Enum>
void bindOptionalEnum(sqlite3* database, sqlite3_stmt* statement, int index,
                      const std::optional<Enum>& value)
{
    if (value)
    {
        bindInteger(database, statement, index, static_cast<std::int64_t>(*value));
    }
    else
    {
        bindNull(database, statement, index);
    }
}

template <typename Enum, typename Converter>
auto readOptionalEnum(sqlite3_stmt* statement, int column, Converter converter)
    -> std::optional<Enum>
{
    if (sqlite3_column_type(statement, column) == SQLITE_NULL)
    {
        return std::nullopt;
    }
    return converter(sqlite3_column_int64(statement, column));
}

void bindOptionalUnsigned(sqlite3* database, sqlite3_stmt* statement, int index,
                          const std::optional<std::uint64_t>& value)
{
    if (value)
    {
        bindText(database, statement, index, versionToText(*value));
    }
    else
    {
        bindNull(database, statement, index);
    }
}

auto readOptionalVersion(sqlite3_stmt* statement, int column) -> std::optional<std::uint64_t>
{
    if (sqlite3_column_type(statement, column) == SQLITE_NULL)
    {
        return std::nullopt;
    }
    return unsignedFromText(readText(statement, column), "membership version");
}

void bindOptionalCount(sqlite3* database, sqlite3_stmt* statement, int index,
                       const std::optional<std::size_t>& value)
{
    if (value)
    {
        bindText(database, statement, index, countToText(*value));
    }
    else
    {
        bindNull(database, statement, index);
    }
}

auto readOptionalCount(sqlite3_stmt* statement, int column) -> std::optional<std::size_t>
{
    if (sqlite3_column_type(statement, column) == SQLITE_NULL)
    {
        return std::nullopt;
    }
    return countFromText(readText(statement, column));
}

auto readAssignedRoles(sqlite3* database, const domain::PlayerId& owner_player_id,
                       const domain::PlayerId& member_player_id) -> std::vector<domain::RoleId>
{
    auto statement =
        prepare(database, "SELECT role_id FROM membership_role_assignments "
                          "WHERE owner_player_id = ?1 AND player_id = ?2 ORDER BY role_id;");
    bindText(database, statement.get(), 1, owner_player_id.value());
    bindText(database, statement.get(), 2, member_player_id.value());

    std::vector<domain::RoleId> roles;
    for (auto result = sqlite3_step(statement.get()); result != SQLITE_DONE;
         result = sqlite3_step(statement.get()))
    {
        if (result != SQLITE_ROW)
        {
            throwDatabaseError(database, "Could not load membership role assignments");
        }
        roles.emplace_back(readText(statement.get(), 0));
    }
    return roles;
}

auto readRoleScopes(sqlite3* database, std::string_view table,
                    const domain::PlayerId& owner_player_id, const domain::RoleId& role_id)
    -> std::vector<domain::VoiceScope>
{
    const auto sql = "SELECT scope FROM " + std::string{table} +
                     " WHERE owner_player_id = ?1 AND role_id = ?2 ORDER BY scope;";
    auto statement = prepare(database, sql.c_str());
    bindText(database, statement.get(), 1, owner_player_id.value());
    bindText(database, statement.get(), 2, role_id.value());

    std::vector<domain::VoiceScope> scopes;
    for (auto result = sqlite3_step(statement.get()); result != SQLITE_DONE;
         result = sqlite3_step(statement.get()))
    {
        if (result != SQLITE_ROW)
        {
            throwDatabaseError(database, "Could not load role scopes");
        }
        scopes.push_back(scopeFromInteger(sqlite3_column_int64(statement.get(), 0)));
    }
    return scopes;
}

auto loadMembershipContext(sqlite3* database, const domain::PlayerId& owner_player_id)
    -> std::optional<application::AuthoritativeMembershipContext>
{
    auto context_statement = prepare(
        database,
        "SELECT version, hierarchy_id FROM membership_contexts WHERE owner_player_id = ?1;");
    bindText(database, context_statement.get(), 1, owner_player_id.value());
    const auto context_result = sqlite3_step(context_statement.get());
    if (context_result == SQLITE_DONE)
    {
        return std::nullopt;
    }
    if (context_result != SQLITE_ROW)
    {
        throwDatabaseError(database, "Could not load the authoritative membership context");
    }
    const auto version = versionFromText(readText(context_statement.get(), 0));
    const domain::HierarchyId hierarchy_id{readText(context_statement.get(), 1)};

    std::vector<domain::ScopeDefinition> scopes;
    auto scope_statement =
        prepare(database, "SELECT scope, display_name, priority, max_concurrent_speakers "
                          "FROM membership_scopes WHERE owner_player_id = ?1 ORDER BY scope;");
    bindText(database, scope_statement.get(), 1, owner_player_id.value());
    for (auto result = sqlite3_step(scope_statement.get()); result != SQLITE_DONE;
         result = sqlite3_step(scope_statement.get()))
    {
        if (result != SQLITE_ROW)
        {
            throwDatabaseError(database, "Could not load membership scopes");
        }
        std::optional<std::size_t> speaker_limit;
        if (sqlite3_column_type(scope_statement.get(), 3) != SQLITE_NULL)
        {
            const auto stored_limit = sqlite3_column_int64(scope_statement.get(), 3);
            if (stored_limit < 0 ||
                static_cast<std::uint64_t>(stored_limit) >
                    static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
            {
                throw PersistenceError{
                    "The membership database contains an invalid speaker limit."};
            }
            speaker_limit = static_cast<std::size_t>(stored_limit);
        }
        scopes.emplace_back(scopeFromInteger(sqlite3_column_int64(scope_statement.get(), 0)),
                            readText(scope_statement.get(), 1),
                            static_cast<int>(sqlite3_column_int(scope_statement.get(), 2)),
                            speaker_limit);
    }

    std::vector<domain::Group> groups;
    auto group_statement =
        prepare(database, "SELECT group_id, display_name, active FROM membership_groups "
                          "WHERE owner_player_id = ?1 ORDER BY group_id;");
    bindText(database, group_statement.get(), 1, owner_player_id.value());
    for (auto result = sqlite3_step(group_statement.get()); result != SQLITE_DONE;
         result = sqlite3_step(group_statement.get()))
    {
        if (result != SQLITE_ROW)
        {
            throwDatabaseError(database, "Could not load membership groups");
        }
        groups.emplace_back(domain::GroupId{readText(group_statement.get(), 0)},
                            readString(group_statement.get(), 1),
                            readBoolean(group_statement.get(), 2));
    }

    std::vector<domain::Specialization> specializations;
    auto specialization_statement =
        prepare(database,
                "SELECT specialization_id, group_id, display_name FROM membership_specializations "
                "WHERE owner_player_id = ?1 ORDER BY specialization_id;");
    bindText(database, specialization_statement.get(), 1, owner_player_id.value());
    for (auto result = sqlite3_step(specialization_statement.get()); result != SQLITE_DONE;
         result = sqlite3_step(specialization_statement.get()))
    {
        if (result != SQLITE_ROW)
        {
            throwDatabaseError(database, "Could not load membership specializations");
        }
        specializations.emplace_back(
            domain::SpecializationId{readText(specialization_statement.get(), 0)},
            domain::GroupId{readText(specialization_statement.get(), 1)},
            readString(specialization_statement.get(), 2));
    }

    std::vector<domain::Team> teams;
    auto team_statement =
        prepare(database, "SELECT team_id, specialization_id, display_name FROM membership_teams "
                          "WHERE owner_player_id = ?1 ORDER BY team_id;");
    bindText(database, team_statement.get(), 1, owner_player_id.value());
    for (auto result = sqlite3_step(team_statement.get()); result != SQLITE_DONE;
         result = sqlite3_step(team_statement.get()))
    {
        if (result != SQLITE_ROW)
        {
            throwDatabaseError(database, "Could not load membership teams");
        }
        teams.emplace_back(domain::TeamId{readText(team_statement.get(), 0)},
                           domain::SpecializationId{readText(team_statement.get(), 1)},
                           readString(team_statement.get(), 2));
    }

    std::vector<domain::VoiceMembership> memberships;
    auto membership_statement =
        prepare(database, "SELECT player_id, group_id, specialization_id, team_id, connected, "
                          "can_receive_voice, transmit_muted, voice_ban_status FROM memberships "
                          "WHERE owner_player_id = ?1 ORDER BY player_id;");
    bindText(database, membership_statement.get(), 1, owner_player_id.value());
    for (auto result = sqlite3_step(membership_statement.get()); result != SQLITE_DONE;
         result = sqlite3_step(membership_statement.get()))
    {
        if (result != SQLITE_ROW)
        {
            throwDatabaseError(database, "Could not load memberships");
        }
        domain::PlayerId player_id{readText(membership_statement.get(), 0)};
        domain::VoiceMembership membership{
            player_id, domain::GroupId{readText(membership_statement.get(), 1)},
            domain::SpecializationId{readText(membership_statement.get(), 2)},
            domain::TeamId{readText(membership_statement.get(), 3)},
            readAssignedRoles(database, owner_player_id, player_id)};
        membership.connected = readBoolean(membership_statement.get(), 4);
        membership.can_receive_voice = readBoolean(membership_statement.get(), 5);
        membership.transmit_muted = readBoolean(membership_statement.get(), 6);
        membership.voice_ban_status =
            voiceBanFromInteger(sqlite3_column_int64(membership_statement.get(), 7));
        memberships.push_back(std::move(membership));
    }

    std::vector<domain::RolePermissions> role_permissions;
    auto role_statement = prepare(
        database,
        "SELECT role_id FROM role_permissions WHERE owner_player_id = ?1 ORDER BY role_id;");
    bindText(database, role_statement.get(), 1, owner_player_id.value());
    for (auto result = sqlite3_step(role_statement.get()); result != SQLITE_DONE;
         result = sqlite3_step(role_statement.get()))
    {
        if (result != SQLITE_ROW)
        {
            throwDatabaseError(database, "Could not load role permissions");
        }
        domain::RoleId role_id{readText(role_statement.get(), 0)};
        role_permissions.emplace_back(
            role_id, readRoleScopes(database, "role_transmit_scopes", owner_player_id, role_id),
            readRoleScopes(database, "role_receive_scopes", owner_player_id, role_id));
    }

    auto snapshot = std::make_shared<const domain::MembershipSnapshot>(
        version,
        domain::Hierarchy{hierarchy_id, std::move(scopes), std::move(groups),
                          std::move(specializations), std::move(teams)},
        std::move(memberships));
    auto role_policy = std::make_shared<const domain::RolePolicy>(std::move(role_permissions));
    return application::AuthoritativeMembershipContext{std::move(snapshot), std::move(role_policy)};
}

void bindOwnerAndRole(sqlite3* database, sqlite3_stmt* statement,
                      const domain::PlayerId& owner_player_id, const domain::RoleId& role_id)
{
    bindText(database, statement, 1, owner_player_id.value());
    bindText(database, statement, 2, role_id.value());
}

void persistMembershipContext(sqlite3* database, const domain::PlayerId& owner_player_id,
                              const application::AuthoritativeMembershipContext& context)
{
    auto context_statement =
        prepare(database, "INSERT INTO membership_contexts(owner_player_id, version, hierarchy_id) "
                          "VALUES(?1, ?2, ?3);");
    bindText(database, context_statement.get(), 1, owner_player_id.value());
    bindText(database, context_statement.get(), 2, versionToText(context.snapshot->version()));
    bindText(database, context_statement.get(), 3, context.snapshot->hierarchy().id().value());
    requireDone(database, context_statement.get(), "Could not persist the membership context");

    auto scope_statement = prepare(
        database, "INSERT INTO membership_scopes(owner_player_id, scope, display_name, priority, "
                  "max_concurrent_speakers) VALUES(?1, ?2, ?3, ?4, ?5);");
    for (const auto& scope : context.snapshot->hierarchy().scopes())
    {
        bindText(database, scope_statement.get(), 1, owner_player_id.value());
        bindInteger(database, scope_statement.get(), 2, scopeToInteger(scope.scope));
        bindText(database, scope_statement.get(), 3, scope.display_name);
        bindInteger(database, scope_statement.get(), 4, scope.priority);
        if (scope.max_concurrent_speakers)
        {
            if (*scope.max_concurrent_speakers >
                static_cast<std::size_t>(std::numeric_limits<sqlite3_int64>::max()))
            {
                throw PersistenceError{"A speaker limit exceeds SQLite's integer range."};
            }
            bindInteger(database, scope_statement.get(), 5,
                        static_cast<std::int64_t>(*scope.max_concurrent_speakers));
        }
        else if (sqlite3_bind_null(scope_statement.get(), 5) != SQLITE_OK)
        {
            throwDatabaseError(database, "Could not bind an empty speaker limit");
        }
        requireDone(database, scope_statement.get(), "Could not persist a membership scope");
        reset(database, scope_statement.get());
    }

    auto group_statement = prepare(
        database, "INSERT INTO membership_groups(owner_player_id, group_id, display_name, active) "
                  "VALUES(?1, ?2, ?3, ?4);");
    for (const auto& group : context.snapshot->hierarchy().groups())
    {
        bindText(database, group_statement.get(), 1, owner_player_id.value());
        bindText(database, group_statement.get(), 2, group.id.value());
        bindText(database, group_statement.get(), 3, group.display_name);
        bindInteger(database, group_statement.get(), 4, group.active ? 1 : 0);
        requireDone(database, group_statement.get(), "Could not persist a membership group");
        reset(database, group_statement.get());
    }

    auto specialization_statement = prepare(
        database,
        "INSERT INTO membership_specializations(owner_player_id, specialization_id, group_id, "
        "display_name) VALUES(?1, ?2, ?3, ?4);");
    for (const auto& specialization : context.snapshot->hierarchy().specializations())
    {
        bindText(database, specialization_statement.get(), 1, owner_player_id.value());
        bindText(database, specialization_statement.get(), 2, specialization.id.value());
        bindText(database, specialization_statement.get(), 3, specialization.group_id.value());
        bindText(database, specialization_statement.get(), 4, specialization.display_name);
        requireDone(database, specialization_statement.get(),
                    "Could not persist a membership specialization");
        reset(database, specialization_statement.get());
    }

    auto team_statement = prepare(
        database,
        "INSERT INTO membership_teams(owner_player_id, team_id, specialization_id, display_name) "
        "VALUES(?1, ?2, ?3, ?4);");
    for (const auto& team : context.snapshot->hierarchy().teams())
    {
        bindText(database, team_statement.get(), 1, owner_player_id.value());
        bindText(database, team_statement.get(), 2, team.id.value());
        bindText(database, team_statement.get(), 3, team.specialization_id.value());
        bindText(database, team_statement.get(), 4, team.display_name);
        requireDone(database, team_statement.get(), "Could not persist a membership team");
        reset(database, team_statement.get());
    }

    auto role_statement =
        prepare(database, "INSERT INTO role_permissions(owner_player_id, role_id) VALUES(?1, ?2);");
    auto transmit_scope_statement = prepare(
        database,
        "INSERT INTO role_transmit_scopes(owner_player_id, role_id, scope) VALUES(?1, ?2, ?3);");
    auto receive_scope_statement = prepare(
        database,
        "INSERT INTO role_receive_scopes(owner_player_id, role_id, scope) VALUES(?1, ?2, ?3);");
    for (const auto& role : context.role_policy->roles())
    {
        bindOwnerAndRole(database, role_statement.get(), owner_player_id, role.role_id);
        requireDone(database, role_statement.get(), "Could not persist role permissions");
        reset(database, role_statement.get());

        for (const auto scope : role.transmit_scopes)
        {
            bindOwnerAndRole(database, transmit_scope_statement.get(), owner_player_id,
                             role.role_id);
            bindInteger(database, transmit_scope_statement.get(), 3, scopeToInteger(scope));
            requireDone(database, transmit_scope_statement.get(),
                        "Could not persist a role transmit scope");
            reset(database, transmit_scope_statement.get());
        }
        for (const auto scope : role.receive_scopes)
        {
            bindOwnerAndRole(database, receive_scope_statement.get(), owner_player_id,
                             role.role_id);
            bindInteger(database, receive_scope_statement.get(), 3, scopeToInteger(scope));
            requireDone(database, receive_scope_statement.get(),
                        "Could not persist a role receive scope");
            reset(database, receive_scope_statement.get());
        }
    }

    auto membership_statement =
        prepare(database,
                "INSERT INTO memberships(owner_player_id, player_id, group_id, specialization_id, "
                "team_id, connected, can_receive_voice, transmit_muted, voice_ban_status) "
                "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9);");
    auto assignment_statement = prepare(
        database, "INSERT INTO membership_role_assignments(owner_player_id, player_id, role_id) "
                  "VALUES(?1, ?2, ?3);");
    for (const auto& membership : context.snapshot->memberships())
    {
        bindText(database, membership_statement.get(), 1, owner_player_id.value());
        bindText(database, membership_statement.get(), 2, membership.player_id.value());
        bindText(database, membership_statement.get(), 3, membership.group_id.value());
        bindText(database, membership_statement.get(), 4, membership.specialization_id.value());
        bindText(database, membership_statement.get(), 5, membership.team_id.value());
        bindInteger(database, membership_statement.get(), 6, membership.connected ? 1 : 0);
        bindInteger(database, membership_statement.get(), 7, membership.can_receive_voice ? 1 : 0);
        bindInteger(database, membership_statement.get(), 8, membership.transmit_muted ? 1 : 0);
        bindInteger(database, membership_statement.get(), 9,
                    static_cast<std::int64_t>(membership.voice_ban_status));
        requireDone(database, membership_statement.get(), "Could not persist a membership");
        reset(database, membership_statement.get());

        for (const auto& role_id : membership.role_ids)
        {
            bindText(database, assignment_statement.get(), 1, owner_player_id.value());
            bindText(database, assignment_statement.get(), 2, membership.player_id.value());
            bindText(database, assignment_statement.get(), 3, role_id.value());
            requireDone(database, assignment_statement.get(),
                        "Could not persist a membership role assignment");
            reset(database, assignment_statement.get());
        }
    }
}
} // namespace

class SqliteControlPlaneRepository::Implementation final
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
    std::atomic<std::uint64_t> dropped_audit_events{};
};

SqliteControlPlaneRepository::SqliteControlPlaneRepository(std::filesystem::path database_path)
    : implementation_(std::make_unique<Implementation>(database_path))
{
}

SqliteControlPlaneRepository::~SqliteControlPlaneRepository() = default;

auto SqliteControlPlaneRepository::find(const domain::SessionId& session_id) const
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

void SqliteControlPlaneRepository::upsert(application::AuthenticatedSession session)
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

auto SqliteControlPlaneRepository::erase(const domain::SessionId& session_id) -> bool
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

auto SqliteControlPlaneRepository::expiredSessionIds(application::TimePoint now,
                                                     std::size_t limit) const
    -> std::vector<domain::SessionId>
{
    if (limit == 0)
    {
        throw std::invalid_argument{"The expired-session query limit must be positive."};
    }
    std::scoped_lock lock{implementation_->mutex};
    auto statement = prepare(implementation_->database.get(),
                             "SELECT session_id FROM sessions WHERE expires_at_unix_ms <= ?1 "
                             "ORDER BY expires_at_unix_ms, session_id LIMIT ?2;");
    bindInteger(implementation_->database.get(), statement.get(), 1, toUnixMilliseconds(now));
    bindInteger(implementation_->database.get(), statement.get(), 2, limitToInteger(limit));
    std::vector<domain::SessionId> identifiers;
    while (true)
    {
        const auto result = sqlite3_step(statement.get());
        if (result == SQLITE_DONE)
        {
            return identifiers;
        }
        if (result != SQLITE_ROW)
        {
            throwDatabaseError(implementation_->database.get(), "Could not query expired sessions");
        }
        identifiers.emplace_back(readText(statement.get(), 0));
    }
}

auto SqliteControlPlaneRepository::currentFor(const domain::PlayerId& player_id) const
    -> std::optional<application::AuthoritativeMembershipContext>
{
    std::scoped_lock lock{implementation_->mutex};
    return loadMembershipContext(implementation_->database.get(), player_id);
}

auto SqliteControlPlaneRepository::upsertIfNewer(
    const domain::PlayerId& player_id, application::AuthoritativeMembershipContext context)
    -> std::optional<application::AuthoritativeMembershipWriteError>
{
    if (context.snapshot->find(player_id) == nullptr)
    {
        return application::AuthoritativeMembershipWriteError::player_not_in_snapshot;
    }

    std::scoped_lock lock{implementation_->mutex};
    auto* database = implementation_->database.get();
    execute(database, "BEGIN IMMEDIATE;");
    try
    {
        auto version_statement = prepare(
            database, "SELECT version FROM membership_contexts WHERE owner_player_id = ?1;");
        bindText(database, version_statement.get(), 1, player_id.value());
        const auto version_result = sqlite3_step(version_statement.get());
        if (version_result == SQLITE_ROW &&
            context.snapshot->version() <= versionFromText(readText(version_statement.get(), 0)))
        {
            execute(database, "ROLLBACK;");
            return application::AuthoritativeMembershipWriteError::version_not_newer;
        }
        if (version_result != SQLITE_ROW && version_result != SQLITE_DONE)
        {
            throwDatabaseError(database, "Could not read the current membership version");
        }
        version_statement.reset();

        auto delete_statement =
            prepare(database, "DELETE FROM membership_contexts WHERE owner_player_id = ?1;");
        bindText(database, delete_statement.get(), 1, player_id.value());
        requireDone(database, delete_statement.get(), "Could not replace the membership context");

        persistMembershipContext(database, player_id, context);
        execute(database, "COMMIT;");
    }
    catch (...)
    {
        static_cast<void>(sqlite3_exec(database, "ROLLBACK;", nullptr, nullptr, nullptr));
        throw;
    }
    return std::nullopt;
}

auto SqliteControlPlaneRepository::erase(const domain::PlayerId& player_id) -> bool
{
    std::scoped_lock lock{implementation_->mutex};
    auto statement = prepare(implementation_->database.get(),
                             "DELETE FROM membership_contexts WHERE owner_player_id = ?1;");
    bindText(implementation_->database.get(), statement.get(), 1, player_id.value());
    requireDone(implementation_->database.get(), statement.get(),
                "Could not erase the membership context");
    return sqlite3_changes(implementation_->database.get()) != 0;
}

void SqliteControlPlaneRepository::record(const application::TransmissionAuditEvent& event) noexcept
{
    try
    {
        std::scoped_lock lock{implementation_->mutex};
        auto* database = implementation_->database.get();
        auto statement = prepare(
            database,
            "INSERT INTO transmission_audit_events("
            "event_type, operation, occurred_at_unix_ms, correlation_id, session_id, device_id, "
            "client_transmission_id, transmission_id, actor_player_id, sender_player_id, scope, "
            "membership_version, recipient_count, stop_reason, rejection_reason) "
            "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, ?15);");

        bindInteger(database, statement.get(), 1, static_cast<std::int64_t>(event.type));
        bindInteger(database, statement.get(), 2, static_cast<std::int64_t>(event.operation));
        bindInteger(database, statement.get(), 3, toUnixMilliseconds(event.occurred_at));
        bindText(database, statement.get(), 4, event.correlation_id.value());
        bindOptionalIdentifier(database, statement.get(), 5, event.session_id);
        bindOptionalIdentifier(database, statement.get(), 6, event.device_id);
        bindOptionalIdentifier(database, statement.get(), 7, event.client_transmission_id);
        bindOptionalIdentifier(database, statement.get(), 8, event.transmission_id);
        bindOptionalIdentifier(database, statement.get(), 9, event.actor_player_id);
        bindOptionalIdentifier(database, statement.get(), 10, event.sender_player_id);
        bindOptionalEnum(database, statement.get(), 11, event.scope);
        bindOptionalUnsigned(database, statement.get(), 12, event.membership_version);
        bindOptionalCount(database, statement.get(), 13, event.recipient_count);
        bindOptionalEnum(database, statement.get(), 14, event.stop_reason);
        bindOptionalEnum(database, statement.get(), 15, event.rejection_reason);
        requireDone(database, statement.get(), "Could not persist the transmission audit event");
    }
    catch (...)
    {
        implementation_->dropped_audit_events.fetch_add(1, std::memory_order_relaxed);
    }
}

auto SqliteControlPlaneRepository::auditEventsAfter(std::uint64_t sequence, std::size_t limit) const
    -> std::vector<StoredTransmissionAuditEvent>
{
    if (limit == 0 ||
        sequence > static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()))
    {
        return {};
    }

    std::scoped_lock lock{implementation_->mutex};
    auto* database = implementation_->database.get();
    auto statement = prepare(
        database,
        "SELECT sequence, event_type, operation, occurred_at_unix_ms, correlation_id, session_id, "
        "device_id, client_transmission_id, transmission_id, actor_player_id, sender_player_id, "
        "scope, membership_version, recipient_count, stop_reason, rejection_reason "
        "FROM transmission_audit_events WHERE sequence > ?1 ORDER BY sequence LIMIT ?2;");
    bindInteger(database, statement.get(), 1, static_cast<sqlite3_int64>(sequence));
    bindInteger(database, statement.get(), 2, limitToInteger(limit));

    std::vector<StoredTransmissionAuditEvent> events;
    while (true)
    {
        const auto result = sqlite3_step(statement.get());
        if (result == SQLITE_DONE)
        {
            return events;
        }
        if (result != SQLITE_ROW)
        {
            throwDatabaseError(database, "Could not load transmission audit events");
        }

        const auto stored_sequence = sqlite3_column_int64(statement.get(), 0);
        if (stored_sequence <= 0)
        {
            throw PersistenceError{"The audit database contains an invalid event sequence."};
        }

        application::TransmissionAuditEvent event{
            auditEventTypeFromInteger(sqlite3_column_int64(statement.get(), 1)),
            auditOperationFromInteger(sqlite3_column_int64(statement.get(), 2)),
            fromUnixMilliseconds(sqlite3_column_int64(statement.get(), 3)),
            domain::CorrelationId{readText(statement.get(), 4)}};
        event.session_id = readOptionalIdentifier<domain::SessionId>(statement.get(), 5);
        event.device_id = readOptionalIdentifier<domain::DeviceId>(statement.get(), 6);
        event.client_transmission_id =
            readOptionalIdentifier<domain::ClientTransmissionId>(statement.get(), 7);
        event.transmission_id = readOptionalIdentifier<domain::TransmissionId>(statement.get(), 8);
        event.actor_player_id = readOptionalIdentifier<domain::PlayerId>(statement.get(), 9);
        event.sender_player_id = readOptionalIdentifier<domain::PlayerId>(statement.get(), 10);
        event.scope = readOptionalEnum<domain::VoiceScope>(statement.get(), 11, scopeFromInteger);
        event.membership_version = readOptionalVersion(statement.get(), 12);
        event.recipient_count = readOptionalCount(statement.get(), 13);
        event.stop_reason = readOptionalEnum<domain::TransmissionStopReason>(statement.get(), 14,
                                                                             stopReasonFromInteger);
        event.rejection_reason = readOptionalEnum<application::TransmissionAuditRejectionReason>(
            statement.get(), 15, auditRejectionFromInteger);

        events.push_back({static_cast<std::uint64_t>(stored_sequence), std::move(event)});
    }
}

auto SqliteControlPlaneRepository::eraseAuditEventsBefore(application::TimePoint exclusive_cutoff,
                                                          std::size_t limit) -> std::size_t
{
    if (limit == 0)
    {
        return 0;
    }

    std::scoped_lock lock{implementation_->mutex};
    auto* database = implementation_->database.get();
    auto statement = prepare(
        database, "DELETE FROM transmission_audit_events WHERE sequence IN ("
                  "SELECT sequence FROM transmission_audit_events WHERE occurred_at_unix_ms < ?1 "
                  "ORDER BY sequence LIMIT ?2);");
    bindInteger(database, statement.get(), 1, toUnixMilliseconds(exclusive_cutoff));
    bindInteger(database, statement.get(), 2, limitToInteger(limit));
    requireDone(database, statement.get(), "Could not erase expired transmission audit events");
    return static_cast<std::size_t>(sqlite3_changes(database));
}

auto SqliteControlPlaneRepository::droppedAuditEventCount() const noexcept -> std::uint64_t
{
    return implementation_->dropped_audit_events.load(std::memory_order_relaxed);
}

auto SqliteControlPlaneRepository::schemaVersion() const -> std::uint32_t
{
    std::scoped_lock lock{implementation_->mutex};
    return readSchemaVersion(implementation_->database.get());
}
} // namespace hvc::persistence
