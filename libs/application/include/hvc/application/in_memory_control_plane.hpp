#pragma once

#include <cstddef>
#include <deque>
#include <hvc/application/control_plane.hpp>
#include <map>
#include <mutex>

namespace hvc::application
{
/// Identify which part of an authoritative context changed.
enum class AuthoritativeContextChange : std::uint8_t
{
    /// Hierarchy placement or membership data changed.
    membership_changed,
    /// Role-derived transmit or receive permissions changed.
    permissions_changed
};

/**
 * Coordinate sessions, memberships, and active transmissions in memory.
 *
 * All compound validation and mutation operations are serialized by one mutex
 * so state cannot change between authorization and activation. Optional
 * persistent repositories remain authoritative across process restarts.
 */
class InMemoryControlPlaneStore final : public ISessionRepository,
                                        public IAdministrativeMembershipService,
                                        public IActiveTransmissionRepository
{
  public:
    /**
     * Construct a purely in-memory store.
     *
     * @param audit_events Optional non-owning audit sink.
     */
    explicit InMemoryControlPlaneStore(
        ITransmissionAuditEventSink* audit_events = nullptr) noexcept;
    /**
     * Construct a store backed by persistent memberships.
     *
     * @param persistent_memberships Repository that must outlive the store.
     * @param audit_events Optional non-owning audit sink.
     */
    explicit InMemoryControlPlaneStore(
        IMutableAuthoritativeMembershipRepository& persistent_memberships,
        ITransmissionAuditEventSink* audit_events = nullptr) noexcept;
    /**
     * Construct a store backed by persistent sessions and memberships.
     *
     * @param persistent_sessions Session repository that must outlive the store.
     * @param persistent_memberships Membership repository that must outlive the
     *     store.
     * @param audit_events Optional non-owning audit sink.
     */
    InMemoryControlPlaneStore(const ISessionRepository& persistent_sessions,
                              IMutableAuthoritativeMembershipRepository& persistent_memberships,
                              ITransmissionAuditEventSink* audit_events = nullptr) noexcept;

    /**
     * Insert or replace a cached authenticated session.
     *
     * @param session Session keyed by its identifier.
     */
    void upsertSession(AuthenticatedSession session);
    /**
     * Remove a session and interrupt every transmission it owns.
     *
     * @param session_id Session to remove.
     * @param now Authoritative removal time.
     * @param correlation_id Correlation identifier for generated audit events.
     * @returns Transmissions interrupted by the removal.
     */
    [[nodiscard]] auto removeSession(const domain::SessionId& session_id, TimePoint now,
                                     const domain::CorrelationId& correlation_id)
        -> std::vector<EndedTransmission>;

    /// @copydoc ISessionRepository::find
    [[nodiscard]] auto find(const domain::SessionId& session_id) const
        -> std::optional<AuthenticatedSession> override;

    /**
     * Atomically replace authoritative context and interrupt affected voice.
     *
     * @param player_id Participant whose context is changing.
     * @param context New authoritative snapshot and role policy.
     * @param now Authoritative update time.
     * @param correlation_id Correlation identifier for generated audit events.
     * @param change Whether membership placement or permissions changed.
     * @returns The update outcome and any interrupted transmissions.
     */
    [[nodiscard]] auto updateMembership(const domain::PlayerId& player_id,
                                        AuthoritativeMembershipContext context, TimePoint now,
                                        const domain::CorrelationId& correlation_id,
                                        AuthoritativeContextChange change)
        -> MembershipUpdateResult;
    /// @copydoc IAdministrativeMembershipService::replaceMembership
    [[nodiscard]] auto replaceMembership(const domain::PlayerId& player_id,
                                         AuthoritativeMembershipContext context, TimePoint now,
                                         const domain::CorrelationId& correlation_id)
        -> MembershipUpdateResult override;
    /// @copydoc IAdministrativeMembershipService::removeMembership
    [[nodiscard]] auto removeMembership(const domain::PlayerId& player_id, TimePoint now,
                                        const domain::CorrelationId& correlation_id)
        -> std::vector<EndedTransmission> override;

    /// @copydoc IAuthoritativeMembershipProvider::currentFor
    [[nodiscard]] auto currentFor(const domain::PlayerId& player_id) const
        -> std::optional<AuthoritativeMembershipContext> override;

    /// @copydoc IActiveTransmissionRepository::activate
    [[nodiscard]] auto activate(AuthorizedTransmission transmission,
                                const domain::SessionId& session_id,
                                const domain::DeviceId& device_id, TimePoint started_at)
        -> TransmissionActivationResult override;
    /// @copydoc IActiveTransmissionRepository::end
    [[nodiscard]] auto end(const domain::TransmissionId& transmission_id,
                           const domain::SessionId& session_id, const domain::DeviceId& device_id,
                           domain::TransmissionStopReason stop_reason, TimePoint ended_at,
                           const domain::CorrelationId& correlation_id)
        -> TransmissionEndRepositoryResult override;
    /// @copydoc IActiveTransmissionRepository::interrupt
    [[nodiscard]] auto interrupt(const domain::TransmissionId& transmission_id,
                                 domain::TransmissionStopReason stop_reason, TimePoint ended_at,
                                 const domain::CorrelationId& correlation_id)
        -> TransmissionEndRepositoryResult override;
    /// @copydoc IActiveTransmissionRepository::expireTimedOut
    [[nodiscard]] auto expireTimedOut(std::chrono::milliseconds maximum_duration, TimePoint now,
                                      const domain::CorrelationId& correlation_id)
        -> std::vector<EndedTransmission> override;

    /**
     * Find an active transmission by identifier.
     *
     * @param transmission_id Transmission to find.
     * @returns A snapshot of the active transmission, or no value when absent.
     */
    [[nodiscard]] auto active(const domain::TransmissionId& transmission_id) const
        -> std::optional<ActiveTransmission>;
    /**
     * Return the number of active transmissions.
     *
     * @returns Current number of visible active transmissions.
     */
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

/// Configure a maximum request count within a rolling time window.
struct TransmissionRateLimit final
{
    /**
     * Construct a rate limit.
     *
     * @param maximum_request_count Positive number of allowed requests.
     * @param time_window Positive rolling window duration.
     * @throws std::invalid_argument Thrown when either value is not positive.
     */
    TransmissionRateLimit(std::size_t maximum_request_count, std::chrono::milliseconds time_window);

    /// Maximum requests accepted in the rolling window.
    std::size_t maximum_requests;
    /// Duration of the rolling request window.
    std::chrono::milliseconds window;
};

/// Apply independent in-memory rolling limits to transmission starts and ends.
class InMemoryTransmissionRateLimiter final : public ITransmissionRateLimiter
{
  public:
    /**
     * Construct a rate limiter.
     *
     * @param start_limit Limit applied to start operations.
     * @param end_limit Limit applied to end operations.
     */
    InMemoryTransmissionRateLimiter(TransmissionRateLimit start_limit,
                                    TransmissionRateLimit end_limit);

    /// @copydoc ITransmissionRateLimiter::allow
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
