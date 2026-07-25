#include <algorithm>
#include <hvc/domain/routing.hpp>

namespace hvc::domain
{
namespace
{
auto belongsToScope(const VoiceMembership& sender, const VoiceMembership& candidate,
                    VoiceScope scope) noexcept -> bool
{
    switch (scope)
    {
    case VoiceScope::team:
        return candidate.group_id == sender.group_id &&
               candidate.specialization_id == sender.specialization_id &&
               candidate.team_id == sender.team_id;
    case VoiceScope::specialization:
        return candidate.group_id == sender.group_id &&
               candidate.specialization_id == sender.specialization_id;
    case VoiceScope::group:
        return candidate.group_id == sender.group_id;
    }
    return false;
}

auto isRestricted(std::span<const RecipientRestriction> restrictions, const PlayerId& recipient,
                  const PlayerId& sender) noexcept -> bool
{
    return std::ranges::any_of(restrictions, [&](const RecipientRestriction& restriction) {
        return restriction.recipient_player_id == recipient &&
               restriction.blocked_sender_player_id == sender;
    });
}

auto reject(RoutingError error) -> RecipientResolution
{
    return RecipientResolution{error, {}};
}
} // namespace

auto RecipientResolver::resolve(const MembershipSnapshot& snapshot, const RolePolicy& role_policy,
                                const TransmissionRequest& request,
                                std::span<const RecipientRestriction> restrictions)
    -> RecipientResolution
{
    if (request.membership_version != snapshot.version())
    {
        return reject(RoutingError::voice_membership_stale);
    }

    const VoiceMembership* sender = snapshot.find(request.sender_player_id);
    if (sender == nullptr)
    {
        return reject(RoutingError::voice_no_active_membership);
    }
    if (!sender->connected)
    {
        return reject(RoutingError::voice_not_connected);
    }

    const Group* sender_group = snapshot.hierarchy().findGroup(sender->group_id);
    if (sender_group == nullptr || !sender_group->active)
    {
        return reject(RoutingError::voice_no_active_membership);
    }
    if (snapshot.hierarchy().findScope(request.requested_scope) == nullptr)
    {
        return reject(RoutingError::voice_scope_not_found);
    }
    if (sender->transmit_muted || sender->voice_ban_status != VoiceBanStatus::none)
    {
        return reject(RoutingError::voice_transmit_muted);
    }
    if (!role_policy.canTransmit(sender->role_ids, request.requested_scope))
    {
        return reject(RoutingError::voice_scope_not_authorized);
    }

    std::vector<PlayerId> recipients;
    for (const auto& candidate : snapshot.memberships())
    {
        if (candidate.connected && candidate.can_receive_voice &&
            role_policy.canReceive(candidate.role_ids, request.requested_scope) &&
            belongsToScope(*sender, candidate, request.requested_scope) &&
            !isRestricted(restrictions, candidate.player_id, sender->player_id))
        {
            recipients.push_back(candidate.player_id);
        }
    }

    return RecipientResolution{std::nullopt, std::move(recipients)};
}
} // namespace hvc::domain
