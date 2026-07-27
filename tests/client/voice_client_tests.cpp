#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdio>
#include <exception>
#include <hvc/client/voice_client.hpp>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
using namespace hvc::client;
using hvc::domain::VoiceScope;

class FakeVoiceTransport final : public IVoiceTransport
{
  public:
    void setObserver(IVoiceTransportObserver* observer) noexcept override
    {
        observer_ = observer;
    }

    [[nodiscard]] auto state() const noexcept -> VoiceTransportState override
    {
        return state_;
    }

    [[nodiscard]] auto connect(std::span<const VoiceRoomGrant> grants)
        -> VoiceTransportResult override
    {
        ++connect_calls;
        connected_grant_count = grants.size();
        state_ = VoiceTransportState::connected;
        if (observer_ != nullptr)
        {
            observer_->onTransportStateChanged(state_);
        }
        return VoiceTransportResult::success();
    }

    [[nodiscard]] auto disconnect() -> VoiceTransportResult override
    {
        ++disconnect_calls;
        state_ = VoiceTransportState::disconnected;
        active_scope_.reset();
        if (observer_ != nullptr)
        {
            observer_->onTransportStateChanged(state_);
        }
        return VoiceTransportResult::success();
    }

    [[nodiscard]] auto startMicrophone(VoiceScope scope) -> VoiceTransportResult override
    {
        ++start_calls;
        {
            std::unique_lock lock{publication_mutex};
            start_entered = true;
            publication_changed.notify_all();
            publication_changed.wait(lock,
                                     [this] { return !block_microphone_start || finish_start; });
            finish_start = false;
            start_entered = false;
        }
        if (state_ != VoiceTransportState::connected || active_scope_.has_value())
        {
            return VoiceTransportResult::failure(VoiceTransportError::invalid_state,
                                                 "cannot start microphone");
        }
        active_scope_ = scope;
        return VoiceTransportResult::success();
    }

    [[nodiscard]] auto stopMicrophone() -> VoiceTransportResult override
    {
        ++stop_calls;
        active_scope_.reset();
        return VoiceTransportResult::success();
    }

    [[nodiscard]] auto activeTransmissionScope() const noexcept
        -> std::optional<VoiceScope> override
    {
        return active_scope_;
    }

    [[nodiscard]] auto recordingDevices() const -> std::vector<AudioDevice> override
    {
        return {};
    }

    [[nodiscard]] auto playoutDevices() const -> std::vector<AudioDevice> override
    {
        return {};
    }

    [[nodiscard]] auto selectRecordingDevice(const std::string&) -> VoiceTransportResult override
    {
        return VoiceTransportResult::success();
    }

    [[nodiscard]] auto selectPlayoutDevice(const std::string&) -> VoiceTransportResult override
    {
        return VoiceTransportResult::success();
    }

    [[nodiscard]] auto configureRemoteAudio(VoiceScope scope, const std::string& participant_id,
                                            bool admitted, float gain)
        -> VoiceTransportResult override
    {
        const auto match = [&](const auto& playback_entry) {
            return std::get<0>(playback_entry) == scope &&
                   std::get<1>(playback_entry) == participant_id;
        };
        const auto iterator = std::ranges::find_if(playback, match);
        const auto value = std::tuple{scope, participant_id, admitted, gain};
        if (iterator == playback.end())
        {
            playback.push_back(value);
        }
        else
        {
            *iterator = value;
        }
        return VoiceTransportResult::success();
    }

    [[nodiscard]] auto remoteParticipantCount(VoiceScope) const -> std::size_t override
    {
        return 0;
    }

    [[nodiscard]] auto hasRemoteAudio(VoiceScope) const -> bool override
    {
        return false;
    }

    void reconnect()
    {
        if (active_scope_.has_value())
        {
            active_scope_.reset();
            ++stop_calls;
        }
        state_ = VoiceTransportState::reconnecting;
        if (observer_ != nullptr)
        {
            observer_->onTransportStateChanged(state_);
        }
        state_ = VoiceTransportState::connected;
        if (observer_ != nullptr)
        {
            observer_->onTransportStateChanged(state_);
        }
    }

    void makeAudioAvailable(VoiceScope scope, const std::string& participant_id) const
    {
        observer_->onRemoteAudioAvailable(scope, participant_id);
    }

    void makeAudioUnavailable(VoiceScope scope, const std::string& participant_id) const
    {
        observer_->onRemoteAudioUnavailable(scope, participant_id);
    }

    void connectParticipant(VoiceScope scope, const std::string& participant_id) const
    {
        observer_->onRemoteParticipantConnected(scope, participant_id);
    }

    void disconnectParticipant(VoiceScope scope, const std::string& participant_id) const
    {
        observer_->onRemoteParticipantDisconnected(scope, participant_id);
    }

