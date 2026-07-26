#pragma once

#include <cstdint>
#include <hvc/domain/id.hpp>
#include <hvc/domain/model.hpp>
#include <optional>
#include <utility>

namespace hvc::domain
{
/// Identify the current phase of a voice-session connection.
enum class ConnectionState : std::uint8_t
{
    /// No transport or membership session is active.
    disconnected,
    /// The voice transport is establishing a connection.
    connecting_transport,
    /// Authoritative membership is being refreshed.
    refreshing_membership,
    /// Authorized scope subscriptions are being restored.
    restoring_subscriptions,
    /// The connection and subscriptions are ready for transmission.
    ready,
    /// The transport is recovering after an unexpected loss.
    reconnecting_transport
};

/// Enforce valid connection, membership-refresh, and recovery transitions.
class ConnectionStateMachine final
{
  public:
    /**
     * Return the current connection state.
     *
     * @returns Current lifecycle phase.
     */
    [[nodiscard]] auto state() const noexcept -> ConnectionState;
    /**
     * Return whether the session is ready for voice operations.
     *
     * @returns `true` only in the `ready` state.
     */
    [[nodiscard]] auto ready() const noexcept -> bool;
    /**
     * Return the active authoritative membership version.
     *
     * @returns Current version, or no value while disconnected or before the
     *     first refresh.
     */
    [[nodiscard]] auto membershipVersion() const noexcept -> std::optional<std::uint64_t>;

    /**
     * Begin connecting from the disconnected state.
     *
     * @returns `true` when the transition was accepted.
     */
    auto beginConnect() noexcept -> bool;
    /**
     * Record that the transport connection is established.
     *
     * @returns `true` when the transition was accepted.
     */
    auto transportConnected() noexcept -> bool;
    /**
     * Record a refreshed authoritative membership.
     *
     * @param version Nonzero membership version.
     * @returns `true` when the transition was accepted.
     */
    auto membershipRefreshed(std::uint64_t version) noexcept -> bool;
    /**
     * Record that subscriptions for a membership version are restored.
     *
     * @param version Version for which subscriptions were restored.
     * @returns `true` when the version matches and the transition was accepted.
     */
    auto subscriptionsRestored(std::uint64_t version) noexcept -> bool;
    /**
     * Invalidate readiness after an authoritative membership change.
     *
     * @param version New nonzero membership version.
     * @returns `true` when the change was accepted.
     */
    auto membershipChanged(std::uint64_t version) noexcept -> bool;
    /**
     * Begin recovery after an unexpected transport loss.
     *
     * @returns `true` when the transition was accepted.
     */
    auto connectionLost() noexcept -> bool;
    /**
     * Reset the connection to the disconnected state.
     *
     * @returns `true` when state changed.
     */
    auto disconnect() noexcept -> bool;

