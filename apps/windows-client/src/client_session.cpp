#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "client_session.hpp"

#include <array>
#include <chrono>
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
        const std::scoped_lock services_lock{services_mutex_};
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
            resetServicesLocked();
            return {false, message, std::nullopt};
        }

        ptt_input_ = std::make_unique<client::AuthorizedPushToTalkInput>(*authorized_client_);
        ptt_input_->setObserver(this);
        binding_engine_.setObserver(ptt_input_.get());
        const auto bindings = defaultBindings();
        const auto bindings_set = binding_engine_.setBindings(bindings);
        if (!bindings_set)
        {
            resetServicesLocked();
            return {false, "The default push-to-talk bindings could not be activated.",
                    std::nullopt};
        }

        raw_input_ = std::make_unique<client::WinRawInputSource>(binding_engine_);
        const auto input_started = raw_input_->start();
        if (!input_started)
        {
            const auto message = "Raw Input could not be started: " + input_started.message;
            resetServicesLocked();
            return {false, message, std::nullopt};
        }

        auto membership = authorized_client_->membership();
        auto directory_result = control_plane_->directory();
        if (!directory_result || !directory_result.value->snapshot.has_value())
        {
            const auto message =
                directory_result.error.has_value()
                    ? directory_result.error->message
                    : std::string{"The server returned no initial directory snapshot."};
            resetServicesLocked();
            return {false, message, std::nullopt};
        }
        auto presence_result = control_plane_->directoryPresence();
        if (!presence_result)
        {
            const auto message = presence_result.error.has_value()
                                     ? presence_result.error->message
                                     : std::string{"The server returned no initial presence."};
            resetServicesLocked();
            return {false, message, std::nullopt};
        }
        auto directory = std::move(*directory_result.value->snapshot);
        auto presence = std::move(*presence_result.value);
        const auto initial_directory_version = directory.version;
        const auto initial_presence_version = presence.version;
        const auto initial_group_id = directory.group_id;
        const auto initial_presence_retry = presence.retry_after;
        membership_refresh_ = std::jthread{[this, initial_directory_version,
                                            initial_presence_version, initial_group_id,
                                            initial_presence_retry](std::stop_token stop_token) {
            std::uint64_t known_version{};
            auto known_directory_version = initial_directory_version;
            auto known_presence_version = initial_presence_version;
            auto known_group_id = initial_group_id;
            auto next_presence_poll = std::chrono::steady_clock::now() + initial_presence_retry;
            if (const auto current = authorized_client_->membership(); current)
            {
                known_version = current->version;
            }
            while (!stop_token.stop_requested())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds{500});
                if (stop_token.stop_requested())
                {
                    return;
                }
                const std::scoped_lock refresh_lock{services_mutex_};
                if (stop_token.stop_requested() || authorized_client_ == nullptr)
                {
                    return;
                }
                const auto refreshed = authorized_client_->refreshAuthorization();
                if (!refreshed)
                {
                    report(SessionEvent{SessionEventKind::error,
                                        client::VoiceTransportState::connected,
                                        std::nullopt,
                                        {},
                                        refreshed.error->code,
                                        voiceSessionMessage(refreshed)});
                    continue;
                }
                const auto current = authorized_client_->membership();
                if (current && current->version > known_version)
                {
                    known_version = current->version;
                    if (current->group_id.value() != known_group_id)
                    {
                        known_group_id = std::string{current->group_id.value()};
                        known_directory_version = 0;
                        known_presence_version = 0;
                    }
                    SessionEvent event{SessionEventKind::membership_updated};
                    event.membership = current;
                    report(std::move(event));
                }

                auto directory_update = control_plane_->directory(
                    known_directory_version == 0 ? std::nullopt
                                                 : std::optional{known_directory_version});
                if (!directory_update)
                {
                    report(SessionEvent{SessionEventKind::error,
                                        client::VoiceTransportState::connected,
                                        std::nullopt,
                                        {},
                                        directory_update.error->code,
                                        directory_update.error->message});
                }
                else if (directory_update.value->snapshot.has_value())
                {
                    known_directory_version = directory_update.value->snapshot->version;
                    known_presence_version = 0;
                    next_presence_poll = std::chrono::steady_clock::now();
                    SessionEvent event{SessionEventKind::directory_updated};
                    event.directory = std::move(*directory_update.value->snapshot);
                    report(std::move(event));
                }

                if (std::chrono::steady_clock::now() < next_presence_poll)
                {
                    continue;
                }
                auto presence_update = control_plane_->directoryPresence(
                    known_presence_version == 0 ? std::nullopt
                                                : std::optional{known_presence_version});
                if (!presence_update && presence_update.error.has_value() &&
                    presence_update.error->code == "presence_snapshot_required")
                {
                    known_presence_version = 0;
                    presence_update = control_plane_->directoryPresence();
                }
                if (!presence_update)
                {
                    report(SessionEvent{SessionEventKind::error,
                                        client::VoiceTransportState::connected,
                                        std::nullopt,
                                        {},
                                        presence_update.error->code,
                                        presence_update.error->message});
                    next_presence_poll = std::chrono::steady_clock::now() + std::chrono::seconds{1};
                    continue;
                }
                known_presence_version = presence_update.value->version;
                next_presence_poll =
                    std::chrono::steady_clock::now() + presence_update.value->retry_after;
                SessionEvent event{SessionEventKind::presence_updated};
                event.presence = std::move(*presence_update.value);
                report(std::move(event));
            }
        }};
        return {true, {}, std::move(membership), std::move(directory), std::move(presence)};
    }
    catch (const std::exception& error)
    {
        const std::scoped_lock services_lock{services_mutex_};
        resetServicesLocked();
        return {false, error.what(), std::nullopt};
    }
}

