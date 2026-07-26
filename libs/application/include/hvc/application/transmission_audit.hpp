#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <hvc/domain/id.hpp>
#include <hvc/domain/model.hpp>
#include <hvc/domain/state_machine.hpp>
#include <optional>
#include <utility>

namespace hvc::application
{
/// Classify the state change represented by a transmission audit event.
enum class TransmissionAuditEventType : std::uint8_t
{
    /// A transmission became active.
    started,
    /// A transmission ended normally.
    ended,
    /// An attempted operation was rejected.
    rejected,
    /// An active transmission was interrupted by server policy.
    forcibly_interrupted
};

/// Identify the application operation that produced an audit event.
enum class TransmissionAuditOperation : std::uint8_t
{
    /// A participant attempted to start transmission.
    start,
    /// A participant attempted to end transmission.
    end,
    /// A moderator attempted to interrupt transmission.
    moderation,
    /// The timeout sweep expired a transmission.
    timeout,
    /// Session removal interrupted related transmission state.
    session_lifecycle,
    /// Membership or permission changes interrupted related transmission state.
    membership_change
};

/// Classify why an audited transmission operation was rejected.
enum class TransmissionAuditRejectionReason : std::uint8_t
{
    /// The referenced session does not exist.
    session_not_found,
    /// The request device differs from the session-bound device.
    session_device_mismatch,
    /// The referenced session has expired.
    session_expired,
    /// No authoritative membership context is available.
    membership_unavailable,
    /// The sender has no connected voice session.
    voice_not_connected,
    /// The sender is absent from the membership snapshot.
    voice_no_active_membership,
    /// The requested voice scope is undefined.
    voice_scope_not_found,
    /// The sender lacks permission for the requested scope.
    voice_scope_not_authorized,
    /// The sender is muted or banned.
    voice_transmit_muted,
    /// The client used an obsolete membership version.
    voice_membership_stale,
    /// Session state changed between authorization and activation.
    session_changed_during_start,
    /// Membership changed between authorization and activation.
    membership_changed_during_start,
    /// The sender already has an active transmission.
    sender_already_transmitting,
    /// The generated transmission identifier is already active.
    transmission_id_conflict,
    /// The referenced transmission does not exist.
    transmission_not_found,
    /// The referenced transmission belongs to another session or device.
    transmission_not_owned,
    /// The actor lacks administrative authorization.
    not_authorized,
    /// The operation exceeded its configured rate limit.
    rate_limited
};

/**
 * Record privacy-preserving metadata for one transmission lifecycle event.
 *
 * Audit events deliberately contain recipient counts but never recipient lists
 * or voice content.
 */
struct TransmissionAuditEvent final
{
    /**
     * Construct the required audit-event envelope.
     *
     * @param event_type State change represented by the event.
     * @param audited_operation Operation that produced the event.
     * @param event_time Authoritative occurrence time.
     * @param correlation Correlation identifier supplied by the request.
     */
    TransmissionAuditEvent(TransmissionAuditEventType event_type,
                           TransmissionAuditOperation audited_operation,
                           std::chrono::system_clock::time_point event_time,
                           domain::CorrelationId correlation)
        : type(event_type), operation(audited_operation), occurred_at(event_time),
          correlation_id(std::move(correlation))
    {
    }

    /// State change represented by the event.
    TransmissionAuditEventType type;
    /// Application operation that produced the event.
    TransmissionAuditOperation operation;
    /// Authoritative occurrence time.
    std::chrono::system_clock::time_point occurred_at;
    /// Request correlation identifier.
    domain::CorrelationId correlation_id;
    /// Session involved in the event, when known.
    std::optional<domain::SessionId> session_id;
    /// Device involved in the event, when known.
    std::optional<domain::DeviceId> device_id;
    /// Client transmission identifier, when known.
    std::optional<domain::ClientTransmissionId> client_transmission_id;
    /// Server transmission identifier, when allocated.
    std::optional<domain::TransmissionId> transmission_id;
    /// Participant performing an administrative action, when applicable.
    std::optional<domain::PlayerId> actor_player_id;
    /// Participant sending voice, when known.
    std::optional<domain::PlayerId> sender_player_id;
    /// Voice scope involved in the event, when known.
    std::optional<domain::VoiceScope> scope;
    /// Membership version used for authorization, when known.
    std::optional<std::uint64_t> membership_version;
    /// Number of authorized recipients, never their identifiers.
    std::optional<std::size_t> recipient_count;
    /// Stop reason for ended or interrupted transmissions.
    std::optional<domain::TransmissionStopReason> stop_reason;
    /// Rejection reason for rejected operations.
    std::optional<TransmissionAuditRejectionReason> rejection_reason;
};

/// Persist or forward transmission audit events without affecting application flow.
class ITransmissionAuditEventSink
{
  public:
    /// Destroy the audit-event sink interface.
    virtual ~ITransmissionAuditEventSink() = default;

    /**
     * Record one audit event.
     *
     * @param event Complete privacy-preserving event metadata.
     * @note Implementations must absorb internal failures because auditing must
     *     not throw through the application transaction.
     */
    virtual void record(const TransmissionAuditEvent& event) noexcept = 0;
};
} // namespace hvc::application