  private:
    ConnectionState state_{ConnectionState::disconnected};
    std::optional<std::uint64_t> membership_version_;
};

/// Identify the current phase of one push-to-talk transmission.
enum class TransmissionState : std::uint8_t
{
    /// No transmission is active or pending.
    idle,
    /// Authorization has been requested from the server.
    requesting,
    /// The server has authorized an active transmission.
    transmitting,
    /// Termination has been requested and awaits confirmation.
    ending
};

/// Classify why the most recent transmission stopped.
enum class TransmissionStopReason : std::uint8_t
{
    /// No transmission has stopped since initialization.
    none,
    /// The user released push-to-talk.
    push_to_talk_released,
    /// The server rejected the start request.
    rejected,
    /// The connection was lost.
    connection_lost,
    /// Authoritative membership changed.
    membership_changed,
    /// The sender's transmit permission was revoked.
    permission_revoked,
    /// The maximum transmission duration elapsed.
    timed_out,
    /// An authorized moderator interrupted the transmission.
    moderation_interrupted,
    /// The voice transport reported a failure.
    transport_error,
    /// The voice session was explicitly disconnected.
    disconnected
};

/// Enforce the lifecycle and correlation of one transmission at a time.
class TransmissionStateMachine final
{
  public:
    /**
     * Return the current transmission state.
     *
     * @returns Current lifecycle phase.
     */
    [[nodiscard]] auto state() const noexcept -> TransmissionState;
    /**
     * Return the active client request identifier.
     *
     * @returns Pointer valid while the current request context remains active,
     *     or `nullptr` while idle.
     */
    [[nodiscard]] auto clientRequestId() const noexcept -> const ClientTransmissionId*;
    /**
     * Return the server transmission identifier after authorization.
     *
     * @returns Pointer valid while the authorized context remains active, or
     *     `nullptr` before authorization and while idle.
     */
    [[nodiscard]] auto transmissionId() const noexcept -> const TransmissionId*;
    /**
     * Return the requested voice scope.
     *
     * @returns Scope while a request is pending or active, otherwise no value.
     */
    [[nodiscard]] auto scope() const noexcept -> std::optional<VoiceScope>;
    /**
     * Return the membership version used for the current request.
     *
     * @returns Version while a request is pending or active, otherwise no value.
     */
    [[nodiscard]] auto membershipVersion() const noexcept -> std::optional<std::uint64_t>;
    /**
     * Return the reason the most recent transmission stopped.
     *
     * @returns Last terminal reason, or `none` before the first termination.
     */
    [[nodiscard]] auto lastStopReason() const noexcept -> TransmissionStopReason;

    /**
     * Begin a transmission-authorization request.
     *
     * @param client_request_id Client-generated correlation identifier.
     * @param scope Requested voice scope.
     * @param membership_version Membership version used for authorization.
     * @returns `true` when the machine was idle and accepted the request.
     */
    auto requestStart(ClientTransmissionId client_request_id, VoiceScope scope,
                      std::uint64_t membership_version) -> bool;
    /**
     * Accept the matching transmission request.
     *
     * @param client_request_id Client request being accepted.
     * @param transmission_id Server-assigned transmission identifier.
     * @returns `true` when the request matched the pending request.
     */
    auto accepted(const ClientTransmissionId& client_request_id, TransmissionId transmission_id)
        -> bool;
    /**
     * Reject the matching transmission request.
     *
     * @param client_request_id Client request being rejected.
     * @returns `true` when the request matched the pending request.
     */
    auto rejected(const ClientTransmissionId& client_request_id) noexcept -> bool;
    /**
     * Begin ending the active transmission.
     *
     * @returns `true` when the transition was accepted.
     */
    auto requestEnd() noexcept -> bool;
    /**
     * Confirm that the matching server transmission ended.
     *
     * @param transmission_id Server transmission being ended.
     * @returns `true` when the identifier matched the active transmission.
     */
    auto ended(const TransmissionId& transmission_id) noexcept -> bool;
    /**
     * Immediately stop any pending or active transmission.
     *
     * @param reason Non-`none` reason for the interruption.
     * @returns `true` when a transmission existed and was stopped.
     */
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

/**
 * Coordinate connection and transmission state for one voice session.
 *
 * A transmission may start only while the connection is ready. Connection,
 * membership, and permission changes interrupt an in-flight transmission
 * without automatically resuming it.
 */
class VoiceSessionStateMachine final
{
  public:
    /**
     * Return the current connection state.
     *
     * @returns Current coordinated connection phase.
     */
    [[nodiscard]] auto connectionState() const noexcept -> ConnectionState;
    /**
     * Return the current transmission state.
     *
     * @returns Current coordinated transmission phase.
     */
    [[nodiscard]] auto transmissionState() const noexcept -> TransmissionState;
    /**
     * Return the scope selected for the next transmission.
     *
     * @returns Current push-to-talk scope selection.
     */
    [[nodiscard]] auto selectedScope() const noexcept -> VoiceScope;
    /**
     * Return the active membership version.
     *
     * @returns Current version, or no value when membership is unavailable.
     */
    [[nodiscard]] auto membershipVersion() const noexcept -> std::optional<std::uint64_t>;
    /**
     * Return the current client request identifier.
     *
     * @returns Pointer owned by the transmission state machine, or `nullptr`
     *     while idle.
     */
    [[nodiscard]] auto clientRequestId() const noexcept -> const ClientTransmissionId*;
    /**
     * Return the current server transmission identifier.
     *
     * @returns Pointer owned by the transmission state machine, or `nullptr`
     *     before authorization and while idle.
     */
    [[nodiscard]] auto transmissionId() const noexcept -> const TransmissionId*;
    /**
     * Return the reason the most recent transmission stopped.
     *
     * @returns Last terminal reason, or `none` before the first termination.
     */
    [[nodiscard]] auto lastTransmissionStopReason() const noexcept -> TransmissionStopReason;

