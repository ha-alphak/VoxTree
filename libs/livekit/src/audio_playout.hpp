#pragma once

#include <hvc/client/voice_transport.hpp>
#include <livekit/livekit.h>
#include <memory>
#include <string>
#include <vector>

namespace hvc::livekit::detail
{
/// Own one platform microphone capture source and its publishable LiveKit track.
class MicrophoneSource
{
  public:
    /// Construct a microphone source.
    MicrophoneSource() = default;
    MicrophoneSource(const MicrophoneSource&) = delete;
    auto operator=(const MicrophoneSource&) -> MicrophoneSource& = delete;
    MicrophoneSource(MicrophoneSource&&) = delete;
    auto operator=(MicrophoneSource&&) -> MicrophoneSource& = delete;
    /// Destroy the source and stop platform capture.
    virtual ~MicrophoneSource() = default;

    /// Return the LiveKit track fed by this source.
    /// @return Publishable local audio track.
    [[nodiscard]] virtual auto track() const -> std::shared_ptr<::livekit::LocalAudioTrack> = 0;
};

/// Enumerate, select, and create microphone capture sources for one platform.
class MicrophoneBackend
{
  public:
    /// Construct a microphone backend.
    MicrophoneBackend() = default;
    MicrophoneBackend(const MicrophoneBackend&) = delete;
    auto operator=(const MicrophoneBackend&) -> MicrophoneBackend& = delete;
    MicrophoneBackend(MicrophoneBackend&&) = delete;
    auto operator=(MicrophoneBackend&&) -> MicrophoneBackend& = delete;
    /// Destroy the microphone backend.
    virtual ~MicrophoneBackend() = default;

    /// Return the currently available recording devices.
    /// @return Snapshot with stable identifiers and display names.
    [[nodiscard]] virtual auto devices() const -> std::vector<client::AudioDevice> = 0;
    /// Select the stable recording-device identifier used for new sources.
    /// @param device_id Stable identifier returned by `devices()`.
    virtual void selectDevice(const std::string& device_id) = 0;
    /// Create a capture source for the selected or default device.
    /// @return Active microphone source.
    [[nodiscard]] virtual auto createSource() -> std::shared_ptr<MicrophoneSource> = 0;
};

/// Own decoded playout for one admitted remote LiveKit audio track.
class RemoteAudioPlayout
{
  public:
    /// Construct a remote playout.
    RemoteAudioPlayout() = default;
    RemoteAudioPlayout(const RemoteAudioPlayout&) = delete;
    auto operator=(const RemoteAudioPlayout&) -> RemoteAudioPlayout& = delete;
    RemoteAudioPlayout(RemoteAudioPlayout&&) = delete;
    auto operator=(RemoteAudioPlayout&&) -> RemoteAudioPlayout& = delete;
    /// Destroy the playout and stop its platform stream.
    virtual ~RemoteAudioPlayout() = default;

    /// Apply a linear gain without rebuilding the stream.
    /// @param gain Linear gain in the inclusive range `[0.0F, 1.0F]`.
    virtual void setGain(float gain) noexcept = 0;
};

/// Enumerate, select, and create remote-audio playouts for one platform.
class AudioPlayoutBackend
{
  public:
    /// Construct a playout backend.
    AudioPlayoutBackend() = default;
    AudioPlayoutBackend(const AudioPlayoutBackend&) = delete;
    auto operator=(const AudioPlayoutBackend&) -> AudioPlayoutBackend& = delete;
    AudioPlayoutBackend(AudioPlayoutBackend&&) = delete;
    auto operator=(AudioPlayoutBackend&&) -> AudioPlayoutBackend& = delete;
    /// Destroy the playout backend.
    virtual ~AudioPlayoutBackend() = default;

    /// Return the currently available playout devices.
    /// @return Snapshot with stable identifiers and display names.
    [[nodiscard]] virtual auto devices() const -> std::vector<client::AudioDevice> = 0;
    /// Select the stable playout-device identifier used for new streams.
    /// @param device_id Stable identifier returned by `devices()`.
    virtual void selectDevice(const std::string& device_id) = 0;
    /// Create playout for an admitted remote track with an initial gain.
    /// @param track Remote LiveKit track to decode.
    /// @param gain Initial linear gain.
    /// @return Active platform playout.
    [[nodiscard]] virtual auto createPlayout(const std::shared_ptr<::livekit::Track>& track,
                                             float gain) -> std::shared_ptr<RemoteAudioPlayout> = 0;
};

/// Create the platform playout implementation for this build.
/// @param platform_audio LiveKit platform-audio service used by compatible backends.
/// @return Platform playout backend.
[[nodiscard]] auto createAudioPlayoutBackend(::livekit::PlatformAudio& platform_audio)
    -> std::unique_ptr<AudioPlayoutBackend>;
/// Create the platform microphone implementation for this build.
/// @param platform_audio LiveKit platform-audio service used by compatible backends.
/// @return Platform microphone backend.
[[nodiscard]] auto createMicrophoneBackend(::livekit::PlatformAudio& platform_audio)
    -> std::unique_ptr<MicrophoneBackend>;
} // namespace hvc::livekit::detail
