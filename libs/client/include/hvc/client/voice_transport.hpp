#pragma once

#include <cstddef>
#include <cstdint>
#include <hvc/domain/model.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

/**
 * Provide transport-independent voice, control-plane, and input client APIs.
 */
namespace hvc::client
{
/// Identify the observable lifecycle state of a voice transport.
enum class VoiceTransportState : std::uint8_t
{
    /// No voice-room connection is active.
    disconnected,
    /// One or more voice-room connections are being established.
    connecting,
    /// All granted voice-room connections are established.
    connected,
    /// At least one established connection is recovering.
    reconnecting
};

/// Classify failures reported by a voice transport.
enum class VoiceTransportError : std::uint8_t
{
    /// No error occurred.
    none,
    /// A supplied argument is invalid.
    invalid_argument,
    /// The requested operation is not valid in the current state.
    invalid_state,
    /// A voice-room connection could not be established.
    connection_failed,
    /// No usable audio device is available.
    audio_device_unavailable,
    /// The selected audio device could not be activated.
    audio_device_switch_failed,
    /// Microphone publication could not be started or stopped.
    publication_failed,
    /// The transport encountered an unclassified internal failure.
    internal_error
};

/// Hold the outcome and diagnostic message of a transport operation.
struct VoiceTransportResult final
{
    /**
     * Create a successful result.
     *
     * @returns A result that evaluates to `true`.
     */
    [[nodiscard]] static auto success() -> VoiceTransportResult;
    /**
     * Create a failed result.
     *
     * @param error Non-`none` error classification.
     * @param message Human-readable diagnostic message.
     * @returns A result that evaluates to `false`.
     */
    [[nodiscard]] static auto failure(VoiceTransportError error, std::string message)
        -> VoiceTransportResult;
    /**
     * Return whether the operation succeeded.
     *
     * @returns `true` when `error` is `none`.
     */
    [[nodiscard]] explicit operator bool() const noexcept;

    /// Error classification, or `none` after success.
    VoiceTransportError error{VoiceTransportError::none};
    /// Human-readable diagnostic message, empty after success.
    std::string message;
};

/// Grant access to one transport room associated with a voice scope.
struct VoiceRoomGrant final
{
    /// Hierarchy scope carried by the room.
    domain::VoiceScope scope{domain::VoiceScope::team};
    /// WebSocket URL of the voice service.
    std::string url;
    /// Short-lived access token for the room.
    std::string token;
};

/// Describe an audio capture or playout device.
struct AudioDevice final
{
    /// Stable transport-specific device identifier.
    std::string id;
    /// Human-readable device name.
    std::string display_name;
};

/**
 * Receive asynchronous state, participant, audio, and error notifications.
 *
 * Implementations must remain alive until detached from the observed transport.
 */
class IVoiceTransportObserver
{
  public:
    /// Construct an observer interface.
    IVoiceTransportObserver() = default;
    /// Copy construction is disabled.
    IVoiceTransportObserver(const IVoiceTransportObserver&) = delete;
    /// Copy assignment is disabled.
    auto operator=(const IVoiceTransportObserver&) -> IVoiceTransportObserver& = delete;
    /// Move construction is disabled.
    IVoiceTransportObserver(IVoiceTransportObserver&&) = delete;
    /// Move assignment is disabled.
    auto operator=(IVoiceTransportObserver&&) -> IVoiceTransportObserver& = delete;
    /// Destroy the observer interface.
    virtual ~IVoiceTransportObserver() = default;

