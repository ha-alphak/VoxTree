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
/// Identify one addressable level of the voice hierarchy.
enum class VoiceScope : std::uint8_t
{
    /// Address members of the sender's current team.
    team,
    /// Address all teams in the sender's specialization.
    specialization,
    /// Address all eligible members of the sender's group.
    group
};

/// Describe the policy and presentation metadata of a voice scope.
struct ScopeDefinition final
{
    /**
     * Construct a scope definition.
     *
     * @param scope_value Addressable hierarchy level.
     * @param name Human-readable scope name.
     * @param priority_value Relative arbitration priority.
     * @param speaker_limit Maximum simultaneous speakers, or no value for no
     *     explicit limit.
     */
    ScopeDefinition(VoiceScope scope_value, std::string name, int priority_value,
                    std::optional<std::size_t> speaker_limit)
        : scope(scope_value), display_name(std::move(name)), priority(priority_value),
          max_concurrent_speakers(speaker_limit)
    {
    }

    /// Addressable hierarchy level.
    VoiceScope scope;
    /// Human-readable name displayed to users.
    std::string display_name;
    /// Relative priority used when scopes contend for transmission.
    int priority;
    /// Maximum simultaneous speakers, or no value when unlimited by this policy.
    std::optional<std::size_t> max_concurrent_speakers;
};

/// Represent one top-level communication group.
struct Group final
{
    /**
     * Construct a group.
     *
     * @param group_id Stable group identifier.
     * @param name Human-readable group name.
     * @param is_active Whether the group may currently participate in voice
     *     communication.
     */
    Group(GroupId group_id, std::string name, bool is_active = true)
        : id(std::move(group_id)), display_name(std::move(name)), active(is_active)
    {
    }

    /// Stable group identifier.
    GroupId id;
    /// Human-readable group name.
    std::string display_name;
    /// Whether the group is enabled for voice communication.
    bool active{true};
};

/// Represent a specialization owned by one group.
struct Specialization final
{
    /**
     * Construct a specialization.
     *
     * @param specialization_id Stable specialization identifier.
     * @param parent_group_id Group that owns the specialization.
     * @param name Human-readable specialization name.
     */
    Specialization(SpecializationId specialization_id, GroupId parent_group_id, std::string name)
        : id(std::move(specialization_id)), group_id(std::move(parent_group_id)),
          display_name(std::move(name))
    {
    }

    /// Stable specialization identifier.
    SpecializationId id;
    /// Identifier of the owning group.
    GroupId group_id;
    /// Human-readable specialization name.
    std::string display_name;
};

/// Represent a team owned by one specialization.
struct Team final
{
    /**
     * Construct a team.
     *
     * @param team_id Stable team identifier.
     * @param parent_specialization_id Specialization that owns the team.
     * @param name Human-readable team name.
     */
    Team(TeamId team_id, SpecializationId parent_specialization_id, std::string name)
        : id(std::move(team_id)), specialization_id(std::move(parent_specialization_id)),
          display_name(std::move(name))
    {
    }

    /// Stable team identifier.
    TeamId id;
    /// Identifier of the owning specialization.
    SpecializationId specialization_id;
    /// Human-readable team name.
    std::string display_name;
};

/**
 * Own an immutable, validated definition of the communication hierarchy.
 *
 * Construction rejects duplicate identifiers, missing parents, incomplete
 * scope definitions, and other structurally invalid configurations.
 */
class Hierarchy final
{
  public:
    /**
     * Construct and validate a hierarchy.
     *
     * @param hierarchy_id Stable identifier for this hierarchy definition.
     * @param scopes Definitions for all supported voice scopes.
     * @param groups Top-level groups.
     * @param specializations Specializations owned by `groups`.
     * @param teams Teams owned by `specializations`.
     * @throws std::invalid_argument Thrown when the hierarchy is incomplete,
     *     contains duplicates, or references a missing parent.
     * @exceptsafe Strong exception guarantee.
     */
    Hierarchy(HierarchyId hierarchy_id, std::vector<ScopeDefinition> scopes,
              std::vector<Group> groups, std::vector<Specialization> specializations,
              std::vector<Team> teams);

