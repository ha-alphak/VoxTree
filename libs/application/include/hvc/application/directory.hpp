#pragma once

#include <cstddef>
#include <cstdint>
#include <hvc/application/control_plane.hpp>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hvc::application
{
/// Identify the hierarchy level represented by a public directory node.
enum class DirectoryNodeType : std::uint8_t
{
    /// Represent the one visible group root.
    group,
    /// Represent a specialization below the visible group.
    specialization,
    /// Represent a team below a visible specialization.
    team
};

/// Hold one public hierarchy node without authorization metadata.
struct DirectoryNode final
{
    /// Stable hierarchy-node identifier.
    std::string node_id;
    /// Public hierarchy level.
    DirectoryNodeType node_type;
    /// Parent node, or no value for the group root.
    std::optional<std::string> parent_node_id;
    /// Human-readable hierarchy label.
    std::string display_name;
    /// Stable sibling order supplied by the authoritative hierarchy.
    std::uint32_t sort_index;
};

/// Hold a public role label that may be disclosed by the directory.
struct PublicDirectoryRole final
{
    /// Stable role identifier.
    domain::RoleId role_id;
    /// Human-readable public role label.
    std::string display_name;
    /// Positive source-resource version used to reject stale replacements.
    std::uint64_t version;
};

/// Hold the public profile fields used by the directory.
struct DirectoryProfile final
{
    /// Stable participant identifier.
    domain::PlayerId player_id;
    /// Human-readable participant name.
    std::string display_name;
    /// Positive source-profile version used to reject stale replacements.
    std::uint64_t version;
};

/// Hold one privacy-limited participant row.
struct DirectoryParticipant final
{
    /// Stable public participant identifier.
    domain::PlayerId player_id;
    /// Human-readable participant name.
    std::string display_name;
    /// Participant's primary team in the visible group.
    domain::TeamId primary_team_id;
    /// Explicitly public roles assigned to the participant.
    std::vector<domain::RoleId> public_role_ids;
};

/// Hold one complete group-limited directory view.
struct DirectorySnapshot final
{
    /// Strictly positive version of this visible group view.
    std::uint64_t version;
    /// Group from which the authenticated participant's view was derived.
    domain::GroupId group_id;
    /// Visible hierarchy rooted at `group_id`.
    std::vector<DirectoryNode> nodes;
    /// Public role labels available to participant rows.
    std::vector<PublicDirectoryRole> public_roles;
    /// Visible participants in the authenticated participant's group.
    std::vector<DirectoryParticipant> participants;
};

/// Identify why a directory snapshot could not be produced.
enum class DirectoryReadError : std::uint8_t
{
    /// The actor has no active authoritative membership.
    directory_unavailable,
    /// The visible group exceeds the v1 participant limit.
    directory_limit_exceeded
};

/// Hold either a directory snapshot or its stable failure category.
struct DirectoryReadResult final
{
    /// Directory snapshot on success.
    std::optional<DirectorySnapshot> snapshot;
    /// Failure category when no snapshot is available.
    std::optional<DirectoryReadError> error;

    /**
     * Determine whether the operation produced a directory snapshot.
     *
     * @returns `true` only when `snapshot` is present and `error` is absent.
     */
    [[nodiscard]] auto successful() const noexcept -> bool;
};

/// Identify the current aggregated transport presence of a participant.
enum class DirectoryPresenceState : std::uint8_t
{
    /// No authorized voice-scope transport is connected.
    offline,
    /// At least one authorized voice-scope transport is connected.
    online
};

/// Hold one privacy-limited presence observation.
struct DirectoryPresenceEntry final
{
    /// Stable public participant identifier.
    domain::PlayerId player_id;
    /// Aggregated current transport presence.
    DirectoryPresenceState state;
};

/// Identify whether a presence response is complete or incremental.
enum class DirectoryPresenceMode : std::uint8_t
{
    /// Include every currently visible participant exactly once.
    snapshot,
    /// Include the latest visible change per participant after a version.
    delta
};

/// Hold one group-limited presence response.
struct DirectoryPresenceSnapshot final
{
    /// Strictly positive version of the visible presence state.
    std::uint64_t version;
    /// Whether `entries` is a full snapshot or an incremental result.
    DirectoryPresenceMode mode;
    /// Authoritative observation time.
    TimePoint observed_at;
    /// Visible presence entries without transport counts or history.
    std::vector<DirectoryPresenceEntry> entries;
};

/// Identify why a presence response could not be produced.
enum class DirectoryPresenceReadError : std::uint8_t
{
    /// The actor has no active authoritative membership.
    directory_unavailable,
    /// The requested delta base is invalid, future, or no longer retained.
    snapshot_required
};

/// Hold either a presence response or its stable failure category.
struct DirectoryPresenceReadResult final
{
    /// Presence response on success.
    std::optional<DirectoryPresenceSnapshot> snapshot;
    /// Failure category when no snapshot is available.
    std::optional<DirectoryPresenceReadError> error;

    /**
     * Determine whether the operation produced a presence response.
     *
     * @returns `true` only when `snapshot` is present and `error` is absent.
     */
    [[nodiscard]] auto successful() const noexcept -> bool;
};

/**
 * Supply current voice-transport presence independently from membership data.
 *
 * Implementations aggregate every authorized scope connection for a player.
 * The directory discloses only whether the returned count is zero.
 */
class ITransportPresenceProvider
{
  public:
    /// Destroy the transport-presence interface.
    virtual ~ITransportPresenceProvider() = default;

    /**
     * Count currently connected authorized voice scopes.
     *
     * @param player_id Participant whose transport presence is requested.
     * @returns Current scope-connection count. The value is never exposed.
     */
    [[nodiscard]] virtual auto connectedScopeCount(const domain::PlayerId& player_id) const
        -> std::size_t = 0;
};

/**
 * Build privacy-limited directory and presence views from authoritative state.
 *
 * The service owns only public profile and role projections plus bounded,
 * group-local version histories. Membership and transport-presence providers
 * must outlive it. All public operations are safe to call concurrently.
 */
class DirectoryApplicationService final
{
  public:
    /**
     * Construct a directory application service.
     *
     * @param memberships Authoritative membership source.
     * @param transport_presence Aggregated transport-presence source.
     */
    DirectoryApplicationService(const IAuthoritativeMembershipProvider& memberships,
                                const ITransportPresenceProvider& transport_presence) noexcept;

    /**
     * Insert or replace one public profile when its version is newer.
     *
     * @param profile Public profile projection.
     * @returns `true` when the profile was inserted or replaced.
     * @throws std::invalid_argument Thrown for an empty name or zero version.
     * @exceptsafe Strong exception guarantee.
     */
    [[nodiscard]] auto upsertProfile(DirectoryProfile profile) -> bool;

    /**
     * Replace the complete public role catalog when its version is newer.
     *
     * @param catalog_version Positive monotonically increasing catalog version.
     * @param roles Complete set of roles explicitly approved for disclosure.
     * @returns `true` when the catalog was replaced.
     * @throws std::invalid_argument Thrown for zero versions, empty labels,
     *     duplicate role IDs, or role versions newer than `catalog_version`.
     * @exceptsafe Strong exception guarantee.
     */
    [[nodiscard]] auto replacePublicRoles(std::uint64_t catalog_version,
                                          std::vector<PublicDirectoryRole> roles) -> bool;

    /**
     * Read the authenticated participant's current group directory.
     *
     * @param actor Authenticated participant.
     * @returns Group-limited directory or a stable failure category.
     */
    [[nodiscard]] auto directoryFor(const domain::PlayerId& actor) const -> DirectoryReadResult;

    /**
     * Read a full or incremental group-limited presence view.
     *
     * @param actor Authenticated participant.
     * @param after_version Exclusive delta base, or no value for a snapshot.
     * @param observed_at Authoritative time attached to the response.
     * @returns Presence response or a stable failure category.
     */
    [[nodiscard]] auto presenceFor(const domain::PlayerId& actor,
                                   std::optional<std::uint64_t> after_version,
                                   TimePoint observed_at) const -> DirectoryPresenceReadResult;

  private:
    struct VersionedDirectory final
    {
        std::uint64_t version;
        std::string fingerprint;
    };

    struct PresenceRevision final
    {
        std::uint64_t version;
        std::vector<DirectoryPresenceEntry> entries;
    };

    struct VersionedPresence final
    {
        std::uint64_t version;
        bool initialized{false};
        std::map<domain::PlayerId, DirectoryPresenceState> states;
        std::vector<PresenceRevision> revisions;
    };

    [[nodiscard]] auto currentContextFor(const domain::PlayerId& actor) const
        -> std::optional<AuthoritativeMembershipContext>;
    [[nodiscard]] auto makeDirectory(const AuthoritativeMembershipContext& context,
                                     const domain::VoiceMembership& actor_membership) const
        -> DirectoryReadResult;

    const IAuthoritativeMembershipProvider& memberships_;
    const ITransportPresenceProvider& transport_presence_;
    mutable std::mutex mutex_;
    std::map<domain::PlayerId, DirectoryProfile> profiles_;
    std::map<domain::RoleId, PublicDirectoryRole> public_roles_;
    std::uint64_t public_role_catalog_version_{0};
    mutable std::map<domain::GroupId, VersionedDirectory> directory_versions_;
    mutable std::map<domain::GroupId, VersionedPresence> presence_versions_;
};
} // namespace hvc::application
