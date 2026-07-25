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

[[nodiscard]] auto scopeName(domain::VoiceScope scope) -> std::string
{
    switch (scope)
    {
    case domain::VoiceScope::team:
        return "Team";
    case domain::VoiceScope::specialization:
        return "Specialization";
    case domain::VoiceScope::group:
        return "Group";
    }
    return "Unknown";
}

[[nodiscard]] auto actionName(client::PushToTalkAction action) -> std::string
{
    return scopeName(client::voiceScopeFor(action));
}
} // namespace

ClientSession::ClientSession(StatusCallback status_callback)
    : status_callback_(std::move(status_callback))
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
        return {false, "Eine Server-URL ist erforderlich.", std::nullopt};
    }
    if (credential.empty())
    {
        return {false, "Ein Anmelde-Credential ist erforderlich.", std::nullopt};
    }

    try
    {
        if (authorized_client_ != nullptr)
        {
            return {false, "Der Client ist bereits verbunden.", std::nullopt};
        }

        report("Control Plane wird kontaktiert ...");
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
            return {false, "Die Standard-PTT-Belegungen konnten nicht aktiviert werden.",
                    std::nullopt};
        }

        raw_input_ = std::make_unique<client::WinRawInputSource>(binding_engine_);
        const auto input_started = raw_input_->start();
        if (!input_started)
        {
            const auto message =
                "Raw Input konnte nicht gestartet werden: " + input_started.message;
            disconnect();
            return {false, message, std::nullopt};
        }

        auto membership = authorized_client_->membership();
        report("Verbunden und bereit.");
        return {true, "Verbunden und bereit.", std::move(membership)};
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
    switch (state)
    {
    case client::VoiceTransportState::disconnected:
        report("Voice-Transport getrennt.");
        break;
    case client::VoiceTransportState::connecting:
        report("Voice-Räume werden verbunden ...");
        break;
    case client::VoiceTransportState::connected:
        report("Voice-Räume verbunden.");
        break;
    case client::VoiceTransportState::reconnecting:
        report("Voice-Transport wird neu verbunden; PTT bleibt beendet.");
        break;
    }
}

void ClientSession::onSpeakerStarted(domain::VoiceScope scope, const std::string& participant_id)
{
    report(scopeName(scope) + ": Sprecher " + participant_id + " ist aktiv.");
}

void ClientSession::onSpeakerStopped(domain::VoiceScope scope, const std::string& participant_id)
{
    report(scopeName(scope) + ": Sprecher " + participant_id + " ist nicht mehr aktiv.");
}

void ClientSession::onVoiceError(client::VoiceTransportError, const std::string& message)
{
    report("Voice-Fehler: " + message);
}

void ClientSession::onPushToTalkInputResult(client::PushToTalkAction action, bool pressed,
                                            const client::VoiceSessionResult& result)
{
    if (!result)
    {
        report(actionName(action) + "-PTT: " + voiceSessionMessage(result));
        return;
    }
    report(actionName(action) + (pressed ? "-PTT sendet." : "-PTT beendet."));
}

void ClientSession::report(std::string message) const
{
    if (status_callback_)
    {
        status_callback_(std::move(message));
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
    return "Unbekannter Clientfehler.";
}
} // namespace hvc::windows_client