    /**
     * Handle a transport-state transition.
     *
     * @param state New aggregate transport state.
     */
    virtual void onTransportStateChanged(VoiceTransportState state) = 0;
    /**
     * Handle a remote participant joining a granted room.
     *
     * @param scope Scope associated with the room.
     * @param participant_id Transport participant identifier.
     */
    virtual void onRemoteParticipantConnected(domain::VoiceScope scope,
                                              const std::string& participant_id) = 0;
    /**
     * Handle a remote participant leaving a granted room.
     *
     * @param scope Scope associated with the room.
     * @param participant_id Transport participant identifier.
     */
    virtual void onRemoteParticipantDisconnected(domain::VoiceScope scope,
                                                 const std::string& participant_id) = 0;
    /**
     * Handle the start of audible remote audio.
     *
     * @param scope Scope associated with the room.
     * @param participant_id Transport participant identifier.
     */
    virtual void onRemoteAudioStarted(domain::VoiceScope scope,
                                      const std::string& participant_id) = 0;
    /**
     * Handle the end of audible remote audio.
     *
     * @param scope Scope associated with the room.
     * @param participant_id Transport participant identifier.
     */
    virtual void onRemoteAudioStopped(domain::VoiceScope scope,
                                      const std::string& participant_id) = 0;
    /**
     * Handle an asynchronous transport failure.
     *
     * @param error Failure classification.
     * @param message Human-readable diagnostic message.
     */
    virtual void onTransportError(VoiceTransportError error, const std::string& message) = 0;
};

/**
 * Abstract voice-room connectivity, audio publication, and device selection.
 *
 * A transport owns its network and media resources but not its observer. Only
 * one microphone scope may be active at a time.
 */
class IVoiceTransport
{
  public:
    /// Construct a transport interface.
    IVoiceTransport() = default;
    /// Copy construction is disabled.
    IVoiceTransport(const IVoiceTransport&) = delete;
    /// Copy assignment is disabled.
    auto operator=(const IVoiceTransport&) -> IVoiceTransport& = delete;
    /// Move construction is disabled.
    IVoiceTransport(IVoiceTransport&&) = delete;
    /// Move assignment is disabled.
    auto operator=(IVoiceTransport&&) -> IVoiceTransport& = delete;
    /// Destroy the transport interface.
    virtual ~IVoiceTransport() = default;

    /**
     * Replace the asynchronous observer.
     *
     * @param observer Observer to notify, or `nullptr` to detach.
     * @note The caller must keep a non-null observer alive until it is detached.
     */
    virtual void setObserver(IVoiceTransportObserver* observer) noexcept = 0;
    /**
     * Return the current aggregate transport state.
     *
     * @returns Latest state across all granted room connections.
     */
    [[nodiscard]] virtual auto state() const noexcept -> VoiceTransportState = 0;
    /**
     * Connect all granted voice rooms.
     *
     * @param grants One non-empty, unique grant per authorized scope.
     * @returns The synchronous outcome of starting the connection operation.
     */
    [[nodiscard]] virtual auto connect(std::span<const VoiceRoomGrant> grants)
        -> VoiceTransportResult = 0;
    /**
     * Disconnect all rooms and stop microphone publication.
     *
     * @returns The outcome of releasing the active transport resources.
     */
    [[nodiscard]] virtual auto disconnect() -> VoiceTransportResult = 0;

    /**
     * Publish microphone audio to one connected scope.
     *
     * @param scope Scope that receives microphone audio.
     * @returns The outcome of starting publication.
     */
    [[nodiscard]] virtual auto startMicrophone(domain::VoiceScope scope)
        -> VoiceTransportResult = 0;
    /**
     * Stop the active microphone publication.
     *
     * @returns The outcome of stopping publication.
     */
    [[nodiscard]] virtual auto stopMicrophone() -> VoiceTransportResult = 0;
    /**
     * Return the scope receiving microphone audio.
     *
     * @returns Active publication scope, or no value when the microphone is
     *     stopped.
     */
    [[nodiscard]] virtual auto activeTransmissionScope() const noexcept
        -> std::optional<domain::VoiceScope> = 0;

    /**
     * Enumerate available audio-capture devices.
     *
     * @returns A snapshot of transport-visible recording devices.
     */
    [[nodiscard]] virtual auto recordingDevices() const -> std::vector<AudioDevice> = 0;
    /**
     * Enumerate available audio-playout devices.
     *
     * @returns A snapshot of transport-visible playout devices.
     */
    [[nodiscard]] virtual auto playoutDevices() const -> std::vector<AudioDevice> = 0;
    /**
     * Select the active audio-capture device.
     *
     * @param device_id Identifier returned by `recordingDevices()`.
     * @returns The outcome of applying the selection.
     */
    [[nodiscard]] virtual auto selectRecordingDevice(const std::string& device_id)
        -> VoiceTransportResult = 0;
    /**
     * Select the active audio-playout device.
     *
     * @param device_id Identifier returned by `playoutDevices()`.
     * @returns The outcome of applying the selection.
     */
    [[nodiscard]] virtual auto selectPlayoutDevice(const std::string& device_id)
        -> VoiceTransportResult = 0;

    /**
     * Count remote participants in one connected scope.
     *
     * @param scope Scope to inspect.
     * @returns The current number of remote participants.
     */
    [[nodiscard]] virtual auto remoteParticipantCount(domain::VoiceScope scope) const
        -> std::size_t = 0;
    /**
     * Determine whether remote audio is active in one scope.
     *
     * @param scope Scope to inspect.
     * @returns `true` when at least one remote participant is audible.
     */
    [[nodiscard]] virtual auto hasRemoteAudio(domain::VoiceScope scope) const -> bool = 0;
};
} // namespace hvc::client
