#include <cstdio>
#include <exception>
#include <hvc/client_core.hpp>
#include <optional>
#include <span>
#include <string>

namespace
{
using namespace hvc::client_core;

class GameVoiceTransport final : public Transport
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

    [[nodiscard]] auto connect(std::span<const RoomGrant>) -> TransportResult override
    {
        // A production adapter starts its asynchronous SDK room connections here.
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
        active_scope_ = scope;
        return {};
    }

    [[nodiscard]] auto stopMicrophone() -> TransportResult override
    {
        active_scope_.reset();
        return {};
    }

    [[nodiscard]] auto activeMicrophoneScope() const noexcept -> std::optional<Scope> override
    {
        return active_scope_;
    }

    [[nodiscard]] auto configureRemoteAudio(Scope, const std::string&, bool, float)
        -> TransportResult override
    {
        return {};
    }

  private:
    TransportObserver* observer_{nullptr};
    ConnectionState state_{ConnectionState::disconnected};
    std::optional<Scope> active_scope_;
};

void printEvent(const Event& event)
{
    if (event.kind == EventKind::membership_updated && event.membership.has_value())
    {
        std::printf("membership %llu for %s\n",
                    static_cast<unsigned long long>(event.membership->version),
                    event.membership->player_id.c_str());
    }
    else if (event.kind == EventKind::connection_state_changed)
    {
        std::printf("connection state %u\n", static_cast<unsigned int>(event.connection_state));
    }
    else if (event.kind == EventKind::error)
    {
        std::fprintf(stderr, "%s: %s\n", event.error_code.c_str(), event.message.c_str());
    }
}
} // namespace

auto main() noexcept -> int
{
    try
    {
        GameVoiceTransport transport;
        ClientCore core{transport, printEvent};

        Membership membership{};
        membership.version = 1;
        membership.hierarchy_id = "main";
        membership.player_id = "player-42";
        membership.group_id = "alpha";
        membership.specialization_id = "red";
        membership.team_id = "red-1";
        membership.role_ids = {"player"};
        membership.connected = true;
        membership.can_receive_voice = true;
        if (core.updateMembership(membership) != Result::success)
        {
            return 1;
        }

        const RoomGrant team{Scope::team, "wss://voice.example.invalid", "short-lived-token"};
        if (core.connect(std::span{&team, 1}) != Result::success)
        {
            return 1;
        }
        if (core.pressPushToTalk(Scope::team) != Result::success ||
            core.releasePushToTalk() != Result::success)
        {
            return 1;
        }
        return core.disconnect() == Result::success ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "client-core example failed: %s\n", error.what());
        return 1;
    }
}
