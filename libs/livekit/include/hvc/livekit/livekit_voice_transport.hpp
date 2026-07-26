#pragma once

#include <hvc/client/voice_transport.hpp>
#include <memory>

namespace hvc::livekit
{
/**
 * Implement scope-isolated voice rooms with the native LiveKit SDK.
 *
 * The transport owns LiveKit rooms, audio devices, event handlers, and
 * publications behind an implementation object. Public operations serialize
 * access to SDK state.
 */
class LiveKitVoiceTransport final : public client::IVoiceTransport
{
  public:
    /**
     * Construct a disconnected LiveKit transport.
     *
     * @throws std::runtime_error Thrown when the native SDK cannot initialize.
     * @exceptsafe Strong exception guarantee.
     */
    LiveKitVoiceTransport();
    /// Disconnect rooms and release native SDK resources.
    ~LiveKitVoiceTransport() override;

    /// Copy construction is disabled.
    LiveKitVoiceTransport(const LiveKitVoiceTransport&) = delete;
    /// Copy assignment is disabled.
    auto operator=(const LiveKitVoiceTransport&) -> LiveKitVoiceTransport& = delete;
    /// Move construction is disabled.
    LiveKitVoiceTransport(LiveKitVoiceTransport&&) = delete;
    /// Move assignment is disabled.
    auto operator=(LiveKitVoiceTransport&&) -> LiveKitVoiceTransport& = delete;

    /// @copydoc client::IVoiceTransport::setObserver
    void setObserver(client::IVoiceTransportObserver* observer) noexcept override;
    /// @copydoc client::IVoiceTransport::state
    [[nodiscard]] auto state() const noexcept -> client::VoiceTransportState override;
    /// @copydoc client::IVoiceTransport::connect
    [[nodiscard]] auto connect(std::span<const client::VoiceRoomGrant> grants)
        -> client::VoiceTransportResult override;
    /// @copydoc client::IVoiceTransport::disconnect
    [[nodiscard]] auto disconnect() -> client::VoiceTransportResult override;
    /// @copydoc client::IVoiceTransport::startMicrophone
    [[nodiscard]] auto startMicrophone(domain::VoiceScope scope)
        -> client::VoiceTransportResult override;
    /// @copydoc client::IVoiceTransport::stopMicrophone
    [[nodiscard]] auto stopMicrophone() -> client::VoiceTransportResult override;
    /// @copydoc client::IVoiceTransport::activeTransmissionScope
    [[nodiscard]] auto activeTransmissionScope() const noexcept
        -> std::optional<domain::VoiceScope> override;
    /// @copydoc client::IVoiceTransport::recordingDevices
    [[nodiscard]] auto recordingDevices() const -> std::vector<client::AudioDevice> override;
    /// @copydoc client::IVoiceTransport::playoutDevices
    [[nodiscard]] auto playoutDevices() const -> std::vector<client::AudioDevice> override;
    /// @copydoc client::IVoiceTransport::selectRecordingDevice
    [[nodiscard]] auto selectRecordingDevice(const std::string& device_id)
        -> client::VoiceTransportResult override;
    /// @copydoc client::IVoiceTransport::selectPlayoutDevice
    [[nodiscard]] auto selectPlayoutDevice(const std::string& device_id)
        -> client::VoiceTransportResult override;
    /// @copydoc client::IVoiceTransport::remoteParticipantCount
    [[nodiscard]] auto remoteParticipantCount(domain::VoiceScope scope) const
        -> std::size_t override;
    /// @copydoc client::IVoiceTransport::hasRemoteAudio
    [[nodiscard]] auto hasRemoteAudio(domain::VoiceScope scope) const -> bool override;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace hvc::livekit
