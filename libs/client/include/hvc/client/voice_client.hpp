#pragma once

#include <hvc/client/voice_transport.hpp>
#include <mutex>
#include <optional>
#include <span>
#include <string>

namespace hvc::client
{
/// Receive normalized voice-client state, speaker, and error notifications.
class IVoiceClientObserver
{
  public:
    /// Construct an observer interface.
    IVoiceClientObserver() = default;
    /// Copy construction is disabled.
    IVoiceClientObserver(const IVoiceClientObserver&) = delete;
    /// Copy assignment is disabled.
    auto operator=(const IVoiceClientObserver&) -> IVoiceClientObserver& = delete;
    /// Move construction is disabled.
    IVoiceClientObserver(IVoiceClientObserver&&) = delete;
    /// Move assignment is disabled.
    auto operator=(IVoiceClientObserver&&) -> IVoiceClientObserver& = delete;
    /// Destroy the observer interface.
    virtual ~IVoiceClientObserver() = default;

    /**
     * Handle a voice-state transition.
     *
     * @param state New transport-backed voice state.
     */
    virtual void onVoiceStateChanged(VoiceTransportState state) = 0;
    /**
     * Handle a participant becoming audible.
     *
     * @param scope Scope in which audio started.
     * @param participant_id Transport participant identifier.
     */
    virtual void onSpeakerStarted(domain::VoiceScope scope, const std::string& participant_id) = 0;
    /**
     * Handle a participant ceasing to be audible.
     *
     * @param scope Scope in which audio stopped.
     * @param participant_id Transport participant identifier.
     */
    virtual void onSpeakerStopped(domain::VoiceScope scope, const std::string& participant_id) = 0;
    /**
     * Handle a voice transport failure.
     *
     * @param error Failure classification.
     * @param message Human-readable diagnostic message.
     */
    virtual void onVoiceError(VoiceTransportError error, const std::string& message) = 0;
};

/**
 * Coordinate one voice transport and expose exclusive push-to-talk operations.
 *
 * The client owns no transport or observer. The referenced transport must
 * outlive the client, and a non-null observer must remain alive until detached.
 * Observer access and cached state are synchronized for concurrent callbacks.
 */
class VoiceClient final : private IVoiceTransportObserver
{
  public:
    /**
     * Construct a client over an existing transport.
     *
     * @param transport Transport that must outlive this client.
     */
    explicit VoiceClient(IVoiceTransport& transport);
    /// Detach from the transport and destroy the client.
    ~VoiceClient() override;

    /// Copy construction is disabled.
    VoiceClient(const VoiceClient&) = delete;
    /// Copy assignment is disabled.
    auto operator=(const VoiceClient&) -> VoiceClient& = delete;
    /// Move construction is disabled.
    VoiceClient(VoiceClient&&) = delete;
    /// Move assignment is disabled.
    auto operator=(VoiceClient&&) -> VoiceClient& = delete;

    /**
     * Replace the normalized voice observer.
     *
     * @param observer Observer to notify, or `nullptr` to detach.
     */
    void setObserver(IVoiceClientObserver* observer) noexcept;
    /**
     * Return the latest observed voice state.
     *
     * @returns Thread-safe snapshot of the cached state.
     */
    [[nodiscard]] auto state() const noexcept -> VoiceTransportState;
    /**
     * Validate grants and connect the underlying transport.
     *
     * @param grants One non-empty, unique grant per authorized scope.
     * @returns The transport outcome or a grant-validation failure.
     */
    [[nodiscard]] auto connect(std::span<const VoiceRoomGrant> grants) -> VoiceTransportResult;
    /**
     * Disconnect and clear the cached active transmission.
     *
     * @returns The underlying transport outcome.
     */
    [[nodiscard]] auto disconnect() -> VoiceTransportResult;
    /**
     * Start microphone publication in one authorized scope.
     *
     * @param scope Scope selected by push-to-talk.
     * @returns The transport outcome, or an invalid-state failure when another
     *     scope is already active.
     */
    [[nodiscard]] auto pressPushToTalk(domain::VoiceScope scope) -> VoiceTransportResult;
    /**
     * Stop the active microphone publication.
     *
     * @returns The transport outcome.
     */
    [[nodiscard]] auto releasePushToTalk() -> VoiceTransportResult;
    /**
     * Return the scope currently receiving microphone audio.
     *
     * @returns Active publication scope, or no value when the microphone is
     *     stopped.
     */
    [[nodiscard]] auto activeTransmissionScope() const noexcept
        -> std::optional<domain::VoiceScope>;

  private:
    void onTransportStateChanged(VoiceTransportState state) override;
    void onRemoteParticipantConnected(domain::VoiceScope scope,
                                      const std::string& participant_id) override;
    void onRemoteParticipantDisconnected(domain::VoiceScope scope,
                                         const std::string& participant_id) override;
    void onRemoteAudioStarted(domain::VoiceScope scope, const std::string& participant_id) override;
    void onRemoteAudioStopped(domain::VoiceScope scope, const std::string& participant_id) override;
    void onTransportError(VoiceTransportError error, const std::string& message) override;

    [[nodiscard]] static auto validateGrants(std::span<const VoiceRoomGrant> grants)
        -> VoiceTransportResult;
    [[nodiscard]] auto observer() const noexcept -> IVoiceClientObserver*;

    IVoiceTransport& transport_;
    mutable std::mutex mutex_;
    IVoiceClientObserver* observer_{nullptr};
    VoiceTransportState state_{VoiceTransportState::disconnected};
    std::optional<domain::VoiceScope> active_scope_;
};
} // namespace hvc::client
