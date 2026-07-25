#pragma once

#include <cstddef>
#include <deque>
#include <hvc/application/control_plane.hpp>
#include <map>
#include <mutex>

namespace hvc::application
{
enum class AuthoritativeContextChange : std::uint8_t
{
    membership_changed,
    permissions_changed
};

using MembershipUpdateError = AuthoritativeMembershipWriteError;

struct MembershipUpdateResult final
{
    [[nodiscard]] static auto updated(std::vector<EndedTransmission> interrupted_transmissions)
        -> MembershipUpdateResult;
    [[nodiscard]] static auto rejected(MembershipUpdateError update_error)
        -> MembershipUpdateResult;

    [[nodiscard]] auto successful() const noexcept -> bool
    {
        return !error.has_value();
    }

    std::vector<EndedTransmission> interrupted;
    std::optional<MembershipUpdateError> error;

  private:
    MembershipUpdateResult(std::vector<EndedTransmission> interrupted_transmissions,
                           std::optional<MembershipUpdateError> update_error);
};

class InMemoryControlPlaneStore final : public ISessionRepository,
                                        public IAuthoritativeMembershipProvider,
                                        public IActiveTransmissionRepository
{
  public:
    explicit InMemoryControlPlaneStore(
        ITransmissionAuditEventSink* audit_events = nullptr) noexcept;
    explicit InMemoryControlPlaneStore(
        IMutableAuthoritativeMembershipRepository& persistent_memberships,
        ITransmissionAuditEventSink* audit_events = nullptr) noexcept;
    InMemoryControlPlaneStore(const ISessionRepository& persistent_sessions,
                              IMutableAuthoritativeMembershipRepository& persistent_memberships,
                              ITransmissionAuditEventSink* audit_events = nullptr) noexcept;

    void upsertSession(AuthenticatedSession session);
    [[nodiscard]] auto removeSession(const domain::SessionId& session_id, TimePoint now,
                                     const domain::CorrelationId& correlation_id)
        -> std::vector<EndedTransmission>;

    [[nodiscard]] auto find(const domain::SessionId& session_id) const
        -> std::optional<AuthenticatedSession> override;

    [[nodiscard]] auto updateMembership(const domain::PlayerId& player_id,
                                        AuthoritativeMembershipContext context, TimePoint now,
                                        const domain::CorrelationId& correlation_id,
                                        AuthoritativeContextChange change)
        -> MembershipUpdateResult;
    [[nodiscard]] auto removeMembership(const domain::PlayerId& player_id, TimePoint now,
                                        const domain::CorrelationId& correlation_id)
        -> std::vector<EndedTransmission>;

    [[nodiscard]] auto currentFor(const domain::PlayerId& player_id) const
        -> std::optional<AuthoritativeMembershipContext> override;

    [[nodiscard]] auto activate(AuthorizedTransmission transmission,
                                const domain::SessionId& session_id,
                                const domain::DeviceId& device_id, TimePoint started_at)
        -> TransmissionActivationResult override;
    [[nodiscard]] auto end(const domain::TransmissionId& transmission_id,
                           const domain::SessionId& session_id, const domain::DeviceId& device_id,
                           domain::TransmissionStopReason stop_reason, TimePoint ended_at,
                           const domain::CorrelationId& correlation_id)
        -> TransmissionEndRepositoryResult override;
    [[nodiscard]] auto interrupt(const domain::TransmissionId& transmission_id,
                                 domain::TransmissionStopReason stop_reason, TimePoint ended_at,
                                 const domain::CorrelationId& correlation_id)
        -> TransmissionEndRepositoryResult override;
    [[nodiscard]] auto expireTimedOut(std::chrono::milliseconds maximum_duration, TimePoint now,
                                      const domain::CorrelationId& correlation_id)
        -> std::vector<EndedTransmission> override;

    [[nodiscard]] auto active(const domain::TransmissionId& transmission_id) const
        -> std::optional<ActiveTransmission>;
    [[nodiscard]] auto activeCount() const -> std::size_t;

  private:
    [[nodiscard]] auto interruptForPlayerLocked(const domain::PlayerId& player_id,
                                                domain::TransmissionStopReason stop_reason,
                                                TimePoint ended_at,
                                                const domain::CorrelationId& correlation_id)
        -> std::vector<EndedTransmission>;
    [[nodiscard]] auto interruptForSessionLocked(const domain::SessionId& session_id,
                                                 domain::TransmissionStopReason stop_reason,
                                                 TimePoint ended_at,
                                                 const domain::CorrelationId& correlation_id)
        -> std::vector<EndedTransmission>;
    void recordForcedInterruptions(const std::vector<EndedTransmission>& interrupted_transmissions,
                                   TransmissionAuditOperation operation) const noexcept;
    [[nodiscard]] auto currentMembershipLocked(const domain::PlayerId& player_id) const
        -> std::optional<AuthoritativeMembershipContext>;
    [[nodiscard]] auto currentSessionLocked(const domain::SessionId& session_id) const
        -> std::optional<AuthenticatedSession>;

    mutable std::mutex mutex_;
    ITransmissionAuditEventSink* audit_events_;
    const ISessionRepository* persistent_sessions_{nullptr};
    IMutableAuthoritativeMembershipRepository* persistent_memberships_{nullptr};
    std::map<domain::SessionId, AuthenticatedSession> sessions_;
    std::map<domain::PlayerId, AuthoritativeMembershipContext> memberships_;
    std::map<domain::TransmissionId, ActiveTransmission> active_transmissions_;
};

struct TransmissionRateLimit final
{
    TransmissionRateLimit(std::size_t maximum_request_count, std::chrono::milliseconds time_window);

    std::size_t maximum_requests;
    std::chrono::milliseconds window;
};

class InMemoryTransmissionRateLimiter final : public ITransmissionRateLimiter
{
  public:
    InMemoryTransmissionRateLimiter(TransmissionRateLimit start_limit,
                                    TransmissionRateLimit end_limit);

    [[nodiscard]] auto allow(const domain::PlayerId& player_id, TransmissionRateLimitAction action,
                             TimePoint now) -> bool override;

  private:
    struct RequestHistory final
    {
        std::deque<TimePoint> starts;
        std::deque<TimePoint> ends;
    };

    mutable std::mutex mutex_;
    TransmissionRateLimit start_limit_;
    TransmissionRateLimit end_limit_;
    std::map<domain::PlayerId, RequestHistory> histories_;
};
} // namespace hvc::application
