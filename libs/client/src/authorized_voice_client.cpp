#include <hvc/client/authorized_voice_client.hpp>
#include <utility>

namespace hvc::client
{
auto VoiceSessionResult::success(std::optional<StartedTransmission> started_transmission)
    -> VoiceSessionResult
{
    return {true, std::move(started_transmission), std::nullopt};
}

auto VoiceSessionResult::failure(VoiceSessionError action_error) -> VoiceSessionResult
{
    return {false, std::nullopt, std::move(action_error)};
}

VoiceSessionResult::operator bool() const noexcept
{
    return successful;
}

AuthorizedVoiceClient::AuthorizedVoiceClient(ControlPlaneClient& control_plane,
                                             VoiceClient& voice_client)
    : control_plane_(control_plane), voice_client_(voice_client)
{
}

auto AuthorizedVoiceClient::connect(std::string_view external_credential) -> VoiceSessionResult
{
    const std::scoped_lock lock{mutex_};
    if (membership_.has_value() || voice_client_.state() != VoiceTransportState::disconnected)
    {
        return VoiceSessionResult::failure({VoiceSessionErrorSource::client_state,
                                            "already_connected",
                                            "the authorized voice client is already active", 0});
    }

    auto session = control_plane_.createSession(external_credential);
    if (!session)
    {
        return controlPlaneFailure(*session.error);
    }
    auto current_membership = control_plane_.membership();
    if (!current_membership)
    {
        return controlPlaneFailure(*current_membership.error);
    }
    auto grants = control_plane_.voiceGrants();
    if (!grants)
    {
        return controlPlaneFailure(*grants.error);
    }
    if (grants.value->membership_version != current_membership.value->version)
    {
        return VoiceSessionResult::failure(
            {VoiceSessionErrorSource::control_plane, "voice_membership_stale",
             "voice grants do not match the current membership version", 0});
    }

    auto connected = voice_client_.connect(grants.value->room_grants);
    if (!connected)
    {
        return transportFailure(connected);
    }
    membership_ = std::move(*current_membership.value);
    return VoiceSessionResult::success();
}

auto AuthorizedVoiceClient::disconnect() -> VoiceSessionResult
{
    const std::scoped_lock lock{mutex_};
    if (active_transmission_.has_value())
    {
        auto ended = releasePushToTalk();
        if (!ended)
        {
            return ended;
        }
    }

    auto disconnected = voice_client_.disconnect();
    if (!disconnected)
    {
        return transportFailure(disconnected);
    }
    membership_.reset();
    control_plane_.clearSession();
    return VoiceSessionResult::success();
}

auto AuthorizedVoiceClient::refreshAuthorization() -> VoiceSessionResult
{
    const std::scoped_lock lock{mutex_};
    if (!membership_.has_value() || voice_client_.state() == VoiceTransportState::disconnected)
    {
        return VoiceSessionResult::failure({VoiceSessionErrorSource::client_state, "not_connected",
                                            "membership refresh requires a connected voice session",
                                            0});
    }
    auto current_membership = control_plane_.membership();
    if (!current_membership)
    {
        return controlPlaneFailure(*current_membership.error);
    }
    if (current_membership.value->version == membership_->version)
    {
        return VoiceSessionResult::success();
    }
    if (current_membership.value->version < membership_->version)
    {
        return VoiceSessionResult::failure(
            {VoiceSessionErrorSource::control_plane, "voice_membership_stale",
             "the server returned an obsolete membership version", 0});
    }
    auto grants = control_plane_.voiceGrants();
    if (!grants)
    {
        return controlPlaneFailure(*grants.error);
    }
    if (grants.value->membership_version != current_membership.value->version)
    {
        return VoiceSessionResult::failure(
            {VoiceSessionErrorSource::control_plane, "voice_membership_stale",
             "voice grants do not match the refreshed membership version", 0});
    }

    if (voice_client_.activeTransmissionScope().has_value())
    {
        static_cast<void>(voice_client_.releasePushToTalk());
    }
    if (active_transmission_.has_value())
    {
        static_cast<void>(control_plane_.endTransmission(active_transmission_->transmission_id));
        active_transmission_.reset();
    }
    auto disconnected = voice_client_.disconnect();
    if (!disconnected)
    {
        return transportFailure(disconnected);
    }
    auto connected = voice_client_.connect(grants.value->room_grants);
    if (!connected)
    {
        membership_.reset();
        return transportFailure(connected);
    }
    membership_ = std::move(*current_membership.value);
    return VoiceSessionResult::success();
}

auto AuthorizedVoiceClient::pressPushToTalk(domain::VoiceScope scope) -> VoiceSessionResult
{
    const std::scoped_lock lock{mutex_};
    if (!membership_.has_value() || voice_client_.state() != VoiceTransportState::connected)
    {
        return VoiceSessionResult::failure(
            {VoiceSessionErrorSource::client_state, "not_connected",
             "membership and voice transport must be ready before push-to-talk", 0});
    }
    if (active_transmission_.has_value())
    {
        return VoiceSessionResult::failure({VoiceSessionErrorSource::client_state,
                                            "transmission_already_active",
                                            "only one authorized transmission may be active", 0});
    }

    auto started = control_plane_.startTransmission(scope, membership_->version);
    if (!started)
    {
        return controlPlaneFailure(*started.error);
    }
    active_transmission_ = *started.value;
    auto microphone = voice_client_.pressPushToTalk(scope);
    if (!microphone)
    {
        const auto rollback = control_plane_.endTransmission(active_transmission_->transmission_id);
        auto failure = transportFailure(microphone);
        if (!rollback)
        {
            failure.error->message += "; server rollback also failed: " + rollback.error->code;
        }
        else
        {
            active_transmission_.reset();
        }
        return failure;
    }

    return VoiceSessionResult::success(active_transmission_);
}

auto AuthorizedVoiceClient::releasePushToTalk() -> VoiceSessionResult
{
    const std::scoped_lock lock{mutex_};
    if (!active_transmission_.has_value())
    {
        return VoiceSessionResult::failure({VoiceSessionErrorSource::client_state,
                                            "no_active_transmission",
                                            "no authorized transmission is active", 0});
    }

    std::optional<VoiceTransportResult> microphone_error;
    if (voice_client_.activeTransmissionScope().has_value())
    {
        auto stopped = voice_client_.releasePushToTalk();
        if (!stopped)
        {
            microphone_error = std::move(stopped);
        }
    }

    auto ended = control_plane_.endTransmission(active_transmission_->transmission_id);
    if (!ended)
    {
        return controlPlaneFailure(*ended.error);
    }
    active_transmission_.reset();
    if (microphone_error.has_value())
    {
        return transportFailure(*microphone_error);
    }
    return VoiceSessionResult::success();
}

auto AuthorizedVoiceClient::endInterruptedTransmission() -> VoiceSessionResult
{
    const std::scoped_lock lock{mutex_};
    if (!active_transmission_.has_value())
    {
        return VoiceSessionResult::success();
    }
    auto ended = control_plane_.endTransmission(active_transmission_->transmission_id);
    if (!ended)
    {
        return controlPlaneFailure(*ended.error);
    }
    active_transmission_.reset();
    return VoiceSessionResult::success();
}

auto AuthorizedVoiceClient::membership() const -> std::optional<MembershipView>
{
    const std::scoped_lock lock{mutex_};
    return membership_;
}

auto AuthorizedVoiceClient::activeTransmission() const -> std::optional<StartedTransmission>
{
    const std::scoped_lock lock{mutex_};
    return active_transmission_;
}

auto AuthorizedVoiceClient::controlPlaneFailure(const ControlPlaneError& error)
    -> VoiceSessionResult
{
    return VoiceSessionResult::failure(
        {VoiceSessionErrorSource::control_plane, error.code, error.message, error.status_code});
}

auto AuthorizedVoiceClient::transportFailure(const VoiceTransportResult& result)
    -> VoiceSessionResult
{
    return VoiceSessionResult::failure(
        {VoiceSessionErrorSource::voice_transport,
         "voice_transport_" + std::to_string(static_cast<unsigned int>(result.error)),
         result.message, 0});
}
} // namespace hvc::client
