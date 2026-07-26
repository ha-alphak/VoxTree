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

/// Assemble the Windows UI shell and its client-side service graph.
namespace hvc::windows_client
{
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
 * reports asynchronous status through a callback supplied by the UI.
 */
class ClientSession final : private client::IClientIdentifierGenerator,
                            private client::IVoiceClientObserver,
                            private client::IPushToTalkInputObserver
{
  public:
    /// Callback used to deliver human-readable session status to the UI.
    using StatusCallback = std::function<void(std::string)>;

    /**
     * Construct a disconnected client session.
     *
     * @param status_callback Callback invoked for state, speaker, and error
     *     messages.
     */
    explicit ClientSession(StatusCallback status_callback);
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
     * @param server_url Absolute HTTPS control-plane base URL.
     * @param credential External credential accepted by the server.
     * @returns Connection outcome and authoritative membership when successful.
     */
    [[nodiscard]] auto connect(const std::string& server_url, const std::string& credential)
        -> ConnectResult;
    /// Disconnect all services and return to an empty session state.
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
