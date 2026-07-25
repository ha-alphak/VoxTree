#pragma once

#include <cstddef>
#include <cstdint>
#include <hvc/domain/id.hpp>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace hvc::domain
{
enum class VoiceScope : std::uint8_t
{
    team,
    specialization,
    group
};

struct ScopeDefinition final
{
    ScopeDefinition(VoiceScope scope_value, std::string name, int priority_value,
                    std::optional<std::size_t> speaker_limit)
        : scope(scope_value), display_name(std::move(name)), priority(priority_value),
          max_concurrent_speakers(speaker_limit)
    {
    }

    VoiceScope scope;
    std::string display_name;
    int priority;
    std::optional<std::size_t> max_concurrent_speakers;
};

struct Group final
{
    Group(GroupId group_id, std::string name, bool is_active = true)
        : id(std::move(group_id)), display_name(std::move(name)), active(is_active)
    {
    }

    GroupId id;
    std::string display_name;
    bool active{true};
};

struct Specialization final
{
    Specialization(SpecializationId specialization_id, GroupId parent_group_id, std::string name)
        : id(std::move(specialization_id)), group_id(std::move(parent_group_id)),
          display_name(std::move(name))
    {
    }

    SpecializationId id;
    GroupId group_id;
    std::string display_name;
};

struct Team final
{
    Team(TeamId team_id, SpecializationId parent_specialization_id, std::string name)
        : id(std::move(team_id)), specialization_id(std::move(parent_specialization_id)),
          display_name(std::move(name))
    {
    }

    TeamId id;
    SpecializationId specialization_id;
    std::string display_name;
};

class Hierarchy final
{
  public:
    Hierarchy(HierarchyId hierarchy_id, std::vector<ScopeDefinition> scopes,
              std::vector<Group> groups, std::vector<Specialization> specializations,
              std::vector<Team> teams);

    [[nodiscard]] auto id() const noexcept -> const HierarchyId&;
    [[nodiscard]] auto scopes() const noexcept -> std::span<const ScopeDefinition>;
    [[nodiscard]] auto groups() const noexcept -> std::span<const Group>;
    [[nodiscard]] auto specializations() const noexcept -> std::span<const Specialization>;
    [[nodiscard]] auto teams() const noexcept -> std::span<const Team>;

    [[nodiscard]] auto findScope(VoiceScope scope) const noexcept -> const ScopeDefinition*;
    [[nodiscard]] auto findGroup(const GroupId& group_id) const noexcept -> const Group*;
    [[nodiscard]] auto findSpecialization(const SpecializationId& specialization_id) const noexcept
        -> const Specialization*;
    [[nodiscard]] auto findTeam(const TeamId& team_id) const noexcept -> const Team*;

  private:
    HierarchyId id_;
    std::vector<ScopeDefinition> scopes_;
    std::vector<Group> groups_;
    std::vector<Specialization> specializations_;
    std::vector<Team> teams_;
};

enum class VoiceBanStatus : std::uint8_t
{
    none,
    temporary,
    permanent
};

struct VoiceMembership final
{
    VoiceMembership(PlayerId player, GroupId group, SpecializationId specialization, TeamId team,
                    std::vector<RoleId> roles)
        : player_id(std::move(player)), group_id(std::move(group)),
          specialization_id(std::move(specialization)), team_id(std::move(team)),
          role_ids(std::move(roles))
    {
    }

    PlayerId player_id;
    GroupId group_id;
    SpecializationId specialization_id;
    TeamId team_id;
    std::vector<RoleId> role_ids;
    bool connected{true};
    bool can_receive_voice{true};
    bool transmit_muted{false};
    VoiceBanStatus voice_ban_status{VoiceBanStatus::none};
};

class MembershipSnapshot final
{
  public:
    MembershipSnapshot(std::uint64_t version, Hierarchy hierarchy,
                       std::vector<VoiceMembership> memberships);

    [[nodiscard]] auto version() const noexcept -> std::uint64_t;
    [[nodiscard]] auto hierarchy() const noexcept -> const Hierarchy&;
    [[nodiscard]] auto memberships() const noexcept -> std::span<const VoiceMembership>;
    [[nodiscard]] auto find(const PlayerId& player_id) const noexcept -> const VoiceMembership*;

  private:
    std::uint64_t version_;
    Hierarchy hierarchy_;
    std::vector<VoiceMembership> memberships_;
};

struct RolePermissions final
{
    // The two scope collections intentionally have the same type: one grants sending and the other
    // grants receiving.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    RolePermissions(RoleId role, std::vector<VoiceScope> allowed_transmit_scopes,
                    std::vector<VoiceScope> allowed_receive_scopes)
        : role_id(std::move(role)), transmit_scopes(std::move(allowed_transmit_scopes)),
          receive_scopes(std::move(allowed_receive_scopes))
    {
    }

    RoleId role_id;
    std::vector<VoiceScope> transmit_scopes;
    std::vector<VoiceScope> receive_scopes;
};

class RolePolicy final
{
  public:
    explicit RolePolicy(std::vector<RolePermissions> roles);

    [[nodiscard]] auto roles() const noexcept -> std::span<const RolePermissions>;
    [[nodiscard]] auto canTransmit(std::span<const RoleId> roles, VoiceScope scope) const noexcept
        -> bool;
    [[nodiscard]] auto canReceive(std::span<const RoleId> roles, VoiceScope scope) const noexcept
        -> bool;

  private:
    std::vector<RolePermissions> roles_;
};
} // namespace hvc::domain