    void startSpeaker(VoiceScope scope, const std::string& participant_id) const
    {
        observer_->onRemoteAudioStarted(scope, participant_id);
    }

    void stopSpeaker(VoiceScope scope, const std::string& participant_id) const
    {
        observer_->onRemoteAudioStopped(scope, participant_id);
    }

    void waitForMicrophoneStart()
    {
        std::unique_lock lock{publication_mutex};
        publication_changed.wait(lock, [this] { return start_entered; });
    }

    void completeMicrophoneStart()
    {
        const std::scoped_lock lock{publication_mutex};
        finish_start = true;
        publication_changed.notify_all();
    }

    IVoiceTransportObserver* observer_{nullptr};
    VoiceTransportState state_{VoiceTransportState::disconnected};
    std::optional<VoiceScope> active_scope_;
    std::size_t connected_grant_count{0};
    int connect_calls{0};
    int disconnect_calls{0};
    int start_calls{0};
    int stop_calls{0};
    bool block_microphone_start{false};
    bool start_entered{false};
    bool finish_start{false};
    std::mutex publication_mutex;
    std::condition_variable publication_changed;
    std::vector<std::tuple<VoiceScope, std::string, bool, float>> playback;
};

class RecordingObserver final : public IVoiceClientObserver
{
  public:
    void onVoiceStateChanged(const VoiceConnectionEvent& event) override
    {
        connections.push_back(event);
    }

    void onVoiceRemoteEvent(const VoiceRemoteEvent& event) override
    {
        remote.push_back(event);
    }

    void onVoiceError(VoiceTransportError, const std::string&) override
    {
        ++errors;
    }

    std::vector<VoiceConnectionEvent> connections;
    std::vector<VoiceRemoteEvent> remote;
    int errors{0};
};

[[nodiscard]] auto validGrants() -> std::array<VoiceRoomGrant, 3>
{
    return {VoiceRoomGrant{VoiceScope::team, "ws://voice", "team-token"},
            VoiceRoomGrant{VoiceScope::specialization, "ws://voice", "specialization-token"},
            VoiceRoomGrant{VoiceScope::group, "ws://voice", "group-token"}};
}

auto testRequiresUniqueAuthorizedScopes() -> bool
{
    FakeVoiceTransport transport;
    VoiceClient client{transport};
    const std::array<VoiceRoomGrant, 0> empty{};
    if (client.connect(empty) || transport.connect_calls != 0)
    {
        return false;
    }

    auto duplicate = validGrants();
    duplicate[2].scope = VoiceScope::team;
    if (client.connect(duplicate) || transport.connect_calls != 0)
    {
        return false;
    }

    auto missing_token = validGrants();
    missing_token[1].token.clear();
    if (client.connect(missing_token) || transport.connect_calls != 0)
    {
        return false;
    }

    const std::array team_only{VoiceRoomGrant{VoiceScope::team, "ws://voice", "team-token"}};
    return client.connect(team_only) && transport.connected_grant_count == 1;
}

auto testConnectAndPttLifecycle() -> bool
{
    FakeVoiceTransport transport;
    VoiceClient client{transport};
    const auto grants = validGrants();
    if (!client.connect(grants) || client.state() != VoiceTransportState::connected ||
        transport.connected_grant_count != grants.size() ||
        !client.pressPushToTalk(VoiceScope::group) ||
        client.activeTransmissionScope() != VoiceScope::group ||
        client.pressPushToTalk(VoiceScope::team) || !client.releasePushToTalk() ||
        client.activeTransmissionScope().has_value())
    {
        return false;
    }

    return transport.start_calls == 1 && transport.stop_calls == 1 && client.disconnect() &&
           client.state() == VoiceTransportState::disconnected;
}

auto testReconnectStopsAndNeverResumesPtt() -> bool
{
    FakeVoiceTransport transport;
    VoiceClient client{transport};
    const auto grants = validGrants();
    if (!client.connect(grants) || !client.pressPushToTalk(VoiceScope::specialization))
    {
        return false;
    }

    transport.reconnect();
    return client.state() == VoiceTransportState::connected &&
           !client.activeTransmissionScope().has_value() &&
           !transport.activeTransmissionScope().has_value() && transport.stop_calls == 1 &&
           transport.start_calls == 1;
}

