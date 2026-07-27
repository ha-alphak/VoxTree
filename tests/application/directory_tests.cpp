#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <exception>
#include <hvc/application/directory.hpp>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
namespace application = hvc::application;
namespace domain = hvc::domain;

[[nodiscard]] auto makeContext(std::uint64_t version = 42, std::string alpha_name = "Alpha")
    -> application::AuthoritativeMembershipContext
{
    std::vector<domain::ScopeDefinition> scopes{
        {domain::VoiceScope::team, "Team", 1, 5},
        {domain::VoiceScope::specialization, "Specialization", 2, 4},
        {domain::VoiceScope::group, "Group", 3, 2}};
    std::vector<domain::Group> groups{{domain::GroupId{"alpha"}, std::move(alpha_name)},
                                      {domain::GroupId{"beta"}, "Beta"}};
    std::vector<domain::Specialization> specializations{
        {domain::SpecializationId{"alpha-red"}, domain::GroupId{"alpha"}, "Alpha Red"},
        {domain::SpecializationId{"beta-blue"}, domain::GroupId{"beta"}, "Beta Blue"}};
    std::vector<domain::Team> teams{
        {domain::TeamId{"alpha-one"}, domain::SpecializationId{"alpha-red"}, "Alpha One"},
        {domain::TeamId{"beta-one"}, domain::SpecializationId{"beta-blue"}, "Beta One"}};
    std::vector<domain::VoiceMembership> memberships{
        {domain::PlayerId{"alpha-actor"},
         domain::GroupId{"alpha"},
         domain::SpecializationId{"alpha-red"},
         domain::TeamId{"alpha-one"},
         {domain::RoleId{"public-leader"}, domain::RoleId{"private-admin"}}},
        {domain::PlayerId{"alpha-peer"},
         domain::GroupId{"alpha"},
         domain::SpecializationId{"alpha-red"},
         domain::TeamId{"alpha-one"},
         {domain::RoleId{"private-admin"}}},
        {domain::PlayerId{"beta-actor"},
         domain::GroupId{"beta"},
         domain::SpecializationId{"beta-blue"},
         domain::TeamId{"beta-one"},
         {domain::RoleId{"public-leader"}}}};

    return {std::make_shared<const domain::MembershipSnapshot>(
                version,
                domain::Hierarchy{domain::HierarchyId{"main"}, std::move(scopes), std::move(groups),
                                  std::move(specializations), std::move(teams)},
                std::move(memberships)),
            std::make_shared<const domain::RolePolicy>(std::vector<domain::RolePermissions>{})};
}

class MembershipProvider final : public application::IAuthoritativeMembershipProvider
{
  public:
    application::AuthoritativeMembershipContext context{makeContext()};

    [[nodiscard]] auto currentFor(const domain::PlayerId& player_id) const
        -> std::optional<application::AuthoritativeMembershipContext> override
    {
        return context.snapshot->find(player_id) == nullptr
                   ? std::nullopt
                   : std::optional<application::AuthoritativeMembershipContext>{context};
    }
};

class PresenceProvider final : public application::ITransportPresenceProvider
{
  public:
    [[nodiscard]] auto connectedScopeCount(const domain::PlayerId& player_id) const
        -> std::size_t override
    {
        const auto count = connected_scopes.find(player_id);
        return count == connected_scopes.end() ? 0 : count->second;
    }

    std::map<domain::PlayerId, std::size_t> connected_scopes;
};

struct Fixture final
{
    Fixture() : directory{memberships, presence}
    {
        static_cast<void>(directory.upsertProfile({domain::PlayerId{"alpha-actor"}, "Alex", 42}));
        static_cast<void>(directory.upsertProfile({domain::PlayerId{"alpha-peer"}, "Pat", 42}));
        static_cast<void>(directory.upsertProfile({domain::PlayerId{"beta-actor"}, "Blake", 42}));
        static_cast<void>(directory.replacePublicRoles(
            42, {{domain::RoleId{"public-leader"}, "Team leader", 42}}));
        presence.connected_scopes.emplace(domain::PlayerId{"alpha-actor"}, 3);
        presence.connected_scopes.emplace(domain::PlayerId{"alpha-peer"}, 0);
        presence.connected_scopes.emplace(domain::PlayerId{"beta-actor"}, 2);
    }

    MembershipProvider memberships;
    PresenceProvider presence;
    application::DirectoryApplicationService directory;
};

