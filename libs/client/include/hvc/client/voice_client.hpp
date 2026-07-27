#pragma once

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <hvc/client/voice_transport.hpp>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hvc::client
{
/// Identify the lifecycle phase of the local microphone publication.
enum class MicrophonePublicationState : std::uint8_t
{
    /// No publication is active or pending.
    idle,
    /// The transport is negotiating a new publication.
    starting,
    /// The transport confirmed the local publication.
    active,
    /// Cancellation or unpublish is in progress.
    stopping
};

/**
 * Configure stream admission, hierarchy ducking, and local playout gain.
 *
 * Scope arrays use the `team`, `specialization`, and `group` enum order.
 */
struct AudioEngineConfig final
{
    /// Maximum number of concurrently admitted remote streams.
    std::size_t maximum_streams{8};
    /// Maximum admitted streams for each scope.
    std::array<std::size_t, 3> maximum_streams_per_scope{5, 4, 2};
    /// Gain applied to Team audio while Specialization audio is admitted.
    float team_gain_under_specialization{0.5F};
    /// Gain applied to Team audio while Group audio is admitted.
    float team_gain_under_group{0.25F};
    /// Gain applied to Specialization audio while Group audio is admitted.
    float specialization_gain_under_group{0.5F};
};

/// Describe the current local playback decision for one remote stream.
struct RemoteAudioPlayback final
{
    /// Hierarchy scope in which the microphone was published.
    domain::VoiceScope scope{domain::VoiceScope::team};
    /// Transport participant identifier.
    std::string participant_id;
    /// Whether the stream is admitted for decoding and playout.
    bool admitted{false};
    /// Effective linear gain after local volume and hierarchy ducking.
    float gain{0.0F};
};

/// Classify normalized remote-participant and audio transitions.
enum class VoiceRemoteEventKind : std::uint8_t
{
    /// The participant joined one granted scope room.
    participant_connected,
    /// The participant left one granted scope room.
    participant_disconnected,
    /// A remote microphone publication is available for admission.
    audio_available,
    /// A remote microphone publication is no longer available.
    audio_unavailable,
    /// Admitted remote audio became audible.
    speaker_started,
    /// Admitted remote audio ceased to be audible.
    speaker_stopped
};

/// Describe an ordered aggregate voice-state transition.
struct VoiceConnectionEvent final
{
    /// New aggregate transport state.
    VoiceTransportState state{VoiceTransportState::disconnected};
    /// Connection generation invalidating earlier remote events.
    std::uint64_t generation{0};
    /// Monotonically increasing voice event sequence.
    std::uint64_t sequence{0};
};

/// Describe one ordered, generation-bound remote transition.
struct VoiceRemoteEvent final
{
    /// Remote transition classification.
    VoiceRemoteEventKind kind{VoiceRemoteEventKind::participant_connected};
    /// Scope room in which the transition occurred.
    domain::VoiceScope scope{domain::VoiceScope::team};
    /// Stable transport participant identifier.
    std::string participant_id;
    /// Connection generation in which the callback was accepted.
    std::uint64_t generation{0};
    /// Monotonically increasing voice event sequence.
    std::uint64_t sequence{0};
};

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
     * @param event Ordered transport-backed voice state.
     */
    virtual void onVoiceStateChanged(const VoiceConnectionEvent& event) = 0;
    /**
     * Handle a participant, publication, or audible-audio transition.
     *
     * @param event Ordered, connection-generation-bound remote transition.
     */
    virtual void onVoiceRemoteEvent(const VoiceRemoteEvent& event) = 0;
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
    /**
     * Return the local microphone publication phase.
     *
     * @returns Current phase synchronized with publication confirmation and
     *     teardown.
     */
    [[nodiscard]] auto microphonePublicationState() const noexcept -> MicrophonePublicationState;
    /**
     * Return the current publication generation.
     *
     * Each accepted press advances the generation so a delayed start result
     * cannot activate a newer request.
     *
     * @returns Monotonically increasing generation within this client.
     */
    [[nodiscard]] auto microphonePublicationGeneration() const noexcept -> std::uint64_t;
    /**
     * Replace audio admission and ducking settings.
     *
     * Existing remote publications are re-evaluated immediately.
     *
     * @param config Valid limits and gains.
     * @returns The outcome of applying all changed playback decisions.
     */
    [[nodiscard]] auto setAudioEngineConfig(const AudioEngineConfig& config)
        -> VoiceTransportResult;
    /**
     * Return the current audio admission and ducking settings.
     *
     * @returns Thread-safe configuration snapshot.
     */
    [[nodiscard]] auto audioEngineConfig() const noexcept -> AudioEngineConfig;
    /**
     * Mute or unmute one participant locally.
     *
     * A mute takes effect immediately without changing any other recipient.
     *
     * @param participant_id Non-empty transport participant identifier.
     * @param muted Whether local audio must be suppressed.
     * @returns The outcome of applying changed playback decisions.
     */
    [[nodiscard]] auto setParticipantMuted(const std::string& participant_id, bool muted)
        -> VoiceTransportResult;
    /**
     * Block or unblock one participant locally.
     *
     * A block always suppresses local audio and is independent of server
     * transmission authorization.
     *
     * @param participant_id Non-empty transport participant identifier.
     * @param blocked Whether local audio must be suppressed.
     * @returns The outcome of applying changed playback decisions.
     */
    [[nodiscard]] auto setParticipantBlocked(const std::string& participant_id, bool blocked)
        -> VoiceTransportResult;
    /**
     * Set an individual participant's local playout volume.
     *
     * @param participant_id Non-empty transport participant identifier.
     * @param volume Linear gain in the inclusive range `[0.0F, 1.0F]`.
     * @returns The outcome of applying changed playback decisions.
     */
    [[nodiscard]] auto setParticipantVolume(const std::string& participant_id, float volume)
        -> VoiceTransportResult;
    /**
     * Return all currently available remote playback decisions.
     *
     * @returns Stable snapshot ordered by scope priority and admission order.
     */
    [[nodiscard]] auto remoteAudioPlayback() const -> std::vector<RemoteAudioPlayback>;

  private:
    void onTransportStateChanged(VoiceTransportState state) override;
    void onRemoteParticipantConnected(domain::VoiceScope scope,
                                      const std::string& participant_id) override;
    void onRemoteParticipantDisconnected(domain::VoiceScope scope,
                                         const std::string& participant_id) override;
    void onRemoteAudioAvailable(domain::VoiceScope scope,
                                const std::string& participant_id) override;
    void onRemoteAudioUnavailable(domain::VoiceScope scope,
                                  const std::string& participant_id) override;
    void onRemoteAudioStarted(domain::VoiceScope scope, const std::string& participant_id) override;
    void onRemoteAudioStopped(domain::VoiceScope scope, const std::string& participant_id) override;
    void onTransportError(VoiceTransportError error, const std::string& message) override;

    [[nodiscard]] static auto validateGrants(std::span<const VoiceRoomGrant> grants)
        -> VoiceTransportResult;
    [[nodiscard]] static auto validateAudioConfig(const AudioEngineConfig& config)
        -> VoiceTransportResult;
    [[nodiscard]] auto observer() const noexcept -> IVoiceClientObserver*;
    void emitRemoteEvent(VoiceRemoteEventKind kind, domain::VoiceScope scope,
                         const std::string& participant_id);
    struct AvailableRemoteAudio final
    {
        domain::VoiceScope scope{domain::VoiceScope::team};
        std::string participant_id;
        std::uint64_t admission_order{0};
        bool admitted{false};
        float gain{0.0F};
    };
    struct PlaybackChange final
    {
        domain::VoiceScope scope{domain::VoiceScope::team};
        std::string participant_id;
        bool admitted{false};
        float gain{0.0F};
    };
    [[nodiscard]] auto recomputePlaybackLocked() -> std::vector<PlaybackChange>;
    [[nodiscard]] auto applyPlaybackChanges(std::vector<PlaybackChange> changes)
        -> VoiceTransportResult;
    [[nodiscard]] auto updateParticipantFlag(const std::string& participant_id, bool enabled,
                                             std::unordered_set<std::string>& values)
        -> VoiceTransportResult;

    IVoiceTransport& transport_;
    mutable std::mutex mutex_;
    std::condition_variable publication_changed_;
    IVoiceClientObserver* observer_{nullptr};
    VoiceTransportState state_{VoiceTransportState::disconnected};
    std::uint64_t connection_generation_{0};
    std::uint64_t next_event_sequence_{0};
    MicrophonePublicationState publication_state_{MicrophonePublicationState::idle};
    std::uint64_t publication_generation_{0};
    std::optional<domain::VoiceScope> active_scope_;
    VoiceTransportResult last_stop_result_{};
    AudioEngineConfig audio_config_;
    std::vector<AvailableRemoteAudio> available_remote_audio_;
    std::unordered_set<std::string> muted_participants_;
    std::unordered_set<std::string> blocked_participants_;
    std::unordered_map<std::string, float> participant_volumes_;
    std::uint64_t next_admission_order_{0};
};
} // namespace hvc::client