    /**
     * Return the stable identifier of this hierarchy.
     *
     * @returns Identifier reference valid for the lifetime of the hierarchy.
     */
    [[nodiscard]] auto id() const noexcept -> const HierarchyId&;
    /**
     * Return all configured scope definitions.
     *
     * @returns Read-only view valid while the hierarchy is alive.
     */
    [[nodiscard]] auto scopes() const noexcept -> std::span<const ScopeDefinition>;
    /**
     * Return all configured groups.
     *
     * @returns Read-only view valid while the hierarchy is alive.
     */
    [[nodiscard]] auto groups() const noexcept -> std::span<const Group>;
    /**
     * Return all configured specializations.
     *
     * @returns Read-only view valid while the hierarchy is alive.
     */
    [[nodiscard]] auto specializations() const noexcept -> std::span<const Specialization>;
    /**
     * Return all configured teams.
     *
     * @returns Read-only view valid while the hierarchy is alive.
     */
    [[nodiscard]] auto teams() const noexcept -> std::span<const Team>;

    /**
     * Find the definition for a voice scope.
     *
     * @param scope Scope to find.
     * @returns The matching definition, or `nullptr` when it is not configured.
     */
    [[nodiscard]] auto findScope(VoiceScope scope) const noexcept -> const ScopeDefinition*;
    /**
     * Find a group by identifier.
     *
     * @param group_id Identifier to find.
     * @returns The matching group, or `nullptr` when it does not exist.
     */
    [[nodiscard]] auto findGroup(const GroupId& group_id) const noexcept -> const Group*;
    /**
     * Find a specialization by identifier.
     *
     * @param specialization_id Identifier to find.
     * @returns The matching specialization, or `nullptr` when it does not exist.
     */
    [[nodiscard]] auto findSpecialization(const SpecializationId& specialization_id) const noexcept
        -> const Specialization*;
    /**
     * Find a team by identifier.
     *
     * @param team_id Identifier to find.
     * @returns The matching team, or `nullptr` when it does not exist.
     */
    [[nodiscard]] auto findTeam(const TeamId& team_id) const noexcept -> const Team*;

  private:
    HierarchyId id_;
    std::vector<ScopeDefinition> scopes_;
    std::vector<Group> groups_;
    std::vector<Specialization> specializations_;
    std::vector<Team> teams_;
};

/// Describe whether a participant is prohibited from voice transmission.
enum class VoiceBanStatus : std::uint8_t
{
    /// No voice ban is active.
    none,
    /// A time-bounded voice ban is active.
    temporary,
    /// An indefinite voice ban is active.
    permanent
};

/// Describe one participant's authoritative place and eligibility in a hierarchy.
struct VoiceMembership final
{
    /**
     * Construct a participant membership.
     *
     * @param player Participant represented by the membership.
     * @param group Owning group.
     * @param specialization Owning specialization.
     * @param team Owning team.
     * @param roles Authorization roles assigned to the participant.
     */
    VoiceMembership(PlayerId player, GroupId group, SpecializationId specialization, TeamId team,
                    std::vector<RoleId> roles)
        : player_id(std::move(player)), group_id(std::move(group)),
          specialization_id(std::move(specialization)), team_id(std::move(team)),
          role_ids(std::move(roles))
    {
    }

    /// Participant represented by this membership.
    PlayerId player_id;
    /// Group containing the participant.
    GroupId group_id;
    /// Specialization containing the participant.
    SpecializationId specialization_id;
    /// Team containing the participant.
    TeamId team_id;
    /// Authorization roles assigned to the participant.
    std::vector<RoleId> role_ids;
    /// Whether the participant currently has a connected voice session.
    bool connected{true};
    /// Whether the participant may receive voice audio.
    bool can_receive_voice{true};
    /// Whether transmission is administratively muted.
    bool transmit_muted{false};
    /// Active voice-ban classification.
    VoiceBanStatus voice_ban_status{VoiceBanStatus::none};
};

/**
 * Own a versioned, immutable view of hierarchy memberships.
 *
 * Construction validates that every membership references a consistent group,
 * specialization, and team and that each player appears at most once.
 */
