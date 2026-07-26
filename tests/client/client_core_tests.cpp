#include <algorithm>
#include <cstdio>
#include <exception>
#include <hvc/client_core.hpp>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
using hvc::client_core::ClientCore;
using hvc::client_core::ConnectionState;
using hvc::client_core::Event;
using hvc::client_core::EventKind;
using hvc::client_core::Membership;
using hvc::client_core::Result;
using hvc::client_core::RoomGrant;
using hvc::client_core::Scope;
using hvc::client_core::Transport;
using hvc::client_core::TransportError;
using hvc::client_core::TransportObserver;
using hvc::client_core::TransportResult;

void check(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error{message};
    }
}

class FakeTransport final : public Transport
{
  public:
    [[nodiscard]] auto setObserver(TransportObserver* observer) -> TransportResult override
    {
        observer_ = observer;
        return {};
    }

    [[nodiscard]] auto state() const noexcept -> ConnectionState override
    {
        return state_;
    }

    [[nodiscard]] auto connect(std::span<const RoomGrant> grants) -> TransportResult override
    {
        grants_.assign(grants.begin(), grants.end());
        state_ = ConnectionState::connecting;
        observer_->onConnectionStateChanged(state_);
        state_ = ConnectionState::connected;
        observer_->onConnectionStateChanged(state_);
        return {};
    }

    [[nodiscard]] auto disconnect() -> TransportResult override
    {
        active_scope_.reset();
        state_ = ConnectionState::disconnected;
        observer_->onConnectionStateChanged(state_);
        return {};
    }

    [[nodiscard]] auto startMicrophone(Scope scope) -> TransportResult override
    {
        if (state_ != ConnectionState::connected)
        {
            return {TransportError::invalid_state, "transport is disconnected"};
        }
        active_scope_ = scope;
        return {};
    }

    [[nodiscard]] auto stopMicrophone() -> TransportResult override
    {
        if (!active_scope_.has_value())
        {
            return {TransportError::invalid_state, "microphone is already stopped"};
        }
        active_scope_.reset();
        return {};
    }

    [[nodiscard]] auto activeMicrophoneScope() const noexcept -> std::optional<Scope> override
    {
        return active_scope_;
    }

    [[nodiscard]] auto configureRemoteAudio(Scope scope, const std::string& participant_id,
                                            bool admitted, float gain) -> TransportResult override
    {
        configured_scope_ = scope;
        configured_participant_ = participant_id;
        admitted_ = admitted;
        gain_ = gain;
        return {};
    }

    void startSpeaker(Scope scope, const std::string& participant_id)
    {
        observer_->onRemoteAudioAvailable(scope, participant_id);
        observer_->onSpeakerStarted(scope, participant_id);
    }

    void stopSpeaker(Scope scope, const std::string& participant_id)
    {
        observer_->onSpeakerStopped(scope, participant_id);
        observer_->onRemoteAudioUnavailable(scope, participant_id);
    }

    void reconnect()
    {
        active_scope_.reset();
        state_ = ConnectionState::reconnecting;
        observer_->onConnectionStateChanged(state_);
        state_ = ConnectionState::connected;
        observer_->onConnectionStateChanged(state_);
    }

    void failAsynchronously()
    {
        observer_->onError(TransportError::connection_failed, "simulated transport failure");
    }

    [[nodiscard]] auto observerAttached() const noexcept -> bool
    {
        return observer_ != nullptr;
    }

    [[nodiscard]] auto grants() const -> const std::vector<RoomGrant>&
    {
        return grants_;
    }

    [[nodiscard]] auto configuredParticipant() const -> const std::string&
    {
        return configured_participant_;
    }

    [[nodiscard]] auto admitted() const noexcept -> bool
    {
        return admitted_;
    }

  private:
    TransportObserver* observer_{nullptr};
    ConnectionState state_{ConnectionState::disconnected};
    std::optional<Scope> active_scope_;
    std::vector<RoomGrant> grants_;
    Scope configured_scope_{Scope::team};
    std::string configured_participant_;
    bool admitted_{false};
    float gain_{0.0F};
};

