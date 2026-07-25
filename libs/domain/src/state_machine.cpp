#include <hvc/domain/state_machine.hpp>
#include <utility>

namespace hvc::domain
{
auto ConnectionStateMachine::state() const noexcept -> ConnectionState
{
    return state_;
}

auto ConnectionStateMachine::ready() const noexcept -> bool
{
    return state_ == ConnectionState::ready;
}

auto ConnectionStateMachine::membershipVersion() const noexcept -> std::optional<std::uint64_t>
{
    return membership_version_;
}

auto ConnectionStateMachine::beginConnect() noexcept -> bool
{
    if (state_ != ConnectionState::disconnected)
    {
        return false;
    }

    state_ = ConnectionState::connecting_transport;
    return true;
}

auto ConnectionStateMachine::transportConnected() noexcept -> bool
{
    if (state_ != ConnectionState::connecting_transport &&
        state_ != ConnectionState::reconnecting_transport)
    {
        return false;
    }

    membership_version_.reset();
    state_ = ConnectionState::refreshing_membership;
    return true;
}

auto ConnectionStateMachine::membershipRefreshed(std::uint64_t version) noexcept -> bool
{
    if (state_ != ConnectionState::refreshing_membership || version == 0)
    {
        return false;
    }

    membership_version_ = version;
    state_ = ConnectionState::restoring_subscriptions;
    return true;
}

auto ConnectionStateMachine::subscriptionsRestored(std::uint64_t version) noexcept -> bool
{
    if (state_ != ConnectionState::restoring_subscriptions || !membership_version_.has_value() ||
        version != *membership_version_)
    {
        return false;
    }

    state_ = ConnectionState::ready;
    return true;
}

auto ConnectionStateMachine::membershipChanged(std::uint64_t version) noexcept -> bool
{
    if (state_ != ConnectionState::ready || !membership_version_.has_value() ||
        version <= *membership_version_)
    {
        return false;
    }

    membership_version_ = version;
    state_ = ConnectionState::restoring_subscriptions;
    return true;
}

auto ConnectionStateMachine::connectionLost() noexcept -> bool
{
    if (state_ == ConnectionState::disconnected ||
        state_ == ConnectionState::reconnecting_transport)
    {
        return false;
    }

    membership_version_.reset();
    state_ = ConnectionState::reconnecting_transport;
    return true;
}

auto ConnectionStateMachine::disconnect() noexcept -> bool
{
    if (state_ == ConnectionState::disconnected)
    {
        return false;
    }

    membership_version_.reset();
    state_ = ConnectionState::disconnected;
    return true;
}

auto TransmissionStateMachine::state() const noexcept -> TransmissionState
{
    return state_;
}

auto TransmissionStateMachine::clientRequestId() const noexcept -> const ClientTransmissionId*
{
    return context_.has_value() ? &context_->client_request_id : nullptr;
}

auto TransmissionStateMachine::transmissionId() const noexcept -> const TransmissionId*
{
    return context_.has_value() && context_->transmission_id.has_value()
               ? &*context_->transmission_id
               : nullptr;
}

auto TransmissionStateMachine::scope() const noexcept -> std::optional<VoiceScope>
{
    return context_.has_value() ? std::optional{context_->scope} : std::nullopt;
}

auto TransmissionStateMachine::membershipVersion() const noexcept -> std::optional<std::uint64_t>
{
    return context_.has_value() ? std::optional{context_->membership_version} : std::nullopt;
}

auto TransmissionStateMachine::lastStopReason() const noexcept -> TransmissionStopReason
{
    return last_stop_reason_;
}

auto TransmissionStateMachine::requestStart(ClientTransmissionId client_request_id,
                                            VoiceScope scope, std::uint64_t membership_version)
    -> bool
{
    if (state_ != TransmissionState::idle || membership_version == 0)
    {
        return false;
    }

    context_.emplace(std::move(client_request_id), scope, membership_version);
    last_stop_reason_ = TransmissionStopReason::none;
    state_ = TransmissionState::requesting;
    return true;
}

auto TransmissionStateMachine::accepted(const ClientTransmissionId& client_request_id,
                                        TransmissionId transmission_id) -> bool
{
    if (state_ != TransmissionState::requesting || !context_.has_value() ||
        context_->client_request_id != client_request_id)
    {
        return false;
    }

    context_->transmission_id = std::move(transmission_id);
    state_ = TransmissionState::transmitting;
    return true;
}

auto TransmissionStateMachine::rejected(const ClientTransmissionId& client_request_id) noexcept
    -> bool
{
    if (state_ != TransmissionState::requesting || !context_.has_value() ||
        context_->client_request_id != client_request_id)
    {
        return false;
    }

    finish(TransmissionStopReason::rejected);
    return true;
}

auto TransmissionStateMachine::requestEnd() noexcept -> bool
{
    if (state_ == TransmissionState::requesting)
    {
        finish(TransmissionStopReason::push_to_talk_released);
        return true;
    }
    if (state_ != TransmissionState::transmitting)
    {
        return false;
    }

    state_ = TransmissionState::ending;
    return true;
}

auto TransmissionStateMachine::ended(const TransmissionId& transmission_id) noexcept -> bool
{
    if (state_ != TransmissionState::ending || !context_.has_value() ||
        !context_->transmission_id.has_value() || *context_->transmission_id != transmission_id)
    {
        return false;
    }

    finish(TransmissionStopReason::push_to_talk_released);
    return true;
}

auto TransmissionStateMachine::interrupt(TransmissionStopReason reason) noexcept -> bool
{
    if (state_ == TransmissionState::idle || reason == TransmissionStopReason::none)
    {
        return false;
    }

    finish(reason);
    return true;
}

void TransmissionStateMachine::finish(TransmissionStopReason reason) noexcept
{
    context_.reset();
    last_stop_reason_ = reason;
    state_ = TransmissionState::idle;
}

auto VoiceSessionStateMachine::connectionState() const noexcept -> ConnectionState
{
    return connection_.state();
}

auto VoiceSessionStateMachine::transmissionState() const noexcept -> TransmissionState
{
    return transmission_.state();
}

auto VoiceSessionStateMachine::selectedScope() const noexcept -> VoiceScope
{
    return selected_scope_;
}

auto VoiceSessionStateMachine::membershipVersion() const noexcept -> std::optional<std::uint64_t>
{
    return connection_.membershipVersion();
}

auto VoiceSessionStateMachine::clientRequestId() const noexcept -> const ClientTransmissionId*
{
    return transmission_.clientRequestId();
}

auto VoiceSessionStateMachine::transmissionId() const noexcept -> const TransmissionId*
{
    return transmission_.transmissionId();
}

auto VoiceSessionStateMachine::lastTransmissionStopReason() const noexcept -> TransmissionStopReason
{
    return transmission_.lastStopReason();
}

void VoiceSessionStateMachine::selectScope(VoiceScope scope) noexcept
{
    selected_scope_ = scope;
}

auto VoiceSessionStateMachine::beginConnect() noexcept -> bool
{
    return connection_.beginConnect();
}

auto VoiceSessionStateMachine::transportConnected() noexcept -> bool
{
    return connection_.transportConnected();
}

auto VoiceSessionStateMachine::membershipRefreshed(std::uint64_t version) noexcept -> bool
{
    return connection_.membershipRefreshed(version);
}

auto VoiceSessionStateMachine::subscriptionsRestored(std::uint64_t version) noexcept -> bool
{
    return connection_.subscriptionsRestored(version);
}

auto VoiceSessionStateMachine::membershipChanged(std::uint64_t version) noexcept -> bool
{
    if (!connection_.membershipChanged(version))
    {
        return false;
    }

    static_cast<void>(transmission_.interrupt(TransmissionStopReason::membership_changed));
    return true;
}

auto VoiceSessionStateMachine::connectionLost() noexcept -> bool
{
    if (!connection_.connectionLost())
    {
        return false;
    }

    static_cast<void>(transmission_.interrupt(TransmissionStopReason::connection_lost));
    return true;
}

auto VoiceSessionStateMachine::disconnect() noexcept -> bool
{
    if (!connection_.disconnect())
    {
        return false;
    }

    static_cast<void>(transmission_.interrupt(TransmissionStopReason::disconnected));
    return true;
}

auto VoiceSessionStateMachine::requestTransmission(ClientTransmissionId client_request_id) -> bool
{
    const auto version = connection_.membershipVersion();
    if (!connection_.ready() || !version.has_value())
    {
        return false;
    }

    return transmission_.requestStart(std::move(client_request_id), selected_scope_, *version);
}

auto VoiceSessionStateMachine::transmissionAccepted(const ClientTransmissionId& client_request_id,
                                                    TransmissionId transmission_id) -> bool
{
    return transmission_.accepted(client_request_id, std::move(transmission_id));
}

auto VoiceSessionStateMachine::transmissionRejected(
    const ClientTransmissionId& client_request_id) noexcept -> bool
{
    return transmission_.rejected(client_request_id);
}

auto VoiceSessionStateMachine::endTransmission() noexcept -> bool
{
    return transmission_.requestEnd();
}

auto VoiceSessionStateMachine::transmissionEnded(const TransmissionId& transmission_id) noexcept
    -> bool
{
    return transmission_.ended(transmission_id);
}

auto VoiceSessionStateMachine::permissionRevoked() noexcept -> bool
{
    return transmission_.interrupt(TransmissionStopReason::permission_revoked);
}

auto VoiceSessionStateMachine::transmissionTimedOut() noexcept -> bool
{
    return transmission_.interrupt(TransmissionStopReason::timed_out);
}

auto VoiceSessionStateMachine::transmissionTransportFailed() noexcept -> bool
{
    return transmission_.interrupt(TransmissionStopReason::transport_error);
}
} // namespace hvc::domain
