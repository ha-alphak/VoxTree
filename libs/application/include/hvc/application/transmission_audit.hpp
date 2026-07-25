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
enum class TransmissionAuditEventType : std::uint8_t
{
    started,
    ended,
    rejected,
    forcibly_interrupted
};

enum class TransmissionAuditOperation : std::uint8_t
{
    start,
    end,
    moderation,
    timeout,
    session_lifecycle,
    membership_change
};

enum class TransmissionAuditRejectionReason : std::uint8_t
{
    session_not_found,
    session_device_mismatch,
    session_expired,
    membership_unavailable,
    voice_not_connected,
    voice_no_active_membership,
    voice_scope_not_found,
    voice_scope_not_authorized,
    voice_transmit_muted,
    voice_membership_stale,
    session_changed_during_start,
    membership_changed_during_start,
    sender_already_transmitting,
    transmission_id_conflict,
    transmission_not_found,
    transmission_not_owned,
    not_authorized,
    rate_limited
};

struct TransmissionAuditEvent final
{
    TransmissionAuditEvent(TransmissionAuditEventType event_type,
                           TransmissionAuditOperation audited_operation,
                           std::chrono::system_clock::time_point event_time,
                           domain::CorrelationId correlation)
        : type(event_type), operation(audited_operation), occurred_at(event_time),
          correlation_id(std::move(correlation))
    {
    }

    TransmissionAuditEventType type;
    TransmissionAuditOperation operation;
    std::chrono::system_clock::time_point occurred_at;
    domain::CorrelationId correlation_id;
    std::optional<domain::SessionId> session_id;
    std::optional<domain::DeviceId> device_id;
    std::optional<domain::ClientTransmissionId> client_transmission_id;
    std::optional<domain::TransmissionId> transmission_id;
    std::optional<domain::PlayerId> actor_player_id;
    std::optional<domain::PlayerId> sender_player_id;
    std::optional<domain::VoiceScope> scope;
    std::optional<std::uint64_t> membership_version;
    std::optional<std::size_t> recipient_count;
    std::optional<domain::TransmissionStopReason> stop_reason;
    std::optional<TransmissionAuditRejectionReason> rejection_reason;
};

class ITransmissionAuditEventSink
{
  public:
    virtual ~ITransmissionAuditEventSink() = default;

    virtual void record(const TransmissionAuditEvent& event) noexcept = 0;
};
} // namespace hvc::application