auto testCancelsPendingPublicationAcrossEveryScope() -> bool
{
    FakeVoiceTransport transport;
    transport.block_microphone_start = true;
    VoiceClient client{transport};
    const auto grants = validGrants();
    if (!client.connect(grants))
    {
        return false;
    }

    constexpr std::array scopes{
        VoiceScope::team,
        VoiceScope::specialization,
        VoiceScope::group,
    };
    constexpr int cycles_per_scope = 100;
    for (const auto scope : scopes)
    {
        for (auto cycle = 0; cycle < cycles_per_scope; ++cycle)
        {
            VoiceTransportResult start_result;
            VoiceTransportResult stop_result;
            std::jthread starter{[&] { start_result = client.pressPushToTalk(scope); }};
            transport.waitForMicrophoneStart();
            if (client.microphonePublicationState() != MicrophonePublicationState::starting)
            {
                return false;
            }
            std::jthread stopper{[&] { stop_result = client.releasePushToTalk(); }};
            while (client.microphonePublicationState() != MicrophonePublicationState::stopping)
            {
                std::this_thread::yield();
            }
            transport.completeMicrophoneStart();
            starter.join();
            stopper.join();
            if (start_result || !stop_result ||
                client.microphonePublicationState() != MicrophonePublicationState::idle ||
                client.activeTransmissionScope().has_value() ||
                transport.activeTransmissionScope().has_value())
            {
                return false;
            }
        }
    }

    return client.microphonePublicationGeneration() ==
               static_cast<std::uint64_t>(scopes.size() * cycles_per_scope) &&
           transport.start_calls == static_cast<int>(scopes.size()) * cycles_per_scope &&
           transport.stop_calls == static_cast<int>(scopes.size()) * cycles_per_scope;
}

auto testDisconnectCancelsPendingPublication() -> bool
{
    FakeVoiceTransport transport;
    transport.block_microphone_start = true;
    VoiceClient client{transport};
    const auto grants = validGrants();
    if (!client.connect(grants))
    {
        return false;
    }

    VoiceTransportResult start_result;
    VoiceTransportResult disconnect_result;
    std::jthread starter{
        [&] { start_result = client.pressPushToTalk(VoiceScope::specialization); }};
    transport.waitForMicrophoneStart();
    std::jthread disconnector{[&] { disconnect_result = client.disconnect(); }};
    while (client.microphonePublicationState() != MicrophonePublicationState::stopping)
    {
        std::this_thread::yield();
    }
    transport.completeMicrophoneStart();
    starter.join();
    disconnector.join();

    return !start_result && disconnect_result &&
           client.state() == VoiceTransportState::disconnected &&
           client.microphonePublicationState() == MicrophonePublicationState::idle &&
           !client.activeTransmissionScope().has_value() &&
           !transport.activeTransmissionScope().has_value() && transport.start_calls == 1 &&
           transport.stop_calls == 1 && transport.disconnect_calls == 1;
}

[[nodiscard]] auto playbackFor(const VoiceClient& client, VoiceScope scope,
                               const std::string& participant_id)
    -> std::optional<RemoteAudioPlayback>
{
    const auto snapshot = client.remoteAudioPlayback();
    const auto iterator = std::ranges::find_if(snapshot, [&](const auto& playback) {
        return playback.scope == scope && playback.participant_id == participant_id;
    });
    return iterator == snapshot.end() ? std::nullopt : std::optional{*iterator};
}

auto testPriorityAdmissionAndDucking() -> bool
{
    FakeVoiceTransport transport;
    VoiceClient client{transport};
    auto config = client.audioEngineConfig();
    config.maximum_streams = 3;
    config.maximum_streams_per_scope = {1, 2, 2};
    config.team_gain_under_specialization = 0.4F;
    config.team_gain_under_group = 0.2F;
    config.specialization_gain_under_group = 0.3F;
    if (!client.setAudioEngineConfig(config))
    {
        return false;
    }

    transport.makeAudioAvailable(VoiceScope::team, "team-one");
    transport.makeAudioAvailable(VoiceScope::team, "team-two");
    transport.makeAudioAvailable(VoiceScope::specialization, "specialization-one");

    const auto team_one = playbackFor(client, VoiceScope::team, "team-one");
    const auto team_two = playbackFor(client, VoiceScope::team, "team-two");
    const auto specialization =
        playbackFor(client, VoiceScope::specialization, "specialization-one");
    if (!team_one.has_value() || !team_two.has_value() || !specialization.has_value() ||
        !team_one->admitted || team_two->admitted || !specialization->admitted ||
        team_one->gain != 0.4F || specialization->gain != 1.0F)
    {
        return false;
    }

    transport.makeAudioAvailable(VoiceScope::group, "group-one");
    const auto group = playbackFor(client, VoiceScope::group, "group-one");
    const auto specialization_ducked =
        playbackFor(client, VoiceScope::specialization, "specialization-one");
    const auto team_ducked = playbackFor(client, VoiceScope::team, "team-one");
    if (!group.has_value() || !group->admitted || group->gain != 1.0F ||
        !specialization_ducked.has_value() || !specialization_ducked->admitted ||
        specialization_ducked->gain != 0.3F || !team_ducked.has_value() || !team_ducked->admitted ||
        team_ducked->gain != 0.2F)
    {
        return false;
    }

    config.maximum_streams = 2;
    if (!client.setAudioEngineConfig(config))
    {
        return false;
    }
    const auto team_displaced = playbackFor(client, VoiceScope::team, "team-one");
    return team_displaced.has_value() && !team_displaced->admitted;
}