    /**
     * Select the scope used by the next transmission request.
     *
     * @param scope Scope to select.
     */
    void selectScope(VoiceScope scope) noexcept;

    /**
     * Begin connecting from the disconnected state.
     *
     * @returns `true` when the coordinated transition was accepted.
     */
    auto beginConnect() noexcept -> bool;
    /**
     * Record that the transport connection is established.
     *
     * @returns `true` when the coordinated transition was accepted.
     */
    auto transportConnected() noexcept -> bool;
    /**
     * Record a refreshed authoritative membership.
     *
     * @param version Nonzero membership version.
     * @returns `true` when the connection transition was accepted.
     */
    auto membershipRefreshed(std::uint64_t version) noexcept -> bool;
    /**
     * Record that subscriptions are restored for a membership version.
     *
     * @param version Version for which subscriptions were restored.
     * @returns `true` when the version matched and readiness was entered.
     */
    auto subscriptionsRestored(std::uint64_t version) noexcept -> bool;
    /**
     * Apply an authoritative membership change.
     *
     * @param version New nonzero membership version.
     * @returns `true` when the connection transition was accepted.
     */
    auto membershipChanged(std::uint64_t version) noexcept -> bool;
    /**
     * Apply an unexpected connection loss and interrupt any transmission.
     *
     * @returns `true` when the connection transition was accepted.
     */
    auto connectionLost() noexcept -> bool;
    /**
     * Disconnect and reset both coordinated state machines.
     *
     * @returns `true` when connection state changed.
     */
    auto disconnect() noexcept -> bool;

    /**
     * Request transmission in the selected scope.
     *
     * @param client_request_id Client-generated correlation identifier.
     * @returns `true` when the connection was ready and the request was accepted.
     */
    auto requestTransmission(ClientTransmissionId client_request_id) -> bool;
    /**
     * Apply server authorization for the matching request.
     *
     * @param client_request_id Client request being authorized.
     * @param transmission_id Server-assigned transmission identifier.
     * @returns `true` when the pending request matched.
     */
    auto transmissionAccepted(const ClientTransmissionId& client_request_id,
                              TransmissionId transmission_id) -> bool;
    /**
     * Apply server rejection for the matching request.
     *
     * @param client_request_id Client request being rejected.
     * @returns `true` when the pending request matched.
     */
    auto transmissionRejected(const ClientTransmissionId& client_request_id) noexcept -> bool;
    /**
     * Request termination of the active transmission.
     *
     * @returns `true` when the transition was accepted.
     */
    auto endTransmission() noexcept -> bool;
    /**
     * Confirm termination of the matching server transmission.
     *
     * @param transmission_id Server transmission being terminated.
     * @returns `true` when the identifier matched.
     */
    auto transmissionEnded(const TransmissionId& transmission_id) noexcept -> bool;
    /**
     * Interrupt transmission after permission revocation.
     *
     * @returns `true` when a pending or active transmission was stopped.
     */
    auto permissionRevoked() noexcept -> bool;
    /**
     * Interrupt transmission after its maximum duration.
     *
     * @returns `true` when a pending or active transmission was stopped.
     */
    auto transmissionTimedOut() noexcept -> bool;
    /**
     * Interrupt transmission after a transport failure.
     *
     * @returns `true` when a pending or active transmission was stopped.
     */
    auto transmissionTransportFailed() noexcept -> bool;

  private:
    ConnectionStateMachine connection_;
    TransmissionStateMachine transmission_;
    VoiceScope selected_scope_{VoiceScope::team};
};
} // namespace hvc::domain