class MembershipSnapshot final
{
  public:
    /**
     * Construct and validate a membership snapshot.
     *
     * @param version Monotonically increasing authoritative version.
     * @param hierarchy Hierarchy against which memberships are validated.
     * @param memberships Participant memberships in this version.
     * @throws std::invalid_argument Thrown when `version` is zero or a membership
     *     is duplicate or inconsistent with `hierarchy`.
     * @exceptsafe Strong exception guarantee.
     */
    MembershipSnapshot(std::uint64_t version, Hierarchy hierarchy,
                       std::vector<VoiceMembership> memberships);

    /**
     * Return the authoritative snapshot version.
     *
     * @returns Positive monotonically increasing version.
     */
    [[nodiscard]] auto version() const noexcept -> std::uint64_t;
    /**
     * Return the hierarchy associated with this snapshot.
     *
     * @returns Hierarchy reference valid for the lifetime of the snapshot.
     */
    [[nodiscard]] auto hierarchy() const noexcept -> const Hierarchy&;
    /**
     * Return all participant memberships.
     *
     * @returns Read-only view valid while the snapshot is alive.
     */
    [[nodiscard]] auto memberships() const noexcept -> std::span<const VoiceMembership>;
    /**
     * Find a participant membership.
     *
     * @param player_id Participant to find.
     * @returns The matching membership, or `nullptr` when the participant is
     *     absent.
     */
    [[nodiscard]] auto find(const PlayerId& player_id) const noexcept -> const VoiceMembership*;

  private:
    std::uint64_t version_;
    Hierarchy hierarchy_;
    std::vector<VoiceMembership> memberships_;
};

/// Map one role to independently granted transmit and receive scopes.
struct RolePermissions final
{
    // The two scope collections intentionally have the same type: one grants sending and the other
    // grants receiving.
    /**
     * Construct permissions for a role.
     *
     * @param role Role receiving the permissions.
     * @param allowed_transmit_scopes Scopes in which the role may transmit.
     * @param allowed_receive_scopes Scopes from which the role may receive.
     */
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    RolePermissions(RoleId role, std::vector<VoiceScope> allowed_transmit_scopes,
                    std::vector<VoiceScope> allowed_receive_scopes)
        : role_id(std::move(role)), transmit_scopes(std::move(allowed_transmit_scopes)),
          receive_scopes(std::move(allowed_receive_scopes))
    {
    }

    /// Role receiving these permissions.
    RoleId role_id;
    /// Scopes in which the role may transmit.
    std::vector<VoiceScope> transmit_scopes;
    /// Scopes from which the role may receive.
    std::vector<VoiceScope> receive_scopes;
};

/// Evaluate effective voice permissions across a participant's roles.
class RolePolicy final
{
  public:
    /**
     * Construct and validate a role policy.
     *
     * @param roles Permission entries keyed by role identifier.
     * @throws std::invalid_argument Thrown when a role is duplicated or a scope
     *     list contains duplicates.
     * @exceptsafe Strong exception guarantee.
     */
    explicit RolePolicy(std::vector<RolePermissions> roles);

    /**
     * Return all configured role permissions.
     *
     * @returns Read-only view valid while the policy is alive.
     */
    [[nodiscard]] auto roles() const noexcept -> std::span<const RolePermissions>;
    /**
     * Determine whether any assigned role grants transmission in a scope.
     *
     * @param roles Roles assigned to the participant.
     * @param scope Requested transmit scope.
     * @returns `true` when at least one role grants the scope.
     */
    [[nodiscard]] auto canTransmit(std::span<const RoleId> roles, VoiceScope scope) const noexcept
        -> bool;
    /**
     * Determine whether any assigned role grants reception from a scope.
     *
     * @param roles Roles assigned to the participant.
     * @param scope Requested receive scope.
     * @returns `true` when at least one role grants the scope.
     */
    [[nodiscard]] auto canReceive(std::span<const RoleId> roles, VoiceScope scope) const noexcept
        -> bool;

  private:
    std::vector<RolePermissions> roles_;
};
} // namespace hvc::domain
