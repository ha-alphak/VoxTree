#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <hvc/persistence/sqlite_session_repository.hpp>
#include <stdexcept>
#include <string>

namespace
{
using namespace hvc;

class TemporaryDatabase final
{
  public:
    TemporaryDatabase()
    {
        const auto suffix =
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        path_ = std::filesystem::temp_directory_path() / ("hvc-persistence-" + suffix + ".db");
        std::filesystem::remove(path_);
    }

    ~TemporaryDatabase()
    {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
        std::filesystem::remove(path_.string() + "-shm", ignored);
        std::filesystem::remove(path_.string() + "-wal", ignored);
    }

    [[nodiscard]] auto path() const -> const std::filesystem::path&
    {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

auto initialMigrationIsAppliedOnce() -> bool
{
    TemporaryDatabase database;
    {
        const persistence::SqliteSessionRepository repository{database.path()};
        if (repository.schemaVersion() !=
            persistence::SqliteSessionRepository::latest_schema_version)
        {
            return false;
        }
    }

    const persistence::SqliteSessionRepository reopened{database.path()};
    return reopened.schemaVersion() == persistence::SqliteSessionRepository::latest_schema_version;
}

auto sessionSurvivesRepositoryRestart() -> bool
{
    TemporaryDatabase database;
    const auto expiration = application::TimePoint{std::chrono::milliseconds{1'783'000'123'456LL}};
    {
        persistence::SqliteSessionRepository repository{database.path()};
        repository.upsert({domain::SessionId{"session-1"}, domain::PlayerId{"player-1"},
                           domain::DeviceId{"device-1"}, expiration});
    }

    const persistence::SqliteSessionRepository reopened{database.path()};
    const auto session = reopened.find(domain::SessionId{"session-1"});
    return session && session->session_id == domain::SessionId{"session-1"} &&
           session->player_id == domain::PlayerId{"player-1"} &&
           session->device_id == domain::DeviceId{"device-1"} && session->expires_at == expiration;
}

auto sessionCanBeReplacedAndErased() -> bool
{
    TemporaryDatabase database;
    persistence::SqliteSessionRepository repository{database.path()};
    repository.upsert({domain::SessionId{"session-1"}, domain::PlayerId{"player-old"},
                       domain::DeviceId{"device-old"}, application::TimePoint{}});
    repository.upsert({domain::SessionId{"session-1"}, domain::PlayerId{"player-new"},
                       domain::DeviceId{"device-new"},
                       application::TimePoint{std::chrono::seconds{42}}});

    const auto updated = repository.find(domain::SessionId{"session-1"});
    if (!updated || updated->player_id != domain::PlayerId{"player-new"} ||
        updated->device_id != domain::DeviceId{"device-new"} ||
        updated->expires_at != application::TimePoint{std::chrono::seconds{42}})
    {
        return false;
    }

    return repository.erase(domain::SessionId{"session-1"}) &&
           !repository.erase(domain::SessionId{"session-1"}) &&
           !repository.find(domain::SessionId{"session-1"}).has_value();
}

auto newerSchemaIsRejected() -> bool
{
    TemporaryDatabase database;
    {
        const persistence::SqliteSessionRepository repository{database.path()};
    }

    // SQLite stores PRAGMA user_version as a four-byte big-endian value at header offset 60.
    std::fstream file{database.path(), std::ios::binary | std::ios::in | std::ios::out};
    const char unsupported_version[]{0, 0, 0, 2};
    file.seekp(60);
    file.write(unsupported_version, sizeof(unsupported_version));
    file.close();

    try
    {
        const persistence::SqliteSessionRepository repository{database.path()};
    }
    catch (const persistence::PersistenceError&)
    {
        return true;
    }
    return false;
}
} // namespace

auto main() -> int
{
    try
    {
        return initialMigrationIsAppliedOnce() && sessionSurvivesRepositoryRestart() &&
                       sessionCanBeReplacedAndErased() && newerSchemaIsRejected()
                   ? 0
                   : 1;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "SQLite persistence test failed: %s\n", error.what());
        return 1;
    }
}
