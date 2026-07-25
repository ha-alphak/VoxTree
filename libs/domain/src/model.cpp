#include <algorithm>
#include <hvc/domain/model.hpp>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace hvc::domain
{
namespace
{
template <typename Item, typename IdAccessor>
void requireUniqueIds(const std::vector<Item>& items, IdAccessor id_accessor, std::string_view type)
{
    for (auto current = items.begin(); current != items.end(); ++current)
    {
        const auto duplicate =
            std::find_if(std::next(current), items.end(), [&](const Item& candidate) {
                return id_accessor(*current) == id_accessor(candidate);
            });
        if (duplicate != items.end())
        {
            throw std::invalid_argument{std::string{"Duplicate "} + std::string{type} + " ID."};
        }
    }
}

auto containsScope(std::span<const VoiceScope> scopes, VoiceScope scope) noexcept -> bool
{
    return std::ranges::find(scopes, scope) != scopes.end();
}
} // namespace

Hierarchy::Hierarchy(HierarchyId hierarchy_id, std::vector<ScopeDefinition> scopes,
                     std::vector<Group> groups, std::vector<Specialization> specializations,
                     std::vector<Team> teams)
    : id_(std::move(hierarchy_id)), scopes_(std::move(scopes)), groups_(std::move(groups)),
      specializations_(std::move(specializations)), teams_(std::move(teams))
{
    requireUniqueIds(
        groups_, [](const Group& group) -> const GroupId& { return group.id; }, "group");
    requireUniqueIds(
        specializations_,
        [](const Specialization& specialization) -> const SpecializationId& {
            return specialization.id;
        },
        "specialization");
    requireUniqueIds(teams_, [](const Team& team) -> const TeamId& { return team.id; }, "team");

    for (auto current = scopes_.begin(); current != scopes_.end(); ++current)
    {
        if (current->display_name.empty())
        {
            throw std::invalid_argument{"A scope display name must not be empty."};
        }
        if (std::find_if(std::next(current), scopes_.end(), [&](const ScopeDefinition& candidate) {
                return current->scope == candidate.scope;
            }) != scopes_.end())
        {
            throw std::invalid_argument{"Duplicate voice scope."};
        }
    }

    for (const auto& specialization : specializations_)
    {
        if (findGroup(specialization.group_id) == nullptr)
        {
            throw std::invalid_argument{"A specialization references an unknown group."};
        }
    }
    for (const auto& team : teams_)
    {
        if (findSpecialization(team.specialization_id) == nullptr)
        {
            throw std::invalid_argument{"A team references an unknown specialization."};
        }
    }
}

auto Hierarchy::id() const noexcept -> const HierarchyId&
{
    return id_;
}

auto Hierarchy::scopes() const noexcept -> std::span<const ScopeDefinition>
{
    return scopes_;
}

auto Hierarchy::groups() const noexcept -> std::span<const Group>
{
    return groups_;
}

auto Hierarchy::specializations() const noexcept -> std::span<const Specialization>
{
    return specializations_;
}

auto Hierarchy::teams() const noexcept -> std::span<const Team>
{
    return teams_;
}

auto Hierarchy::findScope(VoiceScope scope) const noexcept -> const ScopeDefinition*
{
    const auto found = std::ranges::find_if(
        scopes_, [scope](const ScopeDefinition& definition) { return definition.scope == scope; });
    return found == scopes_.end() ? nullptr : &*found;
}

auto Hierarchy::findGroup(const GroupId& group_id) const noexcept -> const Group*
{
    const auto found =
        std::ranges::find_if(groups_, [&](const Group& group) { return group.id == group_id; });
    return found == groups_.end() ? nullptr : &*found;
}

auto Hierarchy::findSpecialization(const SpecializationId& specialization_id) const noexcept
    -> const Specialization*
{
    const auto found = std::ranges::find_if(
        specializations_, [&](const Specialization& item) { return item.id == specialization_id; });
    return found == specializations_.end() ? nullptr : &*found;
}

auto Hierarchy::findTeam(const TeamId& team_id) const noexcept -> const Team*
{
    const auto found =
        std::ranges::find_if(teams_, [&](const Team& team) { return team.id == team_id; });
    return found == teams_.end() ? nullptr : &*found;
}

MembershipSnapshot::MembershipSnapshot(std::uint64_t version, Hierarchy hierarchy,
                                       std::vector<VoiceMembership> memberships)
    : version_(version), hierarchy_(std::move(hierarchy)), memberships_(std::move(memberships))
{
    if (version_ == 0)
    {
        throw std::invalid_argument{"A membership snapshot version must be greater than zero."};
    }

    requireUniqueIds(
        memberships_,
        [](const VoiceMembership& membership) -> const PlayerId& { return membership.player_id; },
        "player");

    for (const auto& membership : memberships_)
    {
        const Group* group = hierarchy_.findGroup(membership.group_id);
        const Specialization* specialization =
            hierarchy_.findSpecialization(membership.specialization_id);
        const Team* team = hierarchy_.findTeam(membership.team_id);
        if (group == nullptr || specialization == nullptr || team == nullptr ||
            specialization->group_id != group->id || team->specialization_id != specialization->id)
        {
            throw std::invalid_argument{"A membership does not form a valid hierarchy path."};
        }
    }

    std::ranges::sort(memberships_, {}, [](const VoiceMembership& membership) -> const PlayerId& {
        return membership.player_id;
    });
}

auto MembershipSnapshot::version() const noexcept -> std::uint64_t
{
    return version_;
}

auto MembershipSnapshot::hierarchy() const noexcept -> const Hierarchy&
{
    return hierarchy_;
}

auto MembershipSnapshot::memberships() const noexcept -> std::span<const VoiceMembership>
{
    return memberships_;
}

auto MembershipSnapshot::find(const PlayerId& player_id) const noexcept -> const VoiceMembership*
{
    const auto found =
        std::ranges::lower_bound(memberships_, player_id, {}, &VoiceMembership::player_id);
    return found == memberships_.end() || found->player_id != player_id ? nullptr : &*found;
}

RolePolicy::RolePolicy(std::vector<RolePermissions> roles) : roles_(std::move(roles))
{
    requireUniqueIds(
        roles_, [](const RolePermissions& role) -> const RoleId& { return role.role_id; }, "role");
}

auto RolePolicy::roles() const noexcept -> std::span<const RolePermissions>
{
    return roles_;
}

auto RolePolicy::canTransmit(std::span<const RoleId> roles, VoiceScope scope) const noexcept -> bool
{
    return std::ranges::any_of(roles, [&](const RoleId& role_id) {
        const auto role = std::ranges::find(roles_, role_id, &RolePermissions::role_id);
        return role != roles_.end() && containsScope(role->transmit_scopes, scope);
    });
}

auto RolePolicy::canReceive(std::span<const RoleId> roles, VoiceScope scope) const noexcept -> bool
{
    return std::ranges::any_of(roles, [&](const RoleId& role_id) {
        const auto role = std::ranges::find(roles_, role_id, &RolePermissions::role_id);
        return role != roles_.end() && containsScope(role->receive_scopes, scope);
    });
}
} // namespace hvc::domain