[[nodiscard]] auto limitsDirectoryToTheActorsGroupAndPublicFields() -> bool
{
    Fixture fixture;
    const auto result = fixture.directory.directoryFor(domain::PlayerId{"alpha-actor"});
    if (!result.successful())
    {
        return false;
    }
    const auto& directory = *result.snapshot;
    return directory.group_id == domain::GroupId{"alpha"} && directory.nodes.size() == 3 &&
           directory.participants.size() == 2 && directory.public_roles.size() == 1 &&
           directory.participants.front().player_id == domain::PlayerId{"alpha-actor"} &&
           directory.participants.front().display_name == "Alex" &&
           directory.participants.front().public_role_ids ==
               std::vector<domain::RoleId>{domain::RoleId{"public-leader"}} &&
           directory.nodes.front().node_type == application::DirectoryNodeType::group &&
           !directory.nodes.front().parent_node_id &&
           std::ranges::none_of(directory.participants, [](const auto& participant) {
               return participant.player_id == domain::PlayerId{"beta-actor"};
           });
}

[[nodiscard]] auto versionsOnlyVisibleDirectoryChanges() -> bool
{
    Fixture fixture;
    const auto first = fixture.directory.directoryFor(domain::PlayerId{"alpha-actor"});
    if (!first.successful())
    {
        return false;
    }

    static_cast<void>(
        fixture.directory.upsertProfile({domain::PlayerId{"beta-actor"}, "Beta Changed", 43}));
    const auto after_foreign_change =
        fixture.directory.directoryFor(domain::PlayerId{"alpha-actor"});
    static_cast<void>(
        fixture.directory.upsertProfile({domain::PlayerId{"alpha-peer"}, "Pat Changed", 43}));
    const auto after_visible_change =
        fixture.directory.directoryFor(domain::PlayerId{"alpha-actor"});
    const auto stale_profile =
        fixture.directory.upsertProfile({domain::PlayerId{"alpha-peer"}, "Stale", 42});

    fixture.memberships.context = makeContext(44, "Alpha Renamed");
    const auto after_hierarchy_change =
        fixture.directory.directoryFor(domain::PlayerId{"alpha-actor"});

    return after_foreign_change.successful() && after_visible_change.successful() &&
           after_hierarchy_change.successful() &&
           after_foreign_change.snapshot->version == first.snapshot->version &&
           after_visible_change.snapshot->version > first.snapshot->version && !stale_profile &&
           after_hierarchy_change.snapshot->version > after_visible_change.snapshot->version;
}