auto testMuteBlockVolumeAndReadmission() -> bool
{
    FakeVoiceTransport transport;
    VoiceClient client{transport};
    auto config = client.audioEngineConfig();
    config.maximum_streams = 1;
    config.maximum_streams_per_scope = {1, 1, 1};
    if (!client.setAudioEngineConfig(config))
    {
        return false;
    }

    transport.makeAudioAvailable(VoiceScope::group, "first");
    transport.makeAudioAvailable(VoiceScope::group, "second");
    if (!client.setParticipantVolume("first", 0.6F))
    {
        return false;
    }
    auto first = playbackFor(client, VoiceScope::group, "first");
    auto second = playbackFor(client, VoiceScope::group, "second");
    if (!first.has_value() || !first->admitted || first->gain != 0.6F || !second.has_value() ||
        second->admitted)
    {
        return false;
    }

    if (!client.setParticipantMuted("first", true))
    {
        return false;
    }
    first = playbackFor(client, VoiceScope::group, "first");
    second = playbackFor(client, VoiceScope::group, "second");
    if (!first.has_value() || first->admitted || !second.has_value() || !second->admitted)
    {
        return false;
    }

    if (!client.setParticipantBlocked("second", true) ||
        !client.setParticipantMuted("first", false))
    {
        return false;
    }
    first = playbackFor(client, VoiceScope::group, "first");
    second = playbackFor(client, VoiceScope::group, "second");
    if (!first.has_value() || !first->admitted || !second.has_value() || second->admitted)
    {
        return false;
    }

    transport.makeAudioUnavailable(VoiceScope::group, "first");
    return !playbackFor(client, VoiceScope::group, "first").has_value() &&
           !client.setParticipantVolume("", 0.5F) && !client.setParticipantVolume("second", 1.1F);
}

auto testOrderedParticipantAndAudioEvents() -> bool
{
    FakeVoiceTransport transport;
    VoiceClient client{transport};
    RecordingObserver observer;
    client.setObserver(&observer);
    const auto grants = validGrants();
    if (!client.connect(grants) || observer.connections.size() != 1 ||
        observer.connections.back().generation != 1)
    {
        return false;
    }
    transport.connectParticipant(VoiceScope::team, "speaker-1");
    transport.connectParticipant(VoiceScope::group, "speaker-1");
    transport.makeAudioAvailable(VoiceScope::group, "speaker-1");
    transport.startSpeaker(VoiceScope::group, "speaker-1");
    transport.stopSpeaker(VoiceScope::group, "speaker-1");
    transport.makeAudioUnavailable(VoiceScope::group, "speaker-1");
    transport.disconnectParticipant(VoiceScope::team, "speaker-1");
    if (observer.remote.size() != 7)
    {
        return false;
    }
    for (std::size_t index = 1; index < observer.remote.size(); ++index)
    {
        if (observer.remote[index].sequence <= observer.remote[index - 1].sequence ||
            observer.remote[index].generation != 1)
        {
            return false;
        }
    }
    if (observer.remote.front().kind != VoiceRemoteEventKind::participant_connected ||
        observer.remote[2].kind != VoiceRemoteEventKind::audio_available ||
        observer.remote[3].kind != VoiceRemoteEventKind::speaker_started ||
        observer.remote.back().kind != VoiceRemoteEventKind::participant_disconnected)
    {
        return false;
    }
    transport.reconnect();
    return observer.connections.size() == 3 &&
           observer.connections[1].state == VoiceTransportState::reconnecting &&
           observer.connections[1].generation == 2 && observer.connections[2].generation == 2 &&
           observer.connections[2].sequence > observer.connections[1].sequence;
}
} // namespace

auto main() noexcept -> int
{
    try
    {
        if (!testRequiresUniqueAuthorizedScopes() || !testConnectAndPttLifecycle() ||
            !testReconnectStopsAndNeverResumesPtt() ||
            !testCancelsPendingPublicationAcrossEveryScope() ||
            !testDisconnectCancelsPendingPublication() || !testPriorityAdmissionAndDucking() ||
            !testMuteBlockVolumeAndReadmission() || !testOrderedParticipantAndAudioEvents())
        {
            std::fputs("A voice-client assertion failed.\n", stderr);
            return 1;
        }
    }
    catch (const std::exception& error)
    {
        std::fputs("Unexpected exception: ", stderr);
        std::fputs(error.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
    return 0;
}
