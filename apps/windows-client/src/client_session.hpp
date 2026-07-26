#pragma once

#include <cstdint>
#include <functional>
#include <hvc/client/authorized_voice_client.hpp>
#include <hvc/client/control_plane_client.hpp>
#include <hvc/client/ptt_input.hpp>
#include <hvc/client/voice_client.hpp>
#include <hvc/client/win_http_transport.hpp>
#include <hvc/client/win_raw_input.hpp>
#include <hvc/livekit/livekit_voice_transport.hpp>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

/// Assemble the Windows UI shell and its client-side service graph.
namespace hvc::windows_client
{
/// Classify structured session events consumed by the Windows UI.
enum class SessionEventKind : std::uint8_t
{
    /// The aggregate voice connection state changed.
    connection_state,
    /// A remote participant began producing audible audio.
    speaker_started,
    /// A remote participant stopped producing audible audio.
    speaker_stopped,
    /// An authorized local transmission started.
    transmission_started,
    /// The local transmission stopped.
    transmission_stopped,
    /// A session operation failed.
    error
};

/// Describe one session event without embedding presentation text.
struct SessionEvent final
{
    /// Event classification.
    SessionEventKind kind{SessionEventKind::connection_state};
    /// Voice state associated with a connection event.
    client::VoiceTransportState voice_state{client::VoiceTransportState::disconnected};
    /// Hierarchy scope associated with a speaker or transmission event.
    std::optional<domain::VoiceScope> scope;
    /// Transport participant identifier associated with a speaker event.
    std::string participant_id;
    /// Stable error code when `kind` is `error`.
    std::string error_code;
    /// Diagnostic detail supplied by the underlying subsystem.
    std::string diagnostic;
};

/// Hold the outcome and membership established by a UI connection attempt.
struct ConnectResult final
{
    /// Whether the client reached the connected ready state.
    bool successful{false};
    /// Human-readable status or failure message.
    std::string message;
    /// Authoritative membership fetched during a successful connection.
    std::optional<client::MembershipView> membership;
};

/**
 * Own one Windows client's control-plane, voice, and input session.
 *
 * The session constructs platform transports lazily during `connect()` and
 * reports asynchronous state through a callback supplied by the UI.
 */
class ClientSession final : private client::IClientIdentifierGenerator,
                            private client::IVoiceClientObserver,
                            private client::IPushToTalkInputObserver
{
  public:
    /// Callback used to deliver structured session events to the UI.
    using EventCallback = std::function<void(SessionEvent)>;

    /**
     * Construct a disconnected client session.
     *
     * @param event_callback Callback invoked for state, speaker, transmission,
     *     and error events.
     */
    explicit ClientSession(EventCallback event_callback);
    /// Disconnect and release all platform client resources.
    ~ClientSession() override;

    /// Copy construction is disabled.
    ClientSession(const ClientSession&) = delete;
    /// Copy assignment is disabled.
    auto operator=(const ClientSession&) -> ClientSession& = delete;
    /// Move construction is disabled.
    ClientSession(ClientSession&&) = delete;
    /// Move assignment is disabled.
    auto operator=(ClientSession&&) -> ClientSession& = delete;

    /**
     * Connect the control plane, authorized rooms, and Raw Input source.
     *
     * @param server_url Absolute HTTP or HTTPS control-plane base URL.
     * @param credential External credential accepted by the server.
     * @returns Connection outcome and authoritative membership when successful.
     */
    [[nodiscard]] auto connect(const std::string& server_url, const std::string& credential)
        -> ConnectResult;
    /// Disconnect all services and return to an empty session state.
    void disconnect() noexcept;
    /**
     * Enumerate recording devices visible to the connected voice transport.
     *
     * @returns Stable device snapshot, or an empty list while disconnected.
     */
    [[nodiscard]] auto recordingDevices() const -> std::vector<client::AudioDevice>;
    /**
     * Enumerate playout devices visible to the connected voice transport.
     *
     * @returns Stable device snapshot, or an empty list while disconnected.
     */
    [[nodiscard]] auto playoutDevices() const -> std::vector<client::AudioDevice>;
    /**
     * Select the active recording device.
     *
     * @param device_id Identifier returned by `recordingDevices()`.
     * @returns Transport outcome or an invalid-state failure while disconnected.
     */
    [[nodiscard]] auto selectRecordingDevice(const std::string& device_id)
        -> client::VoiceTransportResult;
    /**
     * Select the active playout device.
     *
     * @param device_id Identifier returned by `playoutDevices()`.
     * @returns Transport outcome or an invalid-state failure while disconnected.
     */
    [[nodiscard]] auto selectPlayoutDevice(const std::string& device_id)
        -> client::VoiceTransportResult;
    /**
     * Apply stream-admission and hierarchy-ducking settings.
     *
     * @param config Complete audio-engine configuration.
     * @returns Transport outcome or an invalid-state failure while disconnected.
     */
    [[nodiscard]] auto setAudioEngineConfig(const client::AudioEngineConfig& config)
        -> client::VoiceTransportResult;
    /**
     * Return the current audio-engine configuration.
     *
     * @returns Active configuration or defaults while disconnected.
     */
    [[nodiscard]] auto audioEngineConfig() const noexcept -> client::AudioEngineConfig;
    /**
     * Replace all push-to-talk bindings atomically.
     *
     * @param bindings Candidate binding set.
     * @returns All binding validation failures.
     */
    [[nodiscard]] auto setBindings(std::span<const client::InputBinding> bindings)
        -> client::InputBindingResult;
    /**
     * Return active push-to-talk bindings.
     *
     * @returns Thread-safe binding snapshot.
     */
    [[nodiscard]] auto bindings() const -> std::vector<client::InputBinding>;
    /**
     * Return input devices discovered by Raw Input.
     *
     * @returns Thread-safe device-profile snapshot.
     */
    [[nodiscard]] auto inputDevices() const -> std::vector<client::InputDeviceProfile>;
    /**
     * Change one participant's local playout volume.
     *
     * @param participant_id Non-empty transport participant identifier.
     * @param volume Linear gain in the inclusive range `[0.0F, 1.0F]`.
     * @returns Transport outcome or an invalid-state failure while disconnected.
     */
    [[nodiscard]] auto setParticipantVolume(const std::string& participant_id, float volume)
        -> client::VoiceTransportResult;
    /**
     * Change one participant's local mute state.
     *
     * @param participant_id Non-empty transport participant identifier.
     * @param muted Whether local playout must be suppressed.
     * @returns Transport outcome or an invalid-state failure while disconnected.
     */
    [[nodiscard]] auto setParticipantMuted(const std::string& participant_id, bool muted)
        -> client::VoiceTransportResult;

  private:
    [[nodiscard]] auto nextCorrelationId() -> domain::CorrelationId override;
    [[nodiscard]] auto nextTransmissionId() -> domain::ClientTransmissionId override;

    void onVoiceStateChanged(client::VoiceTransportState state) override;
    void onSpeakerStarted(domain::VoiceScope scope, const std::string& participant_id) override;
    void onSpeakerStopped(domain::VoiceScope scope, const std::string& participant_id) override;
    void onVoiceError(client::VoiceTransportError error, const std::string& message) override;
    void onPushToTalkInputResult(client::PushToTalkAction action, bool pressed,
                                 const client::VoiceSessionResult& result) override;

    void report(SessionEvent event) const;
    [[nodiscard]] static auto voiceSessionMessage(const client::VoiceSessionResult& result)
        -> std::string;
    [[nodiscard]] static auto disconnectedResult() -> client::VoiceTransportResult;

    EventCallback event_callback_;
    std::unique_ptr<client::WinHttpTransport> http_transport_;
    std::unique_ptr<client::ControlPlaneClient> control_plane_;
    std::unique_ptr<livekit::LiveKitVoiceTransport> livekit_transport_;
    std::unique_ptr<client::VoiceClient> voice_client_;
    std::unique_ptr<client::AuthorizedVoiceClient> authorized_client_;
    client::PushToTalkBindingEngine binding_engine_;
    std::unique_ptr<client::AuthorizedPushToTalkInput> ptt_input_;
    std::unique_ptr<client::WinRawInputSource> raw_input_;
};
} // namespace hvc::windows_client
