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

auto mapAuthorizationError(TransmissionAuthorizationError error) -> StartTransmissionError
{
    switch (error)
    {
    case TransmissionAuthorizationError::session_not_found:
        return StartTransmissionError::session_not_found;
    case TransmissionAuthorizationError::session_device_mismatch:
        return StartTransmissionError::session_device_mismatch;
    case TransmissionAuthorizationError::session_expired:
        return StartTransmissionError::session_expired;
    case TransmissionAuthorizationError::membership_unavailable:
        return StartTransmissionError::membership_unavailable;
    case TransmissionAuthorizationError::voice_not_connected:
        return StartTransmissionError::voice_not_connected;
    case TransmissionAuthorizationError::voice_no_active_membership:
        return StartTransmissionError::voice_no_active_membership;
    case TransmissionAuthorizationError::voice_scope_not_found:
        return StartTransmissionError::voice_scope_not_found;
    case TransmissionAuthorizationError::voice_scope_not_authorized:
        return StartTransmissionError::voice_scope_not_authorized;
    case TransmissionAuthorizationError::voice_transmit_muted:
        return StartTransmissionError::voice_transmit_muted;
    case TransmissionAuthorizationError::voice_membership_stale:
        return StartTransmissionError::voice_membership_stale;
    }

    throw std::logic_error{"Unhandled transmission authorization error."};
}

auto mapActivationError(TransmissionActivationError error) -> StartTransmissionError
{
    switch (error)
    {
    case TransmissionActivationError::session_changed:
        return StartTransmissionError::session_changed_during_start;
    case TransmissionActivationError::membership_changed:
        return StartTransmissionError::membership_changed_during_start;
    case TransmissionActivationError::sender_already_transmitting:
        return StartTransmissionError::sender_already_transmitting;
    case TransmissionActivationError::transmission_id_conflict:
        return StartTransmissionError::transmission_id_conflict;
    }

    throw std::logic_error{"Unhandled transmission activation error."};
}

auto mapAuditRejection(StartTransmissionError error) -> TransmissionAuditRejectionReason
{
    switch (error)
    {
    case StartTransmissionError::session_not_found:
        return TransmissionAuditRejectionReason::session_not_found;
    case StartTransmissionError::session_device_mismatch:
        return TransmissionAuditRejectionReason::session_device_mismatch;
    case StartTransmissionError::session_expired:
        return TransmissionAuditRejectionReason::session_expired;
    case StartTransmissionError::membership_unavailable:
        return TransmissionAuditRejectionReason::membership_unavailable;
    case StartTransmissionError::voice_not_connected:
        return TransmissionAuditRejectionReason::voice_not_connected;
    case StartTransmissionError::voice_no_active_membership:
        return TransmissionAuditRejectionReason::voice_no_active_membership;
    case StartTransmissionError::voice_scope_not_found:
        return TransmissionAuditRejectionReason::voice_scope_not_found;
    case StartTransmissionError::voice_scope_not_authorized:
        return TransmissionAuditRejectionReason::voice_scope_not_authorized;
    case StartTransmissionError::voice_transmit_muted:
        return TransmissionAuditRejectionReason::voice_transmit_muted;
    case StartTransmissionError::voice_membership_stale:
        return TransmissionAuditRejectionReason::voice_membership_stale;
    case StartTransmissionError::session_changed_during_start:
        return TransmissionAuditRejectionReason::session_changed_during_start;
    case StartTransmissionError::membership_changed_during_start:
        return TransmissionAuditRejectionReason::membership_changed_during_start;
    case StartTransmissionError::sender_already_transmitting:
        return TransmissionAuditRejectionReason::sender_already_transmitting;
    case StartTransmissionError::transmission_id_conflict:
        return TransmissionAuditRejectionReason::transmission_id_conflict;
    case StartTransmissionError::rate_limited:
        return TransmissionAuditRejectionReason::rate_limited;
    }

    throw std::logic_error{"Unhandled start transmission error."};
}

