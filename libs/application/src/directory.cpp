#include <algorithm>
#include <hvc/application/directory.hpp>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace hvc::application
{
namespace
{
constexpr std::size_t maximum_directory_participants{200};
constexpr std::size_t maximum_presence_revisions{128};

void appendFingerprint(std::string& fingerprint, std::string_view value)
{
    fingerprint += std::to_string(value.size());
    fingerprint.push_back(':');
    fingerprint.append(value);
    fingerprint.push_back(';');
}

void appendFingerprint(std::string& fingerprint, std::uint64_t value)
{
    appendFingerprint(fingerprint, std::to_string(value));
}

[[nodiscard]] auto nodeTypeFingerprint(DirectoryNodeType type) noexcept -> std::string_view
{
    switch (type)
    {
    case DirectoryNodeType::group:
        return "group";
    case DirectoryNodeType::specialization:
        return "specialization";
    case DirectoryNodeType::team:
        return "team";
    }
    return "unknown";
}

[[nodiscard]] auto failedDirectory(DirectoryReadError error) -> DirectoryReadResult
{
    return DirectoryReadResult{std::nullopt, error};
}

[[nodiscard]] auto failedPresence(DirectoryPresenceReadError error) -> DirectoryPresenceReadResult
{
    return DirectoryPresenceReadResult{std::nullopt, error};
}
} // namespace

auto DirectoryReadResult::successful() const noexcept -> bool
{
    return snapshot.has_value() && !error.has_value();
}

auto DirectoryPresenceReadResult::successful() const noexcept -> bool
{
    return snapshot.has_value() && !error.has_value();
}

DirectoryApplicationService::DirectoryApplicationService(
    const IAuthoritativeMembershipProvider& memberships,
    const ITransportPresenceProvider& transport_presence) noexcept
    : memberships_(memberships), transport_presence_(transport_presence)
{
}

auto DirectoryApplicationService::upsertProfile(DirectoryProfile profile) -> bool
{
    if (profile.display_name.empty() || profile.version == 0)
    {
        throw std::invalid_argument{"A directory profile requires a name and positive version."};
    }

    std::scoped_lock lock{mutex_};
    const auto existing = profiles_.find(profile.player_id);
    if (existing != profiles_.end() && existing->second.version >= profile.version)
    {
        return false;
    }
    profiles_.insert_or_assign(profile.player_id, std::move(profile));
    return true;
}

auto DirectoryApplicationService::replacePublicRoles(std::uint64_t catalog_version,
                                                     std::vector<PublicDirectoryRole> roles) -> bool
{
    if (catalog_version == 0)
    {
        throw std::invalid_argument{"A public role catalog requires a positive version."};
    }

    std::map<domain::RoleId, PublicDirectoryRole> replacement;
    for (auto& role : roles)
    {
        if (role.display_name.empty() || role.version == 0 || role.version > catalog_version)
        {
            throw std::invalid_argument{"A public role has an invalid name or version."};
        }
        const auto [position, inserted] = replacement.emplace(role.role_id, std::move(role));
        static_cast<void>(position);
        if (!inserted)
        {
            throw std::invalid_argument{"The public role catalog contains a duplicate role."};
        }
    }

    std::scoped_lock lock{mutex_};
    if (catalog_version <= public_role_catalog_version_)
    {
        return false;
    }
    public_roles_ = std::move(replacement);
    public_role_catalog_version_ = catalog_version;
    return true;
}

auto DirectoryApplicationService::currentContextFor(const domain::PlayerId& actor) const
    -> std::optional<AuthoritativeMembershipContext>
{
    auto context = memberships_.currentFor(actor);
    if (!context)
    {
        return std::nullopt;
    }
    const auto* membership = context->snapshot->find(actor);
    if (membership == nullptr)
    {
        return std::nullopt;
    }
    const auto* group = context->snapshot->hierarchy().findGroup(membership->group_id);
    if (group == nullptr || !group->active)
    {
        return std::nullopt;
    }
    return context;
}

auto DirectoryApplicationService::makeDirectory(
    const AuthoritativeMembershipContext& context,
    const domain::VoiceMembership& actor_membership) const -> DirectoryReadResult
{
    const auto& snapshot = *context.snapshot;
    const auto& hierarchy = snapshot.hierarchy();
    const auto* visible_group = hierarchy.findGroup(actor_membership.group_id);
    if (visible_group == nullptr || !visible_group->active)
    {
        return failedDirectory(DirectoryReadError::directory_unavailable);
    }

    std::vector<const domain::VoiceMembership*> visible_memberships;
    for (const auto& membership : snapshot.memberships())
    {
        if (membership.group_id == actor_membership.group_id)
        {
            visible_memberships.push_back(&membership);
        }
    }
    if (visible_memberships.size() > maximum_directory_participants)
    {
        return failedDirectory(DirectoryReadError::directory_limit_exceeded);
    }
    std::ranges::sort(visible_memberships, {},
                      [](const auto* membership) { return membership->player_id.value(); });

    DirectorySnapshot result{snapshot.version(), actor_membership.group_id, {}, {}, {}};
    result.nodes.push_back(DirectoryNode{std::string{visible_group->id.value()},
                                         DirectoryNodeType::group, std::nullopt,
                                         visible_group->display_name, 0});

    std::map<domain::SpecializationId, std::uint32_t> next_team_sort_index;
    std::uint32_t specialization_sort_index{};
    for (const auto& specialization : hierarchy.specializations())
    {
        if (specialization.group_id != actor_membership.group_id)
        {
            continue;
        }
        result.nodes.push_back(
            DirectoryNode{std::string{specialization.id.value()}, DirectoryNodeType::specialization,
                          std::string{visible_group->id.value()}, specialization.display_name,
                          specialization_sort_index++});
        next_team_sort_index.emplace(specialization.id, 0);
    }
    for (const auto& team : hierarchy.teams())
    {
        const auto next_index = next_team_sort_index.find(team.specialization_id);
        if (next_index == next_team_sort_index.end())
        {
            continue;
        }
        result.nodes.push_back(DirectoryNode{std::string{team.id.value()}, DirectoryNodeType::team,
                                             std::string{team.specialization_id.value()},
                                             team.display_name, next_index->second++});
    }

    std::set<domain::RoleId> referenced_public_roles;
    std::uint64_t source_version = snapshot.version();
    for (const auto* membership : visible_memberships)
    {
        std::string display_name{membership->player_id.value()};
        const auto profile = profiles_.find(membership->player_id);
        if (profile != profiles_.end())
        {
            display_name = profile->second.display_name;
            source_version = std::max(source_version, profile->second.version);
        }

        std::vector<domain::RoleId> public_role_ids;
        for (const auto& role_id : membership->role_ids)
        {
            const auto role = public_roles_.find(role_id);
            if (role != public_roles_.end())
            {
                public_role_ids.push_back(role_id);
                referenced_public_roles.insert(role_id);
                source_version = std::max(source_version, role->second.version);
            }
        }
        std::ranges::sort(public_role_ids, {}, [](const auto& role_id) { return role_id.value(); });
        const auto duplicate_roles = std::ranges::unique(public_role_ids);
        public_role_ids.erase(duplicate_roles.begin(), duplicate_roles.end());
        result.participants.push_back(
            DirectoryParticipant{membership->player_id, std::move(display_name),
                                 membership->team_id, std::move(public_role_ids)});
    }

    for (const auto& role_id : referenced_public_roles)
    {
        result.public_roles.push_back(public_roles_.at(role_id));
    }

    std::string fingerprint;
    for (const auto& node : result.nodes)
    {
        appendFingerprint(fingerprint, node.node_id);
        appendFingerprint(fingerprint, nodeTypeFingerprint(node.node_type));
        appendFingerprint(fingerprint, node.parent_node_id.value_or(""));
        appendFingerprint(fingerprint, node.display_name);
        appendFingerprint(fingerprint, node.sort_index);
    }
    for (const auto& role : result.public_roles)
    {
        appendFingerprint(fingerprint, role.role_id.value());
        appendFingerprint(fingerprint, role.display_name);
    }
    for (const auto& participant : result.participants)
    {
        appendFingerprint(fingerprint, participant.player_id.value());
        appendFingerprint(fingerprint, participant.display_name);
        appendFingerprint(fingerprint, participant.primary_team_id.value());
        for (const auto& role_id : participant.public_role_ids)
        {
            appendFingerprint(fingerprint, role_id.value());
        }
    }

    auto version = directory_versions_.find(actor_membership.group_id);
    if (version == directory_versions_.end())
    {
        version = directory_versions_
                      .emplace(actor_membership.group_id,
                               VersionedDirectory{std::max<std::uint64_t>(1, source_version),
                                                  std::move(fingerprint)})
                      .first;
    }
    else if (version->second.fingerprint != fingerprint)
    {
        if (version->second.version == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error{"The directory version space is exhausted."};
        }
        version->second.version = std::max(version->second.version + 1, source_version);
        version->second.fingerprint = std::move(fingerprint);
    }
    result.version = version->second.version;
    return DirectoryReadResult{std::move(result), std::nullopt};
}

auto DirectoryApplicationService::directoryFor(const domain::PlayerId& actor) const
    -> DirectoryReadResult
{
    std::scoped_lock lock{mutex_};
    const auto context = currentContextFor(actor);
    if (!context)
    {
        return failedDirectory(DirectoryReadError::directory_unavailable);
    }
    const auto* actor_membership = context->snapshot->find(actor);
    if (actor_membership == nullptr)
    {
        return failedDirectory(DirectoryReadError::directory_unavailable);
    }
    return makeDirectory(*context, *actor_membership);
}

auto DirectoryApplicationService::presenceFor(const domain::PlayerId& actor,
                                              std::optional<std::uint64_t> after_version,
                                              TimePoint observed_at) const
    -> DirectoryPresenceReadResult
{
    std::scoped_lock lock{mutex_};
    const auto context = currentContextFor(actor);
    if (!context)
    {
        return failedPresence(DirectoryPresenceReadError::directory_unavailable);
    }
    const auto* actor_membership = context->snapshot->find(actor);
    if (actor_membership == nullptr)
    {
        return failedPresence(DirectoryPresenceReadError::directory_unavailable);
    }

    std::map<domain::PlayerId, DirectoryPresenceState> current_states;
    for (const auto& membership : context->snapshot->memberships())
    {
        if (membership.group_id != actor_membership->group_id)
        {
            continue;
        }
        if (current_states.size() == maximum_directory_participants)
        {
            return failedPresence(DirectoryPresenceReadError::directory_unavailable);
        }
        const auto state = transport_presence_.connectedScopeCount(membership.player_id) == 0
                               ? DirectoryPresenceState::offline
                               : DirectoryPresenceState::online;
        current_states.emplace(membership.player_id, state);
    }

    auto& versioned = presence_versions_[actor_membership->group_id];
    if (!versioned.initialized)
    {
        versioned.version = 1;
        versioned.initialized = true;
        versioned.states = current_states;
    }
    else if (versioned.states != current_states)
    {
        if (versioned.version == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error{"The presence version space is exhausted."};
        }
        ++versioned.version;
        PresenceRevision revision{versioned.version, {}};
        for (const auto& [player_id, state] : current_states)
        {
            const auto previous = versioned.states.find(player_id);
            if (previous == versioned.states.end() || previous->second != state)
            {
                revision.entries.push_back(DirectoryPresenceEntry{player_id, state});
            }
        }
        versioned.states = current_states;
        versioned.revisions.push_back(std::move(revision));
        if (versioned.revisions.size() > maximum_presence_revisions)
        {
            versioned.revisions.erase(versioned.revisions.begin());
        }
    }

    if (after_version && (*after_version == 0 || *after_version > versioned.version ||
                          (!versioned.revisions.empty() &&
                           *after_version < (versioned.revisions.front().version - 1))))
    {
        return failedPresence(DirectoryPresenceReadError::snapshot_required);
    }

    DirectoryPresenceSnapshot result{versioned.version,
                                     after_version ? DirectoryPresenceMode::delta
                                                   : DirectoryPresenceMode::snapshot,
                                     observed_at,
                                     {}};
    if (!after_version)
    {
        for (const auto& [player_id, state] : versioned.states)
        {
            result.entries.push_back(DirectoryPresenceEntry{player_id, state});
        }
    }
    else
    {
        std::map<domain::PlayerId, DirectoryPresenceState> latest_changes;
        for (const auto& revision : versioned.revisions)
        {
            if (revision.version <= *after_version)
            {
                continue;
            }
            for (const auto& entry : revision.entries)
            {
                if (versioned.states.contains(entry.player_id))
                {
                    latest_changes.insert_or_assign(entry.player_id, entry.state);
                }
            }
        }
        for (const auto& [player_id, state] : latest_changes)
        {
            result.entries.push_back(DirectoryPresenceEntry{player_id, state});
        }
    }
    return DirectoryPresenceReadResult{std::move(result), std::nullopt};
}
} // namespace hvc::application
