#include <hvc/application/in_memory_control_plane.hpp>
#include <stdexcept>

namespace hvc::application
{
namespace
{
void discardExpiredRequests(std::deque<TimePoint>& requests, TimePoint now,
                            std::chrono::milliseconds window)
{
    const auto oldest_allowed = now - window;
    while (!requests.empty() && requests.front() <= oldest_allowed)
    {
        requests.pop_front();
    }
}
} // namespace

MembershipUpdateResult::MembershipUpdateResult(
    std::vector<EndedTransmission> interrupted_transmissions,
    std::optional<MembershipUpdateError> update_error)
    : interrupted(std::move(interrupted_transmissions)), error(update_error)
{
    if (error && !interrupted.empty())
    {
        throw std::invalid_argument{
            "A rejected membership update must not interrupt transmissions."};
    }
}

auto MembershipUpdateResult::updated(std::vector<EndedTransmission> interrupted_transmissions)
    -> MembershipUpdateResult
{
    return MembershipUpdateResult{std::move(interrupted_transmissions), std::nullopt};
}

auto MembershipUpdateResult::rejected(MembershipUpdateError update_error) -> MembershipUpdateResult
{
    return MembershipUpdateResult{{}, update_error};
}

InMemoryControlPlaneStore::InMemoryControlPlaneStore(
    ITransmissionAuditEventSink* audit_events) noexcept
    : audit_events_(audit_events)
{
}

void InMemoryControlPlaneStore::upsertSession(AuthenticatedSession session)
{
    std::scoped_lock lock{mutex_};
    const auto session_id = session.session_id;
    sessions_.insert_or_assign(session_id, std::move(session));
}

auto InMemoryControlPlaneStore::removeSession(const domain::SessionId& session_id, TimePoint now,
                                              const domain::CorrelationId& correlation_id)
    -> std::vector<EndedTransmission>
{
    std::vector<EndedTransmission> interrupted;
    {
        std::scoped_lock lock{mutex_};
        const auto session = sessions_.find(session_id);
        if (session == sessions_.end())
        {
            return {};
        }

        sessions_.erase(session);
        interrupted = interruptForSessionLocked(
            session_id, domain::TransmissionStopReason::disconnected, now, correlation_id);
    }
    recordForcedInterruptions(interrupted, TransmissionAuditOperation::session_lifecycle);
    return interrupted;
}

auto InMemoryControlPlaneStore::find(const domain::SessionId& session_id) const
    -> std::optional<AuthenticatedSession>
{
    std::scoped_lock lock{mutex_};
    const auto session = sessions_.find(session_id);
    if (session == sessions_.end())
    {
        return std::nullopt;
    }
    return session->second;
}

auto InMemoryControlPlaneStore::updateMembership(const domain::PlayerId& player_id,
                                                 AuthoritativeMembershipContext context,
                                                 TimePoint now,
                                                 const domain::CorrelationId& correlation_id,
                                                 AuthoritativeContextChange change)
    -> MembershipUpdateResult
{
    std::vector<EndedTransmission> interrupted;
    {
        std::scoped_lock lock{mutex_};
        if (context.snapshot->find(player_id) == nullptr)
        {
            return MembershipUpdateResult::rejected(MembershipUpdateError::player_not_in_snapshot);
        }

        const auto existing = memberships_.find(player_id);
        if (existing != memberships_.end() &&
            context.snapshot->version() <= existing->second.snapshot->version())
        {
            return MembershipUpdateResult::rejected(MembershipUpdateError::version_not_newer);
        }

        const auto reason = change == AuthoritativeContextChange::permissions_changed
                                ? domain::TransmissionStopReason::permission_revoked
                                : domain::TransmissionStopReason::membership_changed;
        interrupted = interruptForPlayerLocked(player_id, reason, now, correlation_id);
        memberships_.insert_or_assign(player_id, std::move(context));
    }
    recordForcedInterruptions(interrupted, TransmissionAuditOperation::membership_change);
    return MembershipUpdateResult::updated(std::move(interrupted));
}

auto InMemoryControlPlaneStore::removeMembership(const domain::PlayerId& player_id, TimePoint now,
                                                 const domain::CorrelationId& correlation_id)
    -> std::vector<EndedTransmission>
{
    std::vector<EndedTransmission> interrupted;
    {
        std::scoped_lock lock{mutex_};
        memberships_.erase(player_id);
        interrupted = interruptForPlayerLocked(
            player_id, domain::TransmissionStopReason::membership_changed, now, correlation_id);
    }
    recordForcedInterruptions(interrupted, TransmissionAuditOperation::membership_change);
    return interrupted;
}

auto InMemoryControlPlaneStore::currentFor(const domain::PlayerId& player_id) const
    -> std::optional<AuthoritativeMembershipContext>
{
    std::scoped_lock lock{mutex_};
    const auto membership = memberships_.find(player_id);
    if (membership == memberships_.end())
    {
        return std::nullopt;
    }
    return membership->second;
}

auto InMemoryControlPlaneStore::activate(AuthorizedTransmission transmission,
                                         const domain::SessionId& session_id,
                                         const domain::DeviceId& device_id, TimePoint started_at)
    -> TransmissionActivationResult
{
    std::scoped_lock lock{mutex_};

    const auto session = sessions_.find(session_id);
    if (session == sessions_.end() || session->second.player_id != transmission.sender_player_id ||
        session->second.device_id != device_id || !session->second.activeAt(started_at))
    {
        return TransmissionActivationResult::rejected(TransmissionActivationError::session_changed);
    }

    const auto membership = memberships_.find(transmission.sender_player_id);
    if (membership == memberships_.end() ||
        membership->second.snapshot->version() != transmission.membership_version)
    {
        return TransmissionActivationResult::rejected(
            TransmissionActivationError::membership_changed);
    }

    for (const auto& [id, active_transmission] : active_transmissions_)
    {
        static_cast<void>(id);
        if (active_transmission.authorization.sender_player_id == transmission.sender_player_id)
        {
            return TransmissionActivationResult::rejected(
                TransmissionActivationError::sender_already_transmitting);
        }
    }

    if (active_transmissions_.contains(transmission.transmission_id))
    {
        return TransmissionActivationResult::rejected(
            TransmissionActivationError::transmission_id_conflict);
    }

    ActiveTransmission active_transmission{std::move(transmission), session_id, device_id,
                                           started_at};
    active_transmissions_.emplace(active_transmission.authorization.transmission_id,
                                  active_transmission);
    return TransmissionActivationResult::activated(std::move(active_transmission));
}

auto InMemoryControlPlaneStore::end(const domain::TransmissionId& transmission_id,
                                    const domain::SessionId& session_id,
                                    const domain::DeviceId& device_id,
                                    domain::TransmissionStopReason stop_reason, TimePoint ended_at,
                                    const domain::CorrelationId& correlation_id)
    -> TransmissionEndRepositoryResult
{
    std::scoped_lock lock{mutex_};
    const auto transmission = active_transmissions_.find(transmission_id);
    if (transmission == active_transmissions_.end())
    {
        return TransmissionEndRepositoryResult::rejected(
            TransmissionEndRepositoryError::transmission_not_found);
    }
    if (transmission->second.session_id != session_id ||
        transmission->second.device_id != device_id)
    {
        return TransmissionEndRepositoryResult::rejected(
            TransmissionEndRepositoryError::transmission_not_owned);
    }

    EndedTransmission ended_transmission{transmission->second, stop_reason, ended_at,
                                         correlation_id};
    active_transmissions_.erase(transmission);
    return TransmissionEndRepositoryResult::ended(std::move(ended_transmission));
}

auto InMemoryControlPlaneStore::interrupt(const domain::TransmissionId& transmission_id,
                                          domain::TransmissionStopReason stop_reason,
                                          TimePoint ended_at,
                                          const domain::CorrelationId& correlation_id)
    -> TransmissionEndRepositoryResult
{
    std::scoped_lock lock{mutex_};
    const auto transmission = active_transmissions_.find(transmission_id);
    if (transmission == active_transmissions_.end())
    {
        return TransmissionEndRepositoryResult::rejected(
            TransmissionEndRepositoryError::transmission_not_found);
    }

    EndedTransmission ended_transmission{transmission->second, stop_reason, ended_at,
                                         correlation_id};
    active_transmissions_.erase(transmission);
    return TransmissionEndRepositoryResult::ended(std::move(ended_transmission));
}

auto InMemoryControlPlaneStore::expireTimedOut(std::chrono::milliseconds maximum_duration,
                                               TimePoint now,
                                               const domain::CorrelationId& correlation_id)
    -> std::vector<EndedTransmission>
{
    std::scoped_lock lock{mutex_};
    std::vector<EndedTransmission> expired;
    for (auto transmission = active_transmissions_.begin();
         transmission != active_transmissions_.end();)
    {
        if (now - transmission->second.started_at < maximum_duration)
        {
            ++transmission;
            continue;
        }

        expired.emplace_back(transmission->second, domain::TransmissionStopReason::timed_out, now,
                             correlation_id);
        transmission = active_transmissions_.erase(transmission);
    }
    return expired;
}

auto InMemoryControlPlaneStore::active(const domain::TransmissionId& transmission_id) const
    -> std::optional<ActiveTransmission>
{
    std::scoped_lock lock{mutex_};
    const auto transmission = active_transmissions_.find(transmission_id);
    if (transmission == active_transmissions_.end())
    {
        return std::nullopt;
    }
    return transmission->second;
}

auto InMemoryControlPlaneStore::activeCount() const -> std::size_t
{
    std::scoped_lock lock{mutex_};
    return active_transmissions_.size();
}

auto InMemoryControlPlaneStore::interruptForPlayerLocked(
    const domain::PlayerId& player_id, domain::TransmissionStopReason stop_reason,
    TimePoint ended_at, const domain::CorrelationId& correlation_id)
    -> std::vector<EndedTransmission>
{
    std::vector<EndedTransmission> interrupted;
    for (auto transmission = active_transmissions_.begin();
         transmission != active_transmissions_.end();)
    {
        if (transmission->second.authorization.sender_player_id != player_id)
        {
            ++transmission;
            continue;
        }

        interrupted.emplace_back(transmission->second, stop_reason, ended_at, correlation_id);
        transmission = active_transmissions_.erase(transmission);
    }
    return interrupted;
}

auto InMemoryControlPlaneStore::interruptForSessionLocked(
    const domain::SessionId& session_id, domain::TransmissionStopReason stop_reason,
    TimePoint ended_at, const domain::CorrelationId& correlation_id)
    -> std::vector<EndedTransmission>
{
    std::vector<EndedTransmission> interrupted;
    for (auto transmission = active_transmissions_.begin();
         transmission != active_transmissions_.end();)
    {
        if (transmission->second.session_id != session_id)
        {
            ++transmission;
            continue;
        }

        interrupted.emplace_back(transmission->second, stop_reason, ended_at, correlation_id);
        transmission = active_transmissions_.erase(transmission);
    }
    return interrupted;
}

void InMemoryControlPlaneStore::recordForcedInterruptions(
    const std::vector<EndedTransmission>& interrupted_transmissions,
    TransmissionAuditOperation operation) const noexcept
{
    if (audit_events_ == nullptr)
    {
        return;
    }

    for (const auto& ended : interrupted_transmissions)
    {
        TransmissionAuditEvent event{TransmissionAuditEventType::forcibly_interrupted, operation,
                                     ended.ended_at, ended.correlation_id};
        const auto& active = ended.transmission;
        event.session_id = active.session_id;
        event.device_id = active.device_id;
        event.client_transmission_id = active.authorization.client_transmission_id;
        event.transmission_id = active.authorization.transmission_id;
        event.sender_player_id = active.authorization.sender_player_id;
        event.scope = active.authorization.scope;
        event.membership_version = active.authorization.membership_version;
        event.recipient_count = active.authorization.recipients.size();
        event.stop_reason = ended.stop_reason;
        audit_events_->record(event);
    }
}

TransmissionRateLimit::TransmissionRateLimit(std::size_t maximum_request_count,
                                             std::chrono::milliseconds time_window)
    : maximum_requests(maximum_request_count), window(time_window)
{
    if (maximum_requests == 0 || window <= std::chrono::milliseconds::zero())
    {
        throw std::invalid_argument{
            "A transmission rate limit requires a positive request count and time window."};
    }
}

InMemoryTransmissionRateLimiter::InMemoryTransmissionRateLimiter(TransmissionRateLimit start_limit,
                                                                 TransmissionRateLimit end_limit)
    : start_limit_(start_limit), end_limit_(end_limit)
{
}

auto InMemoryTransmissionRateLimiter::allow(const domain::PlayerId& player_id,
                                            TransmissionRateLimitAction action, TimePoint now)
    -> bool
{
    std::scoped_lock lock{mutex_};
    auto& history = histories_[player_id];
    auto& requests = action == TransmissionRateLimitAction::start ? history.starts : history.ends;
    const auto& limit = action == TransmissionRateLimitAction::start ? start_limit_ : end_limit_;

    discardExpiredRequests(requests, now, limit.window);
    if (requests.size() >= limit.maximum_requests)
    {
        return false;
    }

    requests.push_back(now);
    return true;
}
} // namespace hvc::application
