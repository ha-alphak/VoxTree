#pragma once

#include <cstdint>
#include <hvc/domain/model.hpp>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace hvc::domain
{
enum class RoutingError : std::uint8_t
{
    voice_not_connected,
    voice_no_active_membership,
    voice_scope_not_found,
    voice_scope_not_authorized,
    voice_transmit_muted,
    voice_membership_stale
};

struct TransmissionRequest final
{
    TransmissionRequest(PlayerId sender, VoiceScope scope, std::uint64_t snapshot_version)
        : sender_player_id(std::move(sender)), requested_scope(scope),
          membership_version(snapshot_version)
    {
    }

    PlayerId sender_player_id;
    VoiceScope requested_scope;
    std::uint64_t membership_version;
};

struct RecipientRestriction final
{
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    RecipientRestriction(PlayerId recipient, PlayerId blocked_sender)
        : recipient_player_id(std::move(recipient)),
          blocked_sender_player_id(std::move(blocked_sender))
    {
    }

    PlayerId recipient_player_id;
    PlayerId blocked_sender_player_id;
};

struct RecipientResolution final
{
    RecipientResolution(std::optional<RoutingError> error,
                        std::vector<PlayerId> resolved_recipients)
        : rejection(error), recipients(std::move(resolved_recipients))
    {
    }

    std::optional<RoutingError> rejection;
    std::vector<PlayerId> recipients;

    [[nodiscard]] auto accepted() const noexcept -> bool
    {
        return !rejection.has_value();
    }
};

class RecipientResolver final
{
  public:
    [[nodiscard]] static auto resolve(const MembershipSnapshot& snapshot,
                                      const RolePolicy& role_policy,
                                      const TransmissionRequest& request,
                                      std::span<const RecipientRestriction> restrictions = {})
        -> RecipientResolution;
};
} // namespace hvc::domain
