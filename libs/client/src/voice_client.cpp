#include <array>
#include <hvc/client/voice_client.hpp>
#include <utility>

namespace hvc::client
{
auto VoiceTransportResult::success() -> VoiceTransportResult
{
    return {};
}

auto VoiceTransportResult::failure(VoiceTransportError error, std::string message)
    -> VoiceTransportResult
{
    return VoiceTransportResult{error, std::move(message)};
}

VoiceTransportResult::operator bool() const noexcept
{
    return error == VoiceTransportError::none;
}

VoiceClient::VoiceClient(IVoiceTransport& transport)
    : transport_(transport), state_(transport.state())
{
    transport_.setObserver(this);
}

VoiceClient::~VoiceClient()
{
    transport_.setObserver(nullptr);
}

void VoiceClient::setObserver(IVoiceClientObserver* observer) noexcept
{
    const std::scoped_lock lock{mutex_};
    observer_ = observer;
}

auto VoiceClient::state() const noexcept -> VoiceTransportState
{
    const std::scoped_lock lock{mutex_};
    return state_;
}

auto VoiceClient::connect(std::span<const VoiceRoomGrant> grants) -> VoiceTransportResult
{
    auto validation = validateGrants(grants);
    if (!validation)
    {
        return validation;
    }

    {
        const std::scoped_lock lock{mutex_};
        if (state_ != VoiceTransportState::disconnected)
        {
            return VoiceTransportResult::failure(VoiceTransportError::invalid_state,
                                                 "voice client is already active");
        }
        state_ = VoiceTransportState::connecting;
    }

    auto result = transport_.connect(grants);
    if (!result)
    {
        const std::scoped_lock lock{mutex_};
        state_ = VoiceTransportState::disconnected;
    }
    return result;
}

auto VoiceClient::disconnect() -> VoiceTransportResult
{
    {
        const std::scoped_lock lock{mutex_};
        if (state_ == VoiceTransportState::disconnected)
        {
            active_scope_.reset();
            return VoiceTransportResult::success();
        }
    }

    auto stop_result = releasePushToTalk();
    if (!stop_result && stop_result.error != VoiceTransportError::invalid_state)
    {
        return stop_result;
    }

    auto result = transport_.disconnect();
    if (result)
    {
        const std::scoped_lock lock{mutex_};
        state_ = VoiceTransportState::disconnected;
        active_scope_.reset();
    }
    return result;
}

auto VoiceClient::pressPushToTalk(domain::VoiceScope scope) -> VoiceTransportResult
{
    {
        const std::scoped_lock lock{mutex_};
        if (state_ != VoiceTransportState::connected)
        {
            return VoiceTransportResult::failure(VoiceTransportError::invalid_state,
                                                 "voice transport is not connected");
        }
        if (active_scope_.has_value())
        {
            return VoiceTransportResult::failure(
                VoiceTransportError::invalid_state,
                "only one push-to-talk transmission may be active");
        }
    }

    auto result = transport_.startMicrophone(scope);
    if (!result)
    {
        return result;
    }

    {
        const std::scoped_lock lock{mutex_};
        if (state_ == VoiceTransportState::connected)
        {
            active_scope_ = scope;
            return result;
        }
    }

    static_cast<void>(transport_.stopMicrophone());
    return VoiceTransportResult::failure(VoiceTransportError::invalid_state,
                                         "connection changed while push-to-talk was starting");
}

auto VoiceClient::releasePushToTalk() -> VoiceTransportResult
{
    {
        const std::scoped_lock lock{mutex_};
        if (!active_scope_.has_value() && !transport_.activeTransmissionScope().has_value())
        {
            return VoiceTransportResult::failure(VoiceTransportError::invalid_state,
                                                 "no push-to-talk transmission is active");
        }
    }

    auto result = transport_.stopMicrophone();
    if (result)
    {
        const std::scoped_lock lock{mutex_};
        active_scope_.reset();
    }
    return result;
}

auto VoiceClient::activeTransmissionScope() const noexcept -> std::optional<domain::VoiceScope>
{
    const std::scoped_lock lock{mutex_};
    return active_scope_;
}

void VoiceClient::onTransportStateChanged(VoiceTransportState state)
{
    IVoiceClientObserver* current_observer = nullptr;
    {
        const std::scoped_lock lock{mutex_};
        state_ = state;
        if (state != VoiceTransportState::connected && active_scope_.has_value())
        {
            active_scope_.reset();
        }
        current_observer = observer_;
    }

    if (current_observer != nullptr)
    {
        current_observer->onVoiceStateChanged(state);
    }
}

void VoiceClient::onRemoteParticipantConnected(domain::VoiceScope, const std::string&)
{
}

void VoiceClient::onRemoteParticipantDisconnected(domain::VoiceScope scope,
                                                  const std::string& participant_id)
{
    auto* const current_observer = observer();
    if (current_observer != nullptr)
    {
        current_observer->onSpeakerStopped(scope, participant_id);
    }
}

void VoiceClient::onRemoteAudioStarted(domain::VoiceScope scope, const std::string& participant_id)
{
    auto* const current_observer = observer();
    if (current_observer != nullptr)
    {
        current_observer->onSpeakerStarted(scope, participant_id);
    }
}

void VoiceClient::onRemoteAudioStopped(domain::VoiceScope scope, const std::string& participant_id)
{
    auto* const current_observer = observer();
    if (current_observer != nullptr)
    {
        current_observer->onSpeakerStopped(scope, participant_id);
    }
}

void VoiceClient::onTransportError(VoiceTransportError error, const std::string& message)
{
    auto* const current_observer = observer();
    if (current_observer != nullptr)
    {
        current_observer->onVoiceError(error, message);
    }
}

auto VoiceClient::validateGrants(std::span<const VoiceRoomGrant> grants) -> VoiceTransportResult
{
    constexpr std::size_t required_scope_count = 3;
    if (grants.empty() || grants.size() > required_scope_count)
    {
        return VoiceTransportResult::failure(
            VoiceTransportError::invalid_argument,
            "between one and three authorized voice room grants are required");
    }

    std::array<bool, required_scope_count> seen_scopes{};
    for (const auto& grant : grants)
    {
        const auto index = static_cast<std::size_t>(grant.scope);
        if (index >= seen_scopes.size() || seen_scopes[index])
        {
            return VoiceTransportResult::failure(VoiceTransportError::invalid_argument,
                                                 "voice room grants contain a duplicate scope");
        }
        if (grant.url.empty() || grant.token.empty())
        {
            return VoiceTransportResult::failure(
                VoiceTransportError::invalid_argument,
                "voice room grants require a URL and a non-empty token");
        }
        seen_scopes[index] = true;
    }

    return VoiceTransportResult::success();
}

auto VoiceClient::observer() const noexcept -> IVoiceClientObserver*
{
    const std::scoped_lock lock{mutex_};
    return observer_;
}
} // namespace hvc::client