[[nodiscard]] auto aggregatesPresenceAndReturnsLatestDeltas() -> bool
{
    Fixture fixture;
    const auto observed_at = application::TimePoint{std::chrono::seconds{1'000}};
    const auto initial =
        fixture.directory.presenceFor(domain::PlayerId{"alpha-actor"}, std::nullopt, observed_at);
    if (!initial.successful() || initial.snapshot->entries.size() != 2 ||
        initial.snapshot->entries.front().state != application::DirectoryPresenceState::online)
    {
        return false;
    }

    const auto initial_version = initial.snapshot->version;
    fixture.presence.connected_scopes.insert_or_assign(domain::PlayerId{"alpha-actor"}, 0);
    fixture.presence.connected_scopes.insert_or_assign(domain::PlayerId{"alpha-peer"}, 3);
    const auto delta = fixture.directory.presenceFor(domain::PlayerId{"alpha-actor"},
                                                     initial_version, observed_at);
    if (!delta.successful() || delta.snapshot->version <= initial_version ||
        delta.snapshot->entries.size() != 2 ||
        delta.snapshot->mode != application::DirectoryPresenceMode::delta)
    {
        return false;
    }

    const auto changed_version = delta.snapshot->version;
    fixture.presence.connected_scopes.insert_or_assign(domain::PlayerId{"alpha-peer"}, 2);
    fixture.presence.connected_scopes.insert_or_assign(domain::PlayerId{"beta-actor"}, 0);
    const auto unchanged = fixture.directory.presenceFor(domain::PlayerId{"alpha-actor"},
                                                         changed_version, observed_at);
    const auto invalid =
        fixture.directory.presenceFor(domain::PlayerId{"alpha-actor"}, 0, observed_at);
    const auto future = fixture.directory.presenceFor(domain::PlayerId{"alpha-actor"},
                                                      changed_version + 1, observed_at);
    return unchanged.successful() && unchanged.snapshot->version == changed_version &&
           unchanged.snapshot->entries.empty() && !invalid.successful() &&
           invalid.error == application::DirectoryPresenceReadError::snapshot_required &&
           !future.successful() &&
           future.error == application::DirectoryPresenceReadError::snapshot_required;
}

[[nodiscard]] auto expiresOldPresenceDeltaBases() -> bool
{
    Fixture fixture;
    const auto observed_at = application::TimePoint{std::chrono::seconds{1'000}};
    const auto initial =
        fixture.directory.presenceFor(domain::PlayerId{"alpha-actor"}, std::nullopt, observed_at);
    if (!initial.successful())
    {
        return false;
    }
    for (std::size_t index = 0; index < 130; ++index)
    {
        fixture.presence.connected_scopes.insert_or_assign(domain::PlayerId{"alpha-peer"},
                                                           (index % 2) + 1);
        if (index % 2 == 0)
        {
            fixture.presence.connected_scopes.insert_or_assign(domain::PlayerId{"alpha-peer"}, 0);
        }
        const auto update = fixture.directory.presenceFor(domain::PlayerId{"alpha-actor"},
                                                          std::nullopt, observed_at);
        if (!update.successful())
        {
            return false;
        }
    }
    const auto expired = fixture.directory.presenceFor(domain::PlayerId{"alpha-actor"},
                                                       initial.snapshot->version, observed_at);
    return !expired.successful() &&
           expired.error == application::DirectoryPresenceReadError::snapshot_required;
}

[[nodiscard]] auto rejectsUnavailableAndOversizedDirectories() -> bool
{
    Fixture fixture;
    const auto missing = fixture.directory.directoryFor(domain::PlayerId{"missing"});

    std::vector<domain::ScopeDefinition> scopes{
        {domain::VoiceScope::team, "Team", 1, 5},
        {domain::VoiceScope::specialization, "Specialization", 2, 4},
        {domain::VoiceScope::group, "Group", 3, 2}};
    std::vector<domain::VoiceMembership> memberships;
    memberships.reserve(201);
    for (std::size_t index = 0; index < 201; ++index)
    {
        memberships.emplace_back(domain::PlayerId{"player-" + std::to_string(index)},
                                 domain::GroupId{"alpha"}, domain::SpecializationId{"alpha-red"},
                                 domain::TeamId{"alpha-one"}, std::vector<domain::RoleId>{});
    }
    fixture.memberships.context = {
        std::make_shared<const domain::MembershipSnapshot>(
            50,
            domain::Hierarchy{
                domain::HierarchyId{"large"}, std::move(scopes),
                std::vector<domain::Group>{{domain::GroupId{"alpha"}, "Alpha"}},
                std::vector<domain::Specialization>{
                    {domain::SpecializationId{"alpha-red"}, domain::GroupId{"alpha"}, "Alpha Red"}},
                std::vector<domain::Team>{{domain::TeamId{"alpha-one"},
                                           domain::SpecializationId{"alpha-red"}, "Alpha One"}}},
            std::move(memberships)),
        std::make_shared<const domain::RolePolicy>(std::vector<domain::RolePermissions>{})};
    const auto oversized = fixture.directory.directoryFor(domain::PlayerId{"player-0"});

    return !missing.successful() &&
           missing.error == application::DirectoryReadError::directory_unavailable &&
           !oversized.successful() &&
           oversized.error == application::DirectoryReadError::directory_limit_exceeded;
}
} // namespace

auto main() noexcept -> int
{
    try
    {
        using Check = std::pair<const char*, bool (*)()>;
        const std::array checks{
            Check{"group and field privacy", &limitsDirectoryToTheActorsGroupAndPublicFields},
            Check{"visible directory versions", &versionsOnlyVisibleDirectoryChanges},
            Check{"presence aggregation and deltas", &aggregatesPresenceAndReturnsLatestDeltas},
            Check{"presence retention", &expiresOldPresenceDeltaBases},
            Check{"directory availability and limit", &rejectsUnavailableAndOversizedDirectories}};
        for (const auto& [name, check] : checks)
        {
            if (!check())
            {
                std::fprintf(stderr, "directory application test failed: %s\n", name);
                return 1;
            }
        }
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "unexpected exception: %s\n", error.what());
        return 1;
    }

    std::puts("directory application tests passed");
    return 0;
}