auto mapAuditRejection(EndTransmissionError error) -> TransmissionAuditRejectionReason
{
    switch (error)
    {
    case EndTransmissionError::session_not_found:
        return TransmissionAuditRejectionReason::session_not_found;
    case EndTransmissionError::session_device_mismatch:
        return TransmissionAuditRejectionReason::session_device_mismatch;
    case EndTransmissionError::session_expired:
        return TransmissionAuditRejectionReason::session_expired;
    case EndTransmissionError::transmission_not_found:
        return TransmissionAuditRejectionReason::transmission_not_found;
    case EndTransmissionError::transmission_not_owned:
        return TransmissionAuditRejectionReason::transmission_not_owned;
    case EndTransmissionError::rate_limited:
        return TransmissionAuditRejectionReason::rate_limited;
    }

    throw std::logic_error{"Unhandled end transmission error."};
}

auto mapAuditRejection(ModerateTransmissionError error) -> TransmissionAuditRejectionReason
{
    switch (error)
    {
    case ModerateTransmissionError::session_not_found:
        return TransmissionAuditRejectionReason::session_not_found;
    case ModerateTransmissionError::session_device_mismatch:
        return TransmissionAuditRejectionReason::session_device_mismatch;
    case ModerateTransmissionError::session_expired:
        return TransmissionAuditRejectionReason::session_expired;
    case ModerateTransmissionError::not_authorized:
        return TransmissionAuditRejectionReason::not_authorized;
    case ModerateTransmissionError::transmission_not_found:
        return TransmissionAuditRejectionReason::transmission_not_found;
    }

    throw std::logic_error{"Unhandled moderation transmission error."};
}

void addActiveTransmissionDetails(TransmissionAuditEvent& event,
                                  const ActiveTransmission& transmission)
{
    event.session_id = transmission.session_id;
    event.device_id = transmission.device_id;
    event.client_transmission_id = transmission.authorization.client_transmission_id;
    event.transmission_id = transmission.authorization.transmission_id;
    event.sender_player_id = transmission.authorization.sender_player_id;
    event.scope = transmission.authorization.scope;
    event.membership_version = transmission.authorization.membership_version;
    event.recipient_count = transmission.authorization.recipients.size();
}

void recordStartRejected(ITransmissionAuditEventSink* audit_events,
                         const StartTransmissionCommand& command, TimePoint now,
                         StartTransmissionError error,
                         const std::optional<domain::PlayerId>& sender_player_id) noexcept
{
    if (audit_events == nullptr)
    {
        return;
    }

    TransmissionAuditEvent event{TransmissionAuditEventType::rejected,
                                 TransmissionAuditOperation::start, now, command.correlation_id};
    event.session_id = command.session_id;
    event.device_id = command.device_id;
    event.client_transmission_id = command.client_transmission_id;
    event.actor_player_id = sender_player_id;
    event.sender_player_id = sender_player_id;
    event.scope = command.scope;
    event.membership_version = command.membership_version;
    event.rejection_reason = mapAuditRejection(error);
    audit_events->record(event);
}

void recordEndRejected(ITransmissionAuditEventSink* audit_events,
                       const EndTransmissionCommand& command, TimePoint now,
                       EndTransmissionError error,
                       const std::optional<domain::PlayerId>& sender_player_id) noexcept
{
    if (audit_events == nullptr)
    {
        return;
    }

    TransmissionAuditEvent event{TransmissionAuditEventType::rejected,
                                 TransmissionAuditOperation::end, now, command.correlation_id};
    event.session_id = command.session_id;
    event.device_id = command.device_id;
    event.transmission_id = command.transmission_id;
    event.actor_player_id = sender_player_id;
    event.sender_player_id = sender_player_id;
    event.rejection_reason = mapAuditRejection(error);
    audit_events->record(event);
}

void recordModerationRejected(ITransmissionAuditEventSink* audit_events,
                              const ModerateTransmissionCommand& command, TimePoint now,
                              ModerateTransmissionError error,
                              const std::optional<domain::PlayerId>& moderator_player_id) noexcept
{
    if (audit_events == nullptr)
    {
        return;
    }

    TransmissionAuditEvent event{TransmissionAuditEventType::rejected,
                                 TransmissionAuditOperation::moderation, now,
                                 command.correlation_id};
    event.session_id = command.session_id;
    event.device_id = command.device_id;
    event.transmission_id = command.transmission_id;
    event.actor_player_id = moderator_player_id;
    event.rejection_reason = mapAuditRejection(error);
    audit_events->record(event);
}

