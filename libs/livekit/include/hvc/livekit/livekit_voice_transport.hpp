#pragma once

#include <hvc/client/voice_transport.hpp>
#include <memory>

namespace hvc::livekit
{
class LiveKitVoiceTransport final : public client::IVoiceTransport
{
  public:
    LiveKitVoiceTransport();
    ~LiveKitVoiceTransport() override;

    LiveKitVoiceTransport(const LiveKitVoiceTransport&) = delete;
    auto operator=(const LiveKitVoiceTransport&) -> LiveKitVoiceTransport& = delete;
    LiveKitVoiceTransport(LiveKitVoiceTransport&&) = delete;
    auto operator=(LiveKitVoiceTransport&&) -> LiveKitVoiceTransport& = delete;

    void setObserver(client::IVoiceTransportObserver* observer) noexcept override;
    [[nodiscard]] auto state() const noexcept -> client::VoiceTransportState override;
    [[nodiscard]] auto connect(std::span<const client::VoiceRoomGrant> grants)
        -> client::VoiceTransportResult override;
    [[nodiscard]] auto disconnect() -> client::VoiceTransportResult override;
    [[nodiscard]] auto startMicrophone(domain::VoiceScope scope)
        -> client::VoiceTransportResult override;
    [[nodiscard]] auto stopMicrophone() -> client::VoiceTransportResult override;
    [[nodiscard]] auto activeTransmissionScope() const noexcept
        -> std::optional<domain::VoiceScope> override;
    [[nodiscard]] auto recordingDevices() const -> std::vector<client::AudioDevice> override;
    [[nodiscard]] auto playoutDevices() const -> std::vector<client::AudioDevice> override;
    [[nodiscard]] auto selectRecordingDevice(const std::string& device_id)
        -> client::VoiceTransportResult override;
    [[nodiscard]] auto selectPlayoutDevice(const std::string& device_id)
        -> client::VoiceTransportResult override;
    [[nodiscard]] auto remoteParticipantCount(domain::VoiceScope scope) const
        -> std::size_t override;
    [[nodiscard]] auto hasRemoteAudio(domain::VoiceScope scope) const -> bool override;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace hvc::livekit