void runVersionAndConfigurationChecks()
{
    check(hvc_client_core_api_version() == HVC_CLIENT_CORE_ABI_VERSION,
          "loaded client-core ABI version does not match the header");

    hvc_client_core* core = nullptr;
    hvc_client_core_config_v1 config{};
    config.struct_size = sizeof(config);
    config.abi_version = UINT32_C(0x00020000);
    check(hvc_client_core_create(&config, &core) == HVC_CLIENT_CORE_RESULT_INCOMPATIBLE_ABI,
          "an incompatible ABI major version was accepted");
    check(core == nullptr, "failed creation returned a handle");

    config.abi_version = UINT32_C(0x00010001);
    check(hvc_client_core_create(&config, &core) == HVC_CLIENT_CORE_RESULT_INCOMPATIBLE_ABI,
          "a newer unsupported ABI minor version was accepted");
}

void runCppWrapperScenario()
{
    FakeTransport transport;
    std::vector<Event> events;
    {
        ClientCore core{transport, [&](Event event) { events.push_back(std::move(event)); }};
        check(transport.observerAttached(),
              "the client core did not attach its transport observer");

        Membership membership{};
        membership.version = 42;
        membership.hierarchy_id = "hierarchy-main";
        membership.player_id = "player-7";
        membership.group_id = "group-alpha";
        membership.specialization_id = "red";
        membership.team_id = "red-2";
        membership.role_ids = {"player", "team-leader"};
        membership.connected = true;
        membership.can_receive_voice = true;
        check(core.updateMembership(membership) == Result::success,
              "a valid membership was rejected");
        check(events.back().kind == EventKind::membership_updated,
              "membership update event was not emitted");
        check(events.back().membership.has_value() &&
                  events.back().membership->role_ids == membership.role_ids,
              "membership event did not preserve its owning C++ values");

        check(core.updateMembership(membership) == Result::invalid_argument,
              "a non-increasing membership version was accepted");
        check(events.back().kind == EventKind::error &&
                  events.back().error_code == "client_invalid_argument",
              "membership validation did not emit a stable error event");

        const std::vector grants{
            RoomGrant{Scope::team, "wss://voice.example/team", "team-token"},
            RoomGrant{Scope::group, "wss://voice.example/group", "group-token"}};
        check(core.connect(grants) == Result::success, "valid room grants were rejected");
        check(transport.grants().size() == 2, "room grants did not cross the C ABI");
        check(core.connectionState() == ConnectionState::connected,
              "connection state was not normalized");

        check(core.pressPushToTalk(Scope::group) == Result::success,
              "push-to-talk could not start");
        transport.reconnect();
        check(core.releasePushToTalk() == Result::invalid_state,
              "reconnect retained an active transmission");

        transport.startSpeaker(Scope::group, "speaker-9");
        check(transport.admitted() && transport.configuredParticipant() == "speaker-9",
              "remote audio was not admitted through the client policy");
        check(events.back().kind == EventKind::speaker_started &&
                  events.back().participant_id == "speaker-9",
              "speaker start event was not normalized");
        transport.stopSpeaker(Scope::group, "speaker-9");
        check(events.back().kind == EventKind::speaker_stopped,
              "speaker stop event was not normalized");

        transport.failAsynchronously();
        check(events.back().kind == EventKind::error &&
                  events.back().message == "simulated transport failure",
              "asynchronous transport error was not normalized");

        check(core.clearMembership() == Result::success, "membership could not be cleared");
        check(events.back().kind == EventKind::membership_cleared,
              "membership clear event was not emitted");
        check(core.disconnect() == Result::success, "disconnect failed");

        for (std::size_t index = 1; index < events.size(); ++index)
        {
            check(events[index - 1].sequence < events[index].sequence,
                  "event sequence is not strictly increasing");
        }
    }
    check(!transport.observerAttached(), "client-core destruction did not detach the observer");
}
} // namespace

auto main() noexcept -> int
{
    try
    {
        runVersionAndConfigurationChecks();
        runCppWrapperScenario();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "client-core test failure: %s\n", error.what());
        return 1;
    }
}