void recordStarted(ITransmissionAuditEventSink* audit_events,
                   const ActiveTransmission& transmission) noexcept
{
    if (audit_events == nullptr)
    {
        return;
    }

    TransmissionAuditEvent event{TransmissionAuditEventType::started,
                                 TransmissionAuditOperation::start, transmission.started_at,
                                 transmission.authorization.correlation_id};
    addActiveTransmissionDetails(event, transmission);
    event.actor_player_id = transmission.authorization.sender_player_id;
    audit_events->record(event);
}

void recordEnded(ITransmissionAuditEventSink* audit_events,
                 const EndedTransmission& transmission) noexcept
{
    if (audit_events == nullptr)
    {
        return;
    }

    TransmissionAuditEvent event{TransmissionAuditEventType::ended, TransmissionAuditOperation::end,
                                 transmission.ended_at, transmission.correlation_id};
    addActiveTransmissionDetails(event, transmission.transmission);
    event.actor_player_id = transmission.transmission.authorization.sender_player_id;
    event.stop_reason = transmission.stop_reason;
    audit_events->record(event);
}

void recordForcedInterruption(
    ITransmissionAuditEventSink* audit_events, const EndedTransmission& transmission,
    TransmissionAuditOperation operation,
    const std::optional<domain::PlayerId>& actor_player_id = std::nullopt) noexcept
{
    if (audit_events == nullptr)
    {
        return;
    }

    TransmissionAuditEvent event{TransmissionAuditEventType::forcibly_interrupted, operation,
                                 transmission.ended_at, transmission.correlation_id};
    addActiveTransmissionDetails(event, transmission.transmission);
    event.actor_player_id = actor_player_id;
    event.stop_reason = transmission.stop_reason;
    audit_events->record(event);
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

TransmissionActivationResult::TransmissionActivationResult(
    std::optional<ActiveTransmission> active_transmission,
    std::optional<TransmissionActivationError> activation_error)
    : transmission(std::move(active_transmission)), error(activation_error)
{
    if (transmission.has_value() == error.has_value())
    {
        throw std::invalid_argument{
            "A transmission activation result must contain either a transmission or an error."};
    }
}

auto TransmissionActivationResult::activated(ActiveTransmission active_transmission)
    -> TransmissionActivationResult
{
    return TransmissionActivationResult{std::move(active_transmission), std::nullopt};
}

auto TransmissionActivationResult::rejected(TransmissionActivationError activation_error)
    -> TransmissionActivationResult
{
    return TransmissionActivationResult{std::nullopt, activation_error};
}

TransmissionEndRepositoryResult::TransmissionEndRepositoryResult(
    std::optional<EndedTransmission> ended_transmission,
    std::optional<TransmissionEndRepositoryError> repository_error)
    : transmission(std::move(ended_transmission)), error(repository_error)
{
    if (transmission.has_value() == error.has_value())
    {
        throw std::invalid_argument{
            "A transmission end result must contain either a transmission or an error."};
    }
}

auto TransmissionEndRepositoryResult::ended(EndedTransmission ended_transmission)
    -> TransmissionEndRepositoryResult
{
    return TransmissionEndRepositoryResult{std::move(ended_transmission), std::nullopt};
}

auto TransmissionEndRepositoryResult::rejected(TransmissionEndRepositoryError repository_error)
    -> TransmissionEndRepositoryResult
{
    return TransmissionEndRepositoryResult{std::nullopt, repository_error};
}

StartTransmissionResult::StartTransmissionResult(
    std::optional<ActiveTransmission> active_transmission,
    std::optional<StartTransmissionError> start_error)
    : transmission(std::move(active_transmission)), error(start_error)
{
    if (transmission.has_value() == error.has_value())
    {
        throw std::invalid_argument{
            "A start transmission result must contain either a transmission or an error."};
    }
}

auto StartTransmissionResult::started(ActiveTransmission active_transmission)
    -> StartTransmissionResult
{
    return StartTransmissionResult{std::move(active_transmission), std::nullopt};
}

auto StartTransmissionResult::rejected(StartTransmissionError start_error)
    -> StartTransmissionResult
{
    return StartTransmissionResult{std::nullopt, start_error};
}

EndTransmissionResult::EndTransmissionResult(std::optional<EndedTransmission> ended_transmission,
                                             std::optional<EndTransmissionError> end_error)
    : transmission(std::move(ended_transmission)), error(end_error)
{
    if (transmission.has_value() == error.has_value())
    {
        throw std::invalid_argument{
            "An end transmission result must contain either a transmission or an error."};
    }
}

auto EndTransmissionResult::ended(EndedTransmission ended_transmission) -> EndTransmissionResult
{
    return EndTransmissionResult{std::move(ended_transmission), std::nullopt};
}

auto EndTransmissionResult::rejected(EndTransmissionError end_error) -> EndTransmissionResult
{
    return EndTransmissionResult{std::nullopt, end_error};
}

ModerateTransmissionResult::ModerateTransmissionResult(
    std::optional<EndedTransmission> ended_transmission,
    std::optional<ModerateTransmissionError> moderation_error)
    : transmission(std::move(ended_transmission)), error(moderation_error)
{
    if (transmission.has_value() == error.has_value())
    {
        throw std::invalid_argument{
            "A moderation result must contain either an interrupted transmission or an error."};
    }
}

auto ModerateTransmissionResult::interrupted(EndedTransmission ended_transmission)
    -> ModerateTransmissionResult
{
    return ModerateTransmissionResult{std::move(ended_transmission), std::nullopt};
}

auto ModerateTransmissionResult::rejected(ModerateTransmissionError moderation_error)
    -> ModerateTransmissionResult
{
    return ModerateTransmissionResult{std::nullopt, moderation_error};
}

TransmissionLifecyclePolicy::TransmissionLifecyclePolicy(std::chrono::milliseconds maximum_duration)
    : maximum_transmission_duration(maximum_duration)
{
    if (maximum_transmission_duration <= std::chrono::milliseconds::zero())
    {
        throw std::invalid_argument{"The maximum transmission duration must be positive."};
    }
}

TransmissionApplicationService::TransmissionApplicationService(
    const ISessionRepository& sessions, const IAuthoritativeMembershipProvider& memberships,
    ITransmissionIdGenerator& transmission_ids, IActiveTransmissionRepository& active_transmissions,
    ITransmissionRateLimiter& rate_limiter,
    const ITransmissionModerationAuthorizer& moderation_authorizer,
    TransmissionLifecyclePolicy lifecycle_policy, ITransmissionAuditEventSink* audit_events)
    : sessions_(sessions), authorization_(sessions, memberships, transmission_ids),
      active_transmissions_(active_transmissions), rate_limiter_(rate_limiter),
      moderation_authorizer_(moderation_authorizer), lifecycle_policy_(std::move(lifecycle_policy)),
      audit_events_(audit_events)
{
}

auto TransmissionApplicationService::start(const StartTransmissionCommand& command, TimePoint now)
    -> StartTransmissionResult
{
    std::optional<domain::PlayerId> sender_player_id;
    const auto reject = [&](StartTransmissionError error) {
        recordStartRejected(audit_events_, command, now, error, sender_player_id);
        return StartTransmissionResult::rejected(error);
    };

    const auto session = sessions_.find(command.session_id);
    if (!session)
    {
        return reject(StartTransmissionError::session_not_found);
    }
    sender_player_id = session->player_id;
    if (session->device_id != command.device_id)
    {
        return reject(StartTransmissionError::session_device_mismatch);
    }
    if (!session->activeAt(now))
    {
        return reject(StartTransmissionError::session_expired);
    }
    if (!rate_limiter_.allow(session->player_id, TransmissionRateLimitAction::start, now))
    {
        return reject(StartTransmissionError::rate_limited);
    }

    auto authorization = authorization_.authorizeStart(command, now);
    if (!authorization.authorized())
    {
        return reject(mapAuthorizationError(*authorization.error));
    }

    auto activation = active_transmissions_.activate(std::move(*authorization.transmission),
                                                     command.session_id, command.device_id, now);
    if (!activation.active())
    {
        return reject(mapActivationError(*activation.error));
    }

    recordStarted(audit_events_, *activation.transmission);
    return StartTransmissionResult::started(std::move(*activation.transmission));
}

auto TransmissionApplicationService::end(const EndTransmissionCommand& command, TimePoint now)
    -> EndTransmissionResult
{
    std::optional<domain::PlayerId> sender_player_id;
    const auto reject = [&](EndTransmissionError error) {
        recordEndRejected(audit_events_, command, now, error, sender_player_id);
        return EndTransmissionResult::rejected(error);
    };

    const auto session = sessions_.find(command.session_id);
    if (!session)
    {
        return reject(EndTransmissionError::session_not_found);
    }
    sender_player_id = session->player_id;
    if (session->device_id != command.device_id)
    {
        return reject(EndTransmissionError::session_device_mismatch);
    }
    if (!session->activeAt(now))
    {
        return reject(EndTransmissionError::session_expired);
    }
    if (!rate_limiter_.allow(session->player_id, TransmissionRateLimitAction::end, now))
    {
        return reject(EndTransmissionError::rate_limited);
    }

    auto ended = active_transmissions_.end(
        command.transmission_id, command.session_id, command.device_id,
        domain::TransmissionStopReason::push_to_talk_released, now, command.correlation_id);
    if (!ended.successful())
    {
        const auto error = *ended.error == TransmissionEndRepositoryError::transmission_not_found
                               ? EndTransmissionError::transmission_not_found
                               : EndTransmissionError::transmission_not_owned;
        return reject(error);
    }

    recordEnded(audit_events_, *ended.transmission);
    return EndTransmissionResult::ended(std::move(*ended.transmission));
}

auto TransmissionApplicationService::interruptForModeration(
    const ModerateTransmissionCommand& command, TimePoint now) -> ModerateTransmissionResult
{
    std::optional<domain::PlayerId> moderator_player_id;
    const auto reject = [&](ModerateTransmissionError error) {
        recordModerationRejected(audit_events_, command, now, error, moderator_player_id);
        return ModerateTransmissionResult::rejected(error);
    };

    const auto session = sessions_.find(command.session_id);
    if (!session)
    {
        return reject(ModerateTransmissionError::session_not_found);
    }
    moderator_player_id = session->player_id;
    if (session->device_id != command.device_id)
    {
        return reject(ModerateTransmissionError::session_device_mismatch);
    }
    if (!session->activeAt(now))
    {
        return reject(ModerateTransmissionError::session_expired);
    }
    if (!moderation_authorizer_.canInterrupt(session->player_id, command.transmission_id))
    {
        return reject(ModerateTransmissionError::not_authorized);
    }

    auto interrupted = active_transmissions_.interrupt(
        command.transmission_id, domain::TransmissionStopReason::moderation_interrupted, now,
        command.correlation_id);
    if (!interrupted.successful())
    {
        return reject(ModerateTransmissionError::transmission_not_found);
    }

    recordForcedInterruption(audit_events_, *interrupted.transmission,
                             TransmissionAuditOperation::moderation, moderator_player_id);
    return ModerateTransmissionResult::interrupted(std::move(*interrupted.transmission));
}

auto TransmissionApplicationService::expireTimedOut(TimePoint now,
                                                    const domain::CorrelationId& correlation_id)
    -> std::vector<EndedTransmission>
{
    auto expired = active_transmissions_.expireTimedOut(
        lifecycle_policy_.maximum_transmission_duration, now, correlation_id);
    for (const auto& transmission : expired)
    {
        recordForcedInterruption(audit_events_, transmission, TransmissionAuditOperation::timeout);
    }
    return expired;
}
} // namespace hvc::application
