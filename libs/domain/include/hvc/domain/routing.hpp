#pragma once

#include <cstdint>
#include <hvc/domain/model.hpp>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace hvc::domain
{
/// Classify why a transmission could not be routed.
enum class RoutingError : std::uint8_t
{
    /// The sender has no connected voice session.
    voice_not_connected,
    /// The sender is absent from the active membership snapshot.
    voice_no_active_membership,
    /// The requested scope is not defined by the hierarchy.
    voice_scope_not_found,
    /// The sender's roles do not permit transmission in the requested scope.
    voice_scope_not_authorized,
    /// The sender is muted or banned from transmission.
    voice_transmit_muted,
    /// The client authorized against an obsolete membership version.
    voice_membership_stale
};

/// Describe a request to resolve recipients for one transmission.
struct TransmissionRequest final
{
    /**
     * Construct a transmission request.
     *
     * @param sender Participant requesting transmission.
     * @param scope Requested hierarchy scope.
     * @param snapshot_version Membership version known to the client.
     */
    TransmissionRequest(PlayerId sender, VoiceScope scope, std::uint64_t snapshot_version)
        : sender_player_id(std::move(sender)), requested_scope(scope),
          membership_version(snapshot_version)
    {
    }

    /// Participant requesting transmission.
    PlayerId sender_player_id;
    /// Hierarchy scope requested by the sender.
    VoiceScope requested_scope;
    /// Membership version on which the client based the request.
    std::uint64_t membership_version;
};

/// Prevent one recipient from hearing one sender.
struct RecipientRestriction final
{
    /**
     * Construct a directional recipient restriction.
     *
     * @param recipient Participant applying the restriction.
     * @param blocked_sender Sender whose audio must be excluded.
     */
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    RecipientRestriction(PlayerId recipient, PlayerId blocked_sender)
        : recipient_player_id(std::move(recipient)),
          blocked_sender_player_id(std::move(blocked_sender))
    {
    }

    /// Participant who must not receive the blocked sender.
    PlayerId recipient_player_id;
    /// Sender excluded for the recipient.
    PlayerId blocked_sender_player_id;
};

/// Hold either a routing rejection or the authorized recipient set.
struct RecipientResolution final
{
    /**
     * Construct a recipient-resolution result.
     *
     * @param error Rejection reason, or no value for an accepted request.
     * @param resolved_recipients Authorized recipients; empty after rejection.
     */
    RecipientResolution(std::optional<RoutingError> error,
                        std::vector<PlayerId> resolved_recipients)
        : rejection(error), recipients(std::move(resolved_recipients))
    {
    }

    /// Rejection reason, or no value when routing succeeded.
    std::optional<RoutingError> rejection;
    /// Deterministically ordered authorized recipients.
    std::vector<PlayerId> recipients;

    /**
     * Return whether recipient resolution succeeded.
     *
     * @returns `true` when `rejection` has no value.
     */
    [[nodiscard]] auto accepted() const noexcept -> bool
    {
        return !rejection.has_value();
    }
};

/// Resolve an authorized and isolated recipient set from authoritative state.
class RecipientResolver final
{
  public:
    /**
     * Resolve recipients for a transmission request.
     *
     * The sender is authorized against `snapshot` and `role_policy`. Recipients
     * outside the requested hierarchy scope, unable to receive, or excluded by
     * `restrictions` are omitted.
     *
     * @param snapshot Authoritative membership state.
     * @param role_policy Role permissions used for authorization.
     * @param request Sender, scope, and client membership version.
     * @param restrictions Directional sender blocks to apply.
     * @returns Either a routing rejection or the authorized recipient set.
     */
    [[nodiscard]] static auto resolve(const MembershipSnapshot& snapshot,
                                      const RolePolicy& role_policy,
                                      const TransmissionRequest& request,
                                      std::span<const RecipientRestriction> restrictions = {})
        -> RecipientResolution;
};
} // namespace hvc::domain
