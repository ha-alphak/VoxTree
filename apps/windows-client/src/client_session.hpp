#pragma once

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
#include <string>

namespace hvc::windows_client
{
struct ConnectResult final
{
    bool successful{false};
    std::string message;
    std::optional<client::MembershipView> membership;
};

class ClientSession final : private client::IClientIdentifierGenerator,
                            private client::IVoiceClientObserver,
                            private client::IPushToTalkInputObserver
{
  public:
    using StatusCallback = std::function<void(std::string)>;

    explicit ClientSession(StatusCallback status_callback);
    ~ClientSession() override;

    ClientSession(const ClientSession&) = delete;
    auto operator=(const ClientSession&) -> ClientSession& = delete;
    ClientSession(ClientSession&&) = delete;
    auto operator=(ClientSession&&) -> ClientSession& = delete;

    [[nodiscard]] auto connect(const std::string& server_url, const std::string& credential)
        -> ConnectResult;
    void disconnect() noexcept;

  private:
    [[nodiscard]] auto nextCorrelationId() -> domain::CorrelationId override;
    [[nodiscard]] auto nextTransmissionId() -> domain::ClientTransmissionId override;

    void onVoiceStateChanged(client::VoiceTransportState state) override;
    void onSpeakerStarted(domain::VoiceScope scope, const std::string& participant_id) override;
    void onSpeakerStopped(domain::VoiceScope scope, const std::string& participant_id) override;
    void onVoiceError(client::VoiceTransportError error, const std::string& message) override;
    void onPushToTalkInputResult(client::PushToTalkAction action, bool pressed,
                                 const client::VoiceSessionResult& result) override;

    void report(std::string message) const;
    [[nodiscard]] static auto voiceSessionMessage(const client::VoiceSessionResult& result)
        -> std::string;

    StatusCallback status_callback_;
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
