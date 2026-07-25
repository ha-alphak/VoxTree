#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <hvc/application/in_memory_control_plane.hpp>
#include <hvc/persistence/sqlite_control_plane_repository.hpp>
#include <stdexcept>
#include <string>

namespace
{
using namespace hvc;

auto makeMembershipContext(std::uint64_t version) -> application::AuthoritativeMembershipContext
{
    domain::Hierarchy hierarchy{
        domain::HierarchyId{"hierarchy-1"},
        {{domain::VoiceScope::team, "Team", 1, 5},
         {domain::VoiceScope::specialization, "Specialization", 2, std::nullopt},
         {domain::VoiceScope::group, "Group", 3, 2}},
        {{domain::GroupId{"group-1"}, "Group One"}},
        {{domain::SpecializationId{"specialization-1"}, domain::GroupId{"group-1"},
          "Specialization One"}},
        {{domain::TeamId{"team-1"}, domain::SpecializationId{"specialization-1"}, "Team One"}}};

    domain::VoiceMembership owner{domain::PlayerId{"player-1"},
                                  domain::GroupId{"group-1"},
                                  domain::SpecializationId{"specialization-1"},
                                  domain::TeamId{"team-1"},
                                  {domain::RoleId{"leader"}}};
    owner.can_receive_voice = false;
    owner.voice_ban_status = domain::VoiceBanStatus::temporary;

    auto snapshot = std::make_shared<const domain::MembershipSnapshot>(
        version, std::move(hierarchy),
        std::vector<domain::VoiceMembership>{std::move(owner),
                                             {domain::PlayerId{"player-2"},
                                              domain::GroupId{"group-1"},
                                              domain::SpecializationId{"specialization-1"},
                                              domain::TeamId{"team-1"},
                                              {domain::RoleId{"listener"}}}});
    auto role_policy =
        std::make_shared<const domain::RolePolicy>(std::vector<domain::RolePermissions>{
            {domain::RoleId{"leader"},
             {domain::VoiceScope::team, domain::VoiceScope::group},
             {domain::VoiceScope::team}},
            {domain::RoleId{"listener"}, {}, {domain::VoiceScope::group}}});
    return {std::move(snapshot), std::move(role_policy)};
}

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
        const persistence::SqliteControlPlaneRepository repository{database.path()};
        if (repository.schemaVersion() !=
            persistence::SqliteControlPlaneRepository::latest_schema_version)
        {
            return false;
        }
    }

    const persistence::SqliteControlPlaneRepository reopened{database.path()};
    return reopened.schemaVersion() ==
           persistence::SqliteControlPlaneRepository::latest_schema_version;
}

auto sessionSurvivesRepositoryRestart() -> bool
{
    TemporaryDatabase database;
    const auto expiration = application::TimePoint{std::chrono::milliseconds{1'783'000'123'456LL}};
    {
        persistence::SqliteControlPlaneRepository repository{database.path()};
        repository.upsert({domain::SessionId{"session-1"}, domain::PlayerId{"player-1"},
                           domain::DeviceId{"device-1"}, expiration});
    }

    const persistence::SqliteControlPlaneRepository reopened{database.path()};
    const auto session = reopened.find(domain::SessionId{"session-1"});
    return session && session->session_id == domain::SessionId{"session-1"} &&
           session->player_id == domain::PlayerId{"player-1"} &&
           session->device_id == domain::DeviceId{"device-1"} && session->expires_at == expiration;
}

