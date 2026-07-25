#pragma once

#include <cstddef>
#include <cstdint>
#include <hvc/domain/model.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hvc::client
{
enum class VoiceTransportState : std::uint8_t
{
    disconnected,
    connecting,
    connected,
    reconnecting
};

enum class VoiceTransportError : std::uint8_t
{
    none,
    invalid_argument,
    invalid_state,
    connection_failed,
    audio_device_unavailable,
    audio_device_switch_failed,
    publication_failed,
    internal_error
};

struct VoiceTransportResult final
{
    [[nodiscard]] static auto success() -> VoiceTransportResult;
    [[nodiscard]] static auto failure(VoiceTransportError error, std::string message)
        -> VoiceTransportResult;
    [[nodiscard]] explicit operator bool() const noexcept;

    VoiceTransportError error{VoiceTransportError::none};
    std::string message;
};

struct VoiceRoomGrant final
{
    domain::VoiceScope scope{domain::VoiceScope::team};
    std::string url;
    std::string token;
};

struct AudioDevice final
{
    std::string id;
    std::string display_name;
};

class IVoiceTransportObserver
{
  public:
    IVoiceTransportObserver() = default;
    IVoiceTransportObserver(const IVoiceTransportObserver&) = delete;
    auto operator=(const IVoiceTransportObserver&) -> IVoiceTransportObserver& = delete;
    IVoiceTransportObserver(IVoiceTransportObserver&&) = delete;
    auto operator=(IVoiceTransportObserver&&) -> IVoiceTransportObserver& = delete;
    virtual ~IVoiceTransportObserver() = default;

    virtual void onTransportStateChanged(VoiceTransportState state) = 0;
    virtual void onRemoteParticipantConnected(domain::VoiceScope scope,
                                              const std::string& participant_id) = 0;
    virtual void onRemoteParticipantDisconnected(domain::VoiceScope scope,
                                                 const std::string& participant_id) = 0;
    virtual void onRemoteAudioStarted(domain::VoiceScope scope,
                                      const std::string& participant_id) = 0;
    virtual void onRemoteAudioStopped(domain::VoiceScope scope,
                                      const std::string& participant_id) = 0;
    virtual void onTransportError(VoiceTransportError error, const std::string& message) = 0;
};

class IVoiceTransport
{
  public:
    IVoiceTransport() = default;
    IVoiceTransport(const IVoiceTransport&) = delete;
    auto operator=(const IVoiceTransport&) -> IVoiceTransport& = delete;
    IVoiceTransport(IVoiceTransport&&) = delete;
    auto operator=(IVoiceTransport&&) -> IVoiceTransport& = delete;
    virtual ~IVoiceTransport() = default;

    virtual void setObserver(IVoiceTransportObserver* observer) noexcept = 0;
    [[nodiscard]] virtual auto state() const noexcept -> VoiceTransportState = 0;
    [[nodiscard]] virtual auto connect(std::span<const VoiceRoomGrant> grants)
        -> VoiceTransportResult = 0;
    [[nodiscard]] virtual auto disconnect() -> VoiceTransportResult = 0;

    [[nodiscard]] virtual auto startMicrophone(domain::VoiceScope scope)
        -> VoiceTransportResult = 0;
    [[nodiscard]] virtual auto stopMicrophone() -> VoiceTransportResult = 0;
    [[nodiscard]] virtual auto activeTransmissionScope() const noexcept
        -> std::optional<domain::VoiceScope> = 0;

    [[nodiscard]] virtual auto recordingDevices() const -> std::vector<AudioDevice> = 0;
    [[nodiscard]] virtual auto playoutDevices() const -> std::vector<AudioDevice> = 0;
    [[nodiscard]] virtual auto selectRecordingDevice(const std::string& device_id)
        -> VoiceTransportResult = 0;
    [[nodiscard]] virtual auto selectPlayoutDevice(const std::string& device_id)
        -> VoiceTransportResult = 0;

    [[nodiscard]] virtual auto remoteParticipantCount(domain::VoiceScope scope) const
        -> std::size_t = 0;
    [[nodiscard]] virtual auto hasRemoteAudio(domain::VoiceScope scope) const -> bool = 0;
};
} // namespace hvc::client