void ClientSession::disconnect() noexcept
{
    membership_refresh_.request_stop();
    if (membership_refresh_.joinable())
    {
        membership_refresh_.join();
    }
    const std::scoped_lock services_lock{services_mutex_};
    resetServicesLocked();
}

void ClientSession::resetServicesLocked() noexcept
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
    const std::scoped_lock services_lock{services_mutex_};
    return livekit_transport_ == nullptr ? std::vector<client::AudioDevice>{}
                                         : livekit_transport_->recordingDevices();
}

auto ClientSession::playoutDevices() const -> std::vector<client::AudioDevice>
{
    const std::scoped_lock services_lock{services_mutex_};
    return livekit_transport_ == nullptr ? std::vector<client::AudioDevice>{}
                                         : livekit_transport_->playoutDevices();
}

auto ClientSession::selectRecordingDevice(const std::string& device_id)
    -> client::VoiceTransportResult
{
    const std::scoped_lock services_lock{services_mutex_};
    return livekit_transport_ == nullptr ? disconnectedResult()
                                         : livekit_transport_->selectRecordingDevice(device_id);
}

auto ClientSession::selectPlayoutDevice(const std::string& device_id)
    -> client::VoiceTransportResult
{
    const std::scoped_lock services_lock{services_mutex_};
    return livekit_transport_ == nullptr ? disconnectedResult()
                                         : livekit_transport_->selectPlayoutDevice(device_id);
}

auto ClientSession::setAudioEngineConfig(const client::AudioEngineConfig& config)
    -> client::VoiceTransportResult
{
    const std::scoped_lock services_lock{services_mutex_};
    return voice_client_ == nullptr ? disconnectedResult()
                                    : voice_client_->setAudioEngineConfig(config);
}

auto ClientSession::audioEngineConfig() const noexcept -> client::AudioEngineConfig
{
    const std::scoped_lock services_lock{services_mutex_};
    return voice_client_ == nullptr ? client::AudioEngineConfig{}
                                    : voice_client_->audioEngineConfig();
}

auto ClientSession::setBindings(std::span<const client::InputBinding> bindings)
    -> client::InputBindingResult
{
    const std::scoped_lock services_lock{services_mutex_};
    return binding_engine_.setBindings(bindings);
}

auto ClientSession::bindings() const -> std::vector<client::InputBinding>
{
    const std::scoped_lock services_lock{services_mutex_};
    return binding_engine_.bindings();
}

auto ClientSession::inputDevices() const -> std::vector<client::InputDeviceProfile>
{
    const std::scoped_lock services_lock{services_mutex_};
    return binding_engine_.devices();
}

auto ClientSession::setParticipantVolume(const std::string& participant_id, float volume)
    -> client::VoiceTransportResult
{
    const std::scoped_lock services_lock{services_mutex_};
    return voice_client_ == nullptr ? disconnectedResult()
                                    : voice_client_->setParticipantVolume(participant_id, volume);
}

auto ClientSession::setParticipantMuted(const std::string& participant_id, bool muted)
    -> client::VoiceTransportResult
{
    const std::scoped_lock services_lock{services_mutex_};
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

void ClientSession::onVoiceStateChanged(const client::VoiceConnectionEvent& connection_event)
{
    SessionEvent event{SessionEventKind::connection_state, connection_event.state};
    event.connection_event = connection_event;
    report(std::move(event));
}

void ClientSession::onVoiceRemoteEvent(const client::VoiceRemoteEvent& remote_event)
{
    SessionEvent event{SessionEventKind::remote_voice};
    event.scope = remote_event.scope;
    event.participant_id = remote_event.participant_id;
    event.remote_event = remote_event;
    report(std::move(event));
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
