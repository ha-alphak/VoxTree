#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "client_session.hpp"

#include <array>
#include <cstdio>
#include <objbase.h>
#include <stdexcept>
#include <utility>
#include <windows.h>

namespace hvc::windows_client
{
namespace
{
[[nodiscard]] auto newIdentifier() -> std::string
{
    GUID identifier{};
    if (CoCreateGuid(&identifier) != S_OK)
    {
        throw std::runtime_error{"Windows could not create a client identifier"};
    }

    std::array<char, 37> text{};
    const auto written = std::snprintf(
        text.data(), text.size(),
        "%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX", identifier.Data1,
        identifier.Data2, identifier.Data3, identifier.Data4[0], identifier.Data4[1],
        identifier.Data4[2], identifier.Data4[3], identifier.Data4[4], identifier.Data4[5],
        identifier.Data4[6], identifier.Data4[7]);
    if (written != static_cast<int>(text.size() - 1U))
    {
        throw std::runtime_error{"Windows created an invalid client identifier"};
    }
    return text.data();
}

[[nodiscard]] auto defaultBindings() -> std::array<client::InputBinding, 3>
{
    return {
        client::InputBinding{client::PushToTalkAction::team,
                             {{client::InputDeviceKind::keyboard, 0, VK_F9, false, {}}}},
        client::InputBinding{client::PushToTalkAction::specialization,
                             {{client::InputDeviceKind::keyboard, 0, VK_F10, false, {}}}},
        client::InputBinding{client::PushToTalkAction::group,
                             {{client::InputDeviceKind::keyboard, 0, VK_F11, false, {}}}},
    };
}
} // namespace

ClientSession::ClientSession(EventCallback event_callback)
    : event_callback_(std::move(event_callback))
{
}

ClientSession::~ClientSession()
{
    disconnect();
}

auto ClientSession::connect(const std::string& server_url, const std::string& credential)
    -> ConnectResult
{
    if (server_url.empty())
    {
        return {false, "A server URL is required.", std::nullopt};
    }
    if (credential.empty())
    {
        return {false, "A sign-in credential is required.", std::nullopt};
    }

    try
    {
        if (authorized_client_ != nullptr)
        {
            return {false, "The client is already connected.", std::nullopt};
        }

        http_transport_ = std::make_unique<client::WinHttpTransport>(server_url);
        control_plane_ = std::make_unique<client::ControlPlaneClient>(
            *http_transport_, static_cast<client::IClientIdentifierGenerator&>(*this),
            domain::DeviceId{newIdentifier()});
        livekit_transport_ = std::make_unique<livekit::LiveKitVoiceTransport>();
        voice_client_ = std::make_unique<client::VoiceClient>(*livekit_transport_);
        voice_client_->setObserver(this);
        authorized_client_ =
            std::make_unique<client::AuthorizedVoiceClient>(*control_plane_, *voice_client_);

        const auto connected = authorized_client_->connect(credential);
        if (!connected)
        {
            const auto message = voiceSessionMessage(connected);
            disconnect();
            return {false, message, std::nullopt};
        }

        ptt_input_ = std::make_unique<client::AuthorizedPushToTalkInput>(*authorized_client_);
        ptt_input_->setObserver(this);
        binding_engine_.setObserver(ptt_input_.get());
        const auto bindings = defaultBindings();
        const auto bindings_set = binding_engine_.setBindings(bindings);
        if (!bindings_set)
        {
            disconnect();
            return {false, "The default push-to-talk bindings could not be activated.",
                    std::nullopt};
        }

        raw_input_ = std::make_unique<client::WinRawInputSource>(binding_engine_);
        const auto input_started = raw_input_->start();
        if (!input_started)
        {
            const auto message = "Raw Input could not be started: " + input_started.message;
            disconnect();
            return {false, message, std::nullopt};
        }

        auto membership = authorized_client_->membership();
        return {true, {}, std::move(membership)};
    }
    catch (const std::exception& error)
    {
        disconnect();
        return {false, error.what(), std::nullopt};
    }
}

void ClientSession::disconnect() noexcept
{
    if (raw_input_ != nullptr)
    {
        raw_input_->stop();
    }
    binding_engine_.releaseAll();
    binding_engine_.setObserver(nullptr);
    if (ptt_input_ != nullptr)
    {
        ptt_input_->setObserver(nullptr);
    }
    if (voice_client_ != nullptr)
    {
        voice_client_->setObserver(nullptr);
    }
    if (authorized_client_ != nullptr)
    {
        static_cast<void>(authorized_client_->disconnect());
    }

    raw_input_.reset();
    ptt_input_.reset();
    authorized_client_.reset();
    voice_client_.reset();
    livekit_transport_.reset();
    control_plane_.reset();
    http_transport_.reset();
}

auto ClientSession::recordingDevices() const -> std::vector<client::AudioDevice>
{
    return livekit_transport_ == nullptr ? std::vector<client::AudioDevice>{}
                                         : livekit_transport_->recordingDevices();
}

auto ClientSession::playoutDevices() const -> std::vector<client::AudioDevice>
{
    return livekit_transport_ == nullptr ? std::vector<client::AudioDevice>{}
                                         : livekit_transport_->playoutDevices();
}

auto ClientSession::selectRecordingDevice(const std::string& device_id)
    -> client::VoiceTransportResult
{
    return livekit_transport_ == nullptr ? disconnectedResult()
                                         : livekit_transport_->selectRecordingDevice(device_id);
}

auto ClientSession::selectPlayoutDevice(const std::string& device_id)
    -> client::VoiceTransportResult
{
    return livekit_transport_ == nullptr ? disconnectedResult()
                                         : livekit_transport_->selectPlayoutDevice(device_id);
}

auto ClientSession::setAudioEngineConfig(const client::AudioEngineConfig& config)
    -> client::VoiceTransportResult
{
    return voice_client_ == nullptr ? disconnectedResult()
                                    : voice_client_->setAudioEngineConfig(config);
}

auto ClientSession::audioEngineConfig() const noexcept -> client::AudioEngineConfig
{
    return voice_client_ == nullptr ? client::AudioEngineConfig{}
                                    : voice_client_->audioEngineConfig();
}

auto ClientSession::setBindings(std::span<const client::InputBinding> bindings)
    -> client::InputBindingResult
{
    return binding_engine_.setBindings(bindings);
}

auto ClientSession::bindings() const -> std::vector<client::InputBinding>
{
    return binding_engine_.bindings();
}

auto ClientSession::inputDevices() const -> std::vector<client::InputDeviceProfile>
{
    return binding_engine_.devices();
}

auto ClientSession::setParticipantVolume(const std::string& participant_id, float volume)
    -> client::VoiceTransportResult
{
    return voice_client_ == nullptr ? disconnectedResult()
                                    : voice_client_->setParticipantVolume(participant_id, volume);
}

auto ClientSession::setParticipantMuted(const std::string& participant_id, bool muted)
    -> client::VoiceTransportResult
{
    return voice_client_ == nullptr ? disconnectedResult()
                                    : voice_client_->setParticipantMuted(participant_id, muted);
}

auto ClientSession::nextCorrelationId() -> domain::CorrelationId
{
    return domain::CorrelationId{newIdentifier()};
}

auto ClientSession::nextTransmissionId() -> domain::ClientTransmissionId
{
    return domain::ClientTransmissionId{newIdentifier()};
}

void ClientSession::onVoiceStateChanged(client::VoiceTransportState state)
{
    report(SessionEvent{SessionEventKind::connection_state, state});
}

void ClientSession::onSpeakerStarted(domain::VoiceScope scope, const std::string& participant_id)
{
    report(SessionEvent{SessionEventKind::speaker_started, client::VoiceTransportState::connected,
                        scope, participant_id});
}

void ClientSession::onSpeakerStopped(domain::VoiceScope scope, const std::string& participant_id)
{
    report(SessionEvent{SessionEventKind::speaker_stopped, client::VoiceTransportState::connected,
                        scope, participant_id});
}

void ClientSession::onVoiceError(client::VoiceTransportError error, const std::string& message)
{
    report(SessionEvent{SessionEventKind::error,
                        client::VoiceTransportState::disconnected,
                        std::nullopt,
                        {},
                        std::to_string(static_cast<std::uint8_t>(error)),
                        message});
}

void ClientSession::onPushToTalkInputResult(client::PushToTalkAction action, bool pressed,
                                            const client::VoiceSessionResult& result)
{
    if (!result)
    {
        const auto error_code =
            result.error.has_value() ? result.error->code : std::string{"client_unknown"};
        report(SessionEvent{SessionEventKind::error,
                            client::VoiceTransportState::connected,
                            client::voiceScopeFor(action),
                            {},
                            error_code,
                            voiceSessionMessage(result)});
        return;
    }
    report(SessionEvent{pressed ? SessionEventKind::transmission_started
                                : SessionEventKind::transmission_stopped,
                        client::VoiceTransportState::connected, client::voiceScopeFor(action)});
}

void ClientSession::report(SessionEvent event) const
{
    if (event_callback_)
    {
        event_callback_(std::move(event));
    }
}

auto ClientSession::voiceSessionMessage(const client::VoiceSessionResult& result) -> std::string
{
    if (result.error.has_value())
    {
        if (!result.error->message.empty())
        {
            return result.error->message;
        }
        return result.error->code;
    }
    return "Unknown client failure.";
}

auto ClientSession::disconnectedResult() -> client::VoiceTransportResult
{
    return client::VoiceTransportResult::failure(client::VoiceTransportError::invalid_state,
                                                 "The client is not connected.");
}
} // namespace hvc::windows_client
