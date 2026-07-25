#pragma once

#include <cstdint>
#include <hvc/domain/id.hpp>
#include <hvc/domain/model.hpp>
#include <optional>
#include <utility>

namespace hvc::domain
{
enum class ConnectionState : std::uint8_t
{
    disconnected,
    connecting_transport,
    refreshing_membership,
    restoring_subscriptions,
    ready,
    reconnecting_transport
};

class ConnectionStateMachine final
{
  public:
    [[nodiscard]] auto state() const noexcept -> ConnectionState;
    [[nodiscard]] auto ready() const noexcept -> bool;
    [[nodiscard]] auto membershipVersion() const noexcept -> std::optional<std::uint64_t>;

    auto beginConnect() noexcept -> bool;
    auto transportConnected() noexcept -> bool;
    auto membershipRefreshed(std::uint64_t version) noexcept -> bool;
    auto subscriptionsRestored(std::uint64_t version) noexcept -> bool;
    auto membershipChanged(std::uint64_t version) noexcept -> bool;
    auto connectionLost() noexcept -> bool;
    auto disconnect() noexcept -> bool;

  private:
    ConnectionState state_{ConnectionState::disconnected};
    std::optional<std::uint64_t> membership_version_;
};

enum class TransmissionState : std::uint8_t
{
    idle,
    requesting,
    transmitting,
    ending
};

enum class TransmissionStopReason : std::uint8_t
{
    none,
    push_to_talk_released,
    rejected,
    connection_lost,
    membership_changed,
    permission_revoked,
    timed_out,
    moderation_interrupted,
    transport_error,
    disconnected
};

class TransmissionStateMachine final
{
  public:
    [[nodiscard]] auto state() const noexcept -> TransmissionState;
    [[nodiscard]] auto clientRequestId() const noexcept -> const ClientTransmissionId*;
    [[nodiscard]] auto transmissionId() const noexcept -> const TransmissionId*;
    [[nodiscard]] auto scope() const noexcept -> std::optional<VoiceScope>;
    [[nodiscard]] auto membershipVersion() const noexcept -> std::optional<std::uint64_t>;
    [[nodiscard]] auto lastStopReason() const noexcept -> TransmissionStopReason;

    auto requestStart(ClientTransmissionId client_request_id, VoiceScope scope,
                      std::uint64_t membership_version) -> bool;
    auto accepted(const ClientTransmissionId& client_request_id, TransmissionId transmission_id)
        -> bool;
    auto rejected(const ClientTransmissionId& client_request_id) noexcept -> bool;
    auto requestEnd() noexcept -> bool;
    auto ended(const TransmissionId& transmission_id) noexcept -> bool;
    auto interrupt(TransmissionStopReason reason) noexcept -> bool;

  private:
    struct Context final
    {
        Context(ClientTransmissionId request_id, VoiceScope requested_scope,
                std::uint64_t snapshot_version)
            : client_request_id(std::move(request_id)), scope(requested_scope),
              membership_version(snapshot_version)
        {
        }

        ClientTransmissionId client_request_id;
        std::optional<TransmissionId> transmission_id;
        VoiceScope scope;
        std::uint64_t membership_version;
    };

    void finish(TransmissionStopReason reason) noexcept;

    TransmissionState state_{TransmissionState::idle};
    std::optional<Context> context_;
    TransmissionStopReason last_stop_reason_{TransmissionStopReason::none};
};

class VoiceSessionStateMachine final
{
  public:
    [[nodiscard]] auto connectionState() const noexcept -> ConnectionState;
    [[nodiscard]] auto transmissionState() const noexcept -> TransmissionState;
    [[nodiscard]] auto selectedScope() const noexcept -> VoiceScope;
    [[nodiscard]] auto membershipVersion() const noexcept -> std::optional<std::uint64_t>;
    [[nodiscard]] auto clientRequestId() const noexcept -> const ClientTransmissionId*;
    [[nodiscard]] auto transmissionId() const noexcept -> const TransmissionId*;
    [[nodiscard]] auto lastTransmissionStopReason() const noexcept -> TransmissionStopReason;

    void selectScope(VoiceScope scope) noexcept;

    auto beginConnect() noexcept -> bool;
    auto transportConnected() noexcept -> bool;
    auto membershipRefreshed(std::uint64_t version) noexcept -> bool;
    auto subscriptionsRestored(std::uint64_t version) noexcept -> bool;
    auto membershipChanged(std::uint64_t version) noexcept -> bool;
    auto connectionLost() noexcept -> bool;
    auto disconnect() noexcept -> bool;

    auto requestTransmission(ClientTransmissionId client_request_id) -> bool;
    auto transmissionAccepted(const ClientTransmissionId& client_request_id,
                              TransmissionId transmission_id) -> bool;
    auto transmissionRejected(const ClientTransmissionId& client_request_id) noexcept -> bool;
    auto endTransmission() noexcept -> bool;
    auto transmissionEnded(const TransmissionId& transmission_id) noexcept -> bool;
    auto permissionRevoked() noexcept -> bool;
    auto transmissionTimedOut() noexcept -> bool;
    auto transmissionTransportFailed() noexcept -> bool;

  private:
    ConnectionStateMachine connection_;
    TransmissionStateMachine transmission_;
    VoiceScope selected_scope_{VoiceScope::team};
};
} // namespace hvc::domain
