#pragma once

#include <hvc/client/voice_transport.hpp>
#include <mutex>
#include <optional>
#include <span>
#include <string>

namespace hvc::client
{
class IVoiceClientObserver
{
  public:
    IVoiceClientObserver() = default;
    IVoiceClientObserver(const IVoiceClientObserver&) = delete;
    auto operator=(const IVoiceClientObserver&) -> IVoiceClientObserver& = delete;
    IVoiceClientObserver(IVoiceClientObserver&&) = delete;
    auto operator=(IVoiceClientObserver&&) -> IVoiceClientObserver& = delete;
    virtual ~IVoiceClientObserver() = default;

    virtual void onVoiceStateChanged(VoiceTransportState state) = 0;
    virtual void onSpeakerStarted(domain::VoiceScope scope, const std::string& participant_id) = 0;
    virtual void onSpeakerStopped(domain::VoiceScope scope, const std::string& participant_id) = 0;
    virtual void onVoiceError(VoiceTransportError error, const std::string& message) = 0;
};

class VoiceClient final : private IVoiceTransportObserver
{
  public:
    explicit VoiceClient(IVoiceTransport& transport);
    ~VoiceClient() override;

    VoiceClient(const VoiceClient&) = delete;
    auto operator=(const VoiceClient&) -> VoiceClient& = delete;
    VoiceClient(VoiceClient&&) = delete;
    auto operator=(VoiceClient&&) -> VoiceClient& = delete;

    void setObserver(IVoiceClientObserver* observer) noexcept;
    [[nodiscard]] auto state() const noexcept -> VoiceTransportState;
    [[nodiscard]] auto connect(std::span<const VoiceRoomGrant> grants) -> VoiceTransportResult;
    [[nodiscard]] auto disconnect() -> VoiceTransportResult;
    [[nodiscard]] auto pressPushToTalk(domain::VoiceScope scope) -> VoiceTransportResult;
    [[nodiscard]] auto releasePushToTalk() -> VoiceTransportResult;
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
