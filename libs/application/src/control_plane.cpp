#include <hvc/application/control_plane.hpp>
#include <stdexcept>

namespace hvc::application
{
namespace
{
auto mapRoutingError(domain::RoutingError error) -> TransmissionAuthorizationError
{
    switch (error)
    {
    case domain::RoutingError::voice_not_connected:
        return TransmissionAuthorizationError::voice_not_connected;
    case domain::RoutingError::voice_no_active_membership:
        return TransmissionAuthorizationError::voice_no_active_membership;
    case domain::RoutingError::voice_scope_not_found:
        return TransmissionAuthorizationError::voice_scope_not_found;
    case domain::RoutingError::voice_scope_not_authorized:
        return TransmissionAuthorizationError::voice_scope_not_authorized;
    case domain::RoutingError::voice_transmit_muted:
        return TransmissionAuthorizationError::voice_transmit_muted;
    case domain::RoutingError::voice_membership_stale:
        return TransmissionAuthorizationError::voice_membership_stale;
    }

    throw std::logic_error{"Unhandled routing error."};
}
} // namespace

SessionAuthenticationResult::SessionAuthenticationResult(
    std::optional<AuthenticatedSession> authenticated_session,
    std::optional<SessionAuthenticationError> authentication_error)
    : session(std::move(authenticated_session)), error(authentication_error)
{
    if (session.has_value() == error.has_value())
    {
        throw std::invalid_argument{
            "A session authentication result must contain either a session or an error."};
    }
}

auto SessionAuthenticationResult::accepted(AuthenticatedSession authenticated_session)
    -> SessionAuthenticationResult
{
    return SessionAuthenticationResult{std::move(authenticated_session), std::nullopt};
}

auto SessionAuthenticationResult::rejected(SessionAuthenticationError authentication_error)
    -> SessionAuthenticationResult
{
    return SessionAuthenticationResult{std::nullopt, authentication_error};
}

AuthoritativeMembershipContext::AuthoritativeMembershipContext(
    std::shared_ptr<const domain::MembershipSnapshot> snapshot_value,
    std::shared_ptr<const domain::RolePolicy> role_policy_value)
    : snapshot(std::move(snapshot_value)), role_policy(std::move(role_policy_value))
{
    if (!snapshot || !role_policy)
    {
        throw std::invalid_argument{
            "An authoritative membership context requires a snapshot and a role policy."};
    }
}

TransmissionAuthorizationResult::TransmissionAuthorizationResult(
    std::optional<AuthorizedTransmission> authorized_transmission,
    std::optional<TransmissionAuthorizationError> authorization_error)
    : transmission(std::move(authorized_transmission)), error(authorization_error)
{
    if (transmission.has_value() == error.has_value())
    {
        throw std::invalid_argument{
            "A transmission authorization result must contain either a transmission or an error."};
    }
}

auto TransmissionAuthorizationResult::accepted(AuthorizedTransmission authorized_transmission)
    -> TransmissionAuthorizationResult
{
    return TransmissionAuthorizationResult{std::move(authorized_transmission), std::nullopt};
}

auto TransmissionAuthorizationResult::rejected(TransmissionAuthorizationError authorization_error)
    -> TransmissionAuthorizationResult
{
    return TransmissionAuthorizationResult{std::nullopt, authorization_error};
}

TransmissionAuthorizationService::TransmissionAuthorizationService(
    const ISessionRepository& sessions, const IAuthoritativeMembershipProvider& memberships,
    ITransmissionIdGenerator& transmission_ids)
    : sessions_(sessions), memberships_(memberships), transmission_ids_(transmission_ids)
{
}

auto TransmissionAuthorizationService::authorizeStart(const StartTransmissionCommand& command,
                                                      TimePoint now)
    -> TransmissionAuthorizationResult
{
    const auto session = sessions_.find(command.session_id);
    if (!session)
    {
        return TransmissionAuthorizationResult::rejected(
            TransmissionAuthorizationError::session_not_found);
    }
    if (session->device_id != command.device_id)
    {
        return TransmissionAuthorizationResult::rejected(
            TransmissionAuthorizationError::session_device_mismatch);
    }
    if (!session->activeAt(now))
    {
        return TransmissionAuthorizationResult::rejected(
            TransmissionAuthorizationError::session_expired);
    }

    const auto membership = memberships_.currentFor(session->player_id);
    if (!membership)
    {
        return TransmissionAuthorizationResult::rejected(
            TransmissionAuthorizationError::membership_unavailable);
    }

    const auto resolution = domain::RecipientResolver::resolve(
        *membership->snapshot, *membership->role_policy,
        {session->player_id, command.scope, command.membership_version});
    if (!resolution.accepted())
    {
        return TransmissionAuthorizationResult::rejected(mapRoutingError(*resolution.rejection));
    }

    return TransmissionAuthorizationResult::accepted(
        {transmission_ids_.next(), command.client_transmission_id, session->player_id,
         command.scope, membership->snapshot->version(), resolution.recipients,
         command.correlation_id});
}
} // namespace hvc::application
