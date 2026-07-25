#include <array>
#include <cstdio>
#include <exception>
#include <hvc/client/voice_client.hpp>
#include <optional>
#include <span>
#include <string>
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

    IVoiceTransportObserver* observer_{nullptr};
    VoiceTransportState state_{VoiceTransportState::disconnected};
    std::optional<VoiceScope> active_scope_;
    std::size_t connected_grant_count{0};
    int connect_calls{0};
    int disconnect_calls{0};
    int start_calls{0};
    int stop_calls{0};
};

[[nodiscard]] auto validGrants() -> std::array<VoiceRoomGrant, 3>
{
    return {VoiceRoomGrant{VoiceScope::team, "ws://voice", "team-token"},
            VoiceRoomGrant{VoiceScope::specialization, "ws://voice", "specialization-token"},
            VoiceRoomGrant{VoiceScope::group, "ws://voice", "group-token"}};
}

auto testRequiresOneGrantPerScope() -> bool
{
    FakeVoiceTransport transport;
    VoiceClient client{transport};
    const std::array incomplete{VoiceRoomGrant{VoiceScope::team, "ws://voice", "team-token"}};
    if (client.connect(incomplete) || transport.connect_calls != 0)
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
    return !client.connect(missing_token) && transport.connect_calls == 0;
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
} // namespace

auto main() noexcept -> int
{
    try
    {
        if (!testRequiresOneGrantPerScope() || !testConnectAndPttLifecycle() ||
            !testReconnectStopsAndNeverResumesPtt())
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