auto sessionCanBeReplacedAndErased() -> bool
{
    TemporaryDatabase database;
    persistence::SqliteControlPlaneRepository repository{database.path()};
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

auto membershipContextSurvivesRepositoryRestart() -> bool
{
    TemporaryDatabase database;
    {
        persistence::SqliteControlPlaneRepository repository{database.path()};
        if (repository.upsertIfNewer(domain::PlayerId{"player-1"}, makeMembershipContext(42)))
        {
            return false;
        }
    }

    const persistence::SqliteControlPlaneRepository reopened{database.path()};
    const auto context = reopened.currentFor(domain::PlayerId{"player-1"});
    if (!context || context->snapshot->version() != 42 ||
        context->snapshot->hierarchy().id() != domain::HierarchyId{"hierarchy-1"} ||
        context->snapshot->hierarchy().scopes().size() != 3 ||
        context->snapshot->memberships().size() != 2)
    {
        return false;
    }

    const auto* owner = context->snapshot->find(domain::PlayerId{"player-1"});
    const std::vector leader_role{domain::RoleId{"leader"}};
    return owner != nullptr && !owner->can_receive_voice &&
           owner->voice_ban_status == domain::VoiceBanStatus::temporary &&
           context->role_policy->roles().size() == 2 &&
           context->role_policy->canTransmit(leader_role, domain::VoiceScope::group) &&
           !context->role_policy->canReceive(leader_role, domain::VoiceScope::group) &&
           !reopened.currentFor(domain::PlayerId{"unknown"}).has_value();
}

auto membershipVersionsOnlyMoveForward() -> bool
{
    TemporaryDatabase database;
    persistence::SqliteControlPlaneRepository repository{database.path()};
    if (repository.upsertIfNewer(domain::PlayerId{"player-1"}, makeMembershipContext(43)))
    {
        return false;
    }

    const auto stale =
        repository.upsertIfNewer(domain::PlayerId{"player-1"}, makeMembershipContext(42));
    const auto retained = repository.currentFor(domain::PlayerId{"player-1"});
    return stale == application::AuthoritativeMembershipWriteError::version_not_newer && retained &&
           retained->snapshot->version() == 43 && repository.erase(domain::PlayerId{"player-1"}) &&
           !repository.erase(domain::PlayerId{"player-1"}) &&
           !repository.currentFor(domain::PlayerId{"player-1"});
}

auto persistedUpdateInterruptsTransmissionAtomically() -> bool
{
    TemporaryDatabase database;
    persistence::SqliteControlPlaneRepository repository{database.path()};
    application::InMemoryControlPlaneStore store{repository};
    const auto now = application::TimePoint{std::chrono::seconds{100}};
    store.upsertSession({domain::SessionId{"session-1"}, domain::PlayerId{"player-1"},
                         domain::DeviceId{"device-1"}, now + std::chrono::hours{1}});
    if (!store
             .updateMembership(domain::PlayerId{"player-1"}, makeMembershipContext(42), now,
                               domain::CorrelationId{"initial"},
                               application::AuthoritativeContextChange::membership_changed)
             .successful())
    {
        return false;
    }

    application::AuthorizedTransmission authorization{
        domain::TransmissionId{"transmission-1"},
        domain::ClientTransmissionId{"client-transmission-1"},
        domain::PlayerId{"player-1"},
        domain::VoiceScope::group,
        42,
        {domain::PlayerId{"player-2"}},
        domain::CorrelationId{"start"}};
    if (!store
             .activate(std::move(authorization), domain::SessionId{"session-1"},
                       domain::DeviceId{"device-1"}, now)
             .active())
    {
        return false;
    }

    const auto update = store.updateMembership(
        domain::PlayerId{"player-1"}, makeMembershipContext(43), now + std::chrono::seconds{1},
        domain::CorrelationId{"permission-change"},
        application::AuthoritativeContextChange::permissions_changed);
    const auto persisted = repository.currentFor(domain::PlayerId{"player-1"});
    return update.successful() && update.interrupted.size() == 1 &&
           update.interrupted.front().stop_reason ==
               domain::TransmissionStopReason::permission_revoked &&
           store.activeCount() == 0 && persisted && persisted->snapshot->version() == 43;
}

auto newerSchemaIsRejected() -> bool
{
    TemporaryDatabase database;
    {
        const persistence::SqliteControlPlaneRepository repository{database.path()};
    }

    // SQLite stores PRAGMA user_version as a four-byte big-endian value at header offset 60.
    std::fstream file{database.path(), std::ios::binary | std::ios::in | std::ios::out};
    const char unsupported_version[]{0, 0, 0, 3};
    file.seekp(60);
    file.write(unsupported_version, sizeof(unsupported_version));
    file.close();

    try
    {
        const persistence::SqliteControlPlaneRepository repository{database.path()};
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
                       sessionCanBeReplacedAndErased() &&
                       membershipContextSurvivesRepositoryRestart() &&
                       membershipVersionsOnlyMoveForward() &&
                       persistedUpdateInterruptsTransmissionAtomically() && newerSchemaIsRejected()
                   ? 0
                   : 1;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "SQLite persistence test failed: %s\n", error.what());
        return 1;
    }
}
