#include <hvc/client/authorized_voice_client.hpp>
#include <utility>

namespace hvc::client
{
namespace
{
[[nodiscard]] auto scopeName(domain::VoiceScope scope) -> std::string_view
{
    switch (scope)
    {
    case domain::VoiceScope::team:
        return "team";
    case domain::VoiceScope::specialization:
        return "specialization";
    case domain::VoiceScope::group:
        return "group";
    }
    return "unknown";
}

[[nodiscard]] auto cancelledPublication(domain::VoiceScope scope, std::uint64_t generation,
                                        std::string_view correlation_id) -> VoiceSessionResult
{
    return VoiceSessionResult::failure(
        {VoiceSessionErrorSource::client_state, "publication_cancelled",
         "scope=" + std::string{scopeName(scope)} + "; transition=starting->stopping->idle" +
             "; generation=" + std::to_string(generation) +
             "; correlation_id=" + std::string{correlation_id},
         0});
}
} // namespace

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
    push_to_talk_enabled_ = true;
    return VoiceSessionResult::success();
}

auto AuthorizedVoiceClient::disconnect() -> VoiceSessionResult
{
    auto publication_active = false;
    {
        const std::scoped_lock lock{mutex_};
        push_to_talk_enabled_ = false;
        publication_active = publication_state_ != MicrophonePublicationState::idle;
    }
    if (publication_active)
    {
        const auto ended = releasePushToTalk();
        if (!ended)
        {
            return ended;
        }
    }
    auto interrupted = endInterruptedTransmission();
    if (!interrupted)
    {
        return interrupted;
    }

    auto disconnected = voice_client_.disconnect();
    if (!disconnected)
    {
        return transportFailure(disconnected);
    }
    {
        const std::scoped_lock lock{mutex_};
        membership_.reset();
    }
    control_plane_.clearSession();
    return VoiceSessionResult::success();
}

auto AuthorizedVoiceClient::refreshAuthorization() -> VoiceSessionResult
{
    std::uint64_t known_version{};
    {
        const std::scoped_lock lock{mutex_};
        if (!membership_.has_value() || voice_client_.state() == VoiceTransportState::disconnected)
        {
            return VoiceSessionResult::failure(
                {VoiceSessionErrorSource::client_state, "not_connected",
                 "membership refresh requires a connected voice session", 0});
        }
        known_version = membership_->version;
    }

    auto current_membership = control_plane_.membership();
    if (!current_membership)
    {
        return controlPlaneFailure(*current_membership.error);
    }
    if (current_membership.value->version == known_version)
    {
        return VoiceSessionResult::success();
    }
    if (current_membership.value->version < known_version)
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

    auto publication_active = false;
    {
        const std::scoped_lock lock{mutex_};
        push_to_talk_enabled_ = false;
        publication_active = publication_state_ != MicrophonePublicationState::idle;
    }
    if (publication_active)
    {
        const auto ended = releasePushToTalk();
        if (!ended)
        {
            return ended;
        }
    }
    auto interrupted = endInterruptedTransmission();
    if (!interrupted)
    {
        return interrupted;
    }
    auto disconnected = voice_client_.disconnect();
    if (!disconnected)
    {
        return transportFailure(disconnected);
    }
    auto connected = voice_client_.connect(grants.value->room_grants);
    if (!connected)
    {
        const std::scoped_lock lock{mutex_};
        membership_.reset();
        return transportFailure(connected);
    }
    {
        const std::scoped_lock lock{mutex_};
        membership_ = std::move(*current_membership.value);
        push_to_talk_enabled_ = true;
    }
    return VoiceSessionResult::success();
}

auto AuthorizedVoiceClient::pressPushToTalk(domain::VoiceScope scope) -> VoiceSessionResult
{
    std::uint64_t generation{};
    std::uint64_t membership_version{};
    {
        const std::scoped_lock lock{mutex_};
        if (!push_to_talk_enabled_ || !membership_.has_value() ||
            voice_client_.state() != VoiceTransportState::connected)
        {
            return VoiceSessionResult::failure(
                {VoiceSessionErrorSource::client_state, "not_connected",
                 "membership and voice transport must be ready before push-to-talk", 0});
        }
        if (publication_state_ != MicrophonePublicationState::idle ||
            active_transmission_.has_value())
        {
            return VoiceSessionResult::failure(
                {VoiceSessionErrorSource::client_state, "transmission_already_active",
                 "only one authorized transmission may be active", 0});
        }
        generation = ++publication_generation_;
        membership_version = membership_->version;
        publication_state_ = MicrophonePublicationState::starting;
        last_release_result_ = VoiceSessionResult::success();
    }

    auto started = control_plane_.startTransmission(scope, membership_version);
    if (!started)
    {
        const std::scoped_lock lock{mutex_};
        if (generation == publication_generation_)
        {
            publication_state_ = MicrophonePublicationState::idle;
            publication_changed_.notify_all();
        }
        return controlPlaneFailure(*started.error);
    }

    auto cancelled_before_voice = false;
    {
        const std::scoped_lock lock{mutex_};
        if (generation != publication_generation_)
        {
            cancelled_before_voice = true;
        }
        else
        {
            active_transmission_ = *started.value;
            cancelled_before_voice = publication_state_ == MicrophonePublicationState::stopping;
            voice_start_in_progress_ = !cancelled_before_voice;
        }
    }

    if (cancelled_before_voice)
    {
        const auto ended = control_plane_.endTransmission(started.value->transmission_id);
        const auto cancellation =
            cancelledPublication(scope, generation, started.value->client_transmission_id.value());
        {
            const std::scoped_lock lock{mutex_};
            if (generation == publication_generation_)
            {
                if (ended)
                {
                    active_transmission_.reset();
                    last_release_result_ = VoiceSessionResult::success();
                }
                else
                {
                    last_release_result_ = controlPlaneFailure(*ended.error);
                }
                publication_state_ = MicrophonePublicationState::idle;
                publication_changed_.notify_all();
            }
        }
        return cancellation;
    }

    auto microphone = voice_client_.pressPushToTalk(scope);
    auto cancelled_while_starting = false;
    {
        const std::scoped_lock lock{mutex_};
        voice_start_in_progress_ = false;
        cancelled_while_starting = generation == publication_generation_ &&
                                   publication_state_ == MicrophonePublicationState::stopping;
    }
    if (!microphone)
    {
        const auto rollback = control_plane_.endTransmission(started.value->transmission_id);
        auto failure = transportFailure(microphone);
        failure.error->message +=
            "; scope=" + std::string{scopeName(scope)} + "; transition=starting->idle" +
            "; generation=" + std::to_string(generation) +
            "; correlation_id=" + std::string{started.value->client_transmission_id.value()};
        if (!rollback)
        {
            failure.error->message += "; server rollback also failed: " + rollback.error->code;
        }
        {
            const std::scoped_lock lock{mutex_};
            if (generation == publication_generation_)
            {
                if (rollback)
                {
                    active_transmission_.reset();
                    last_release_result_ = VoiceSessionResult::success();
                }
                else
                {
                    last_release_result_ = controlPlaneFailure(*rollback.error);
                }
                publication_state_ = MicrophonePublicationState::idle;
                publication_changed_.notify_all();
            }
        }
        if (cancelled_while_starting && rollback)
        {
            return cancelledPublication(scope, generation,
                                        started.value->client_transmission_id.value());
        }
        return failure;
    }

    {
        const std::scoped_lock lock{mutex_};
        if (generation == publication_generation_ &&
            publication_state_ == MicrophonePublicationState::starting)
        {
            publication_state_ = MicrophonePublicationState::active;
            publication_changed_.notify_all();
            return VoiceSessionResult::success(active_transmission_);
        }
    }

    static_cast<void>(voice_client_.releasePushToTalk());
    const auto rollback = control_plane_.endTransmission(started.value->transmission_id);
    {
        const std::scoped_lock lock{mutex_};
        if (generation == publication_generation_)
        {
            if (rollback)
            {
                active_transmission_.reset();
                last_release_result_ = VoiceSessionResult::success();
            }
            else
            {
                last_release_result_ = controlPlaneFailure(*rollback.error);
            }
            publication_state_ = MicrophonePublicationState::idle;
            publication_changed_.notify_all();
        }
    }
    return cancelledPublication(scope, generation, started.value->client_transmission_id.value());
}

auto AuthorizedVoiceClient::releasePushToTalk() -> VoiceSessionResult
{
    std::uint64_t generation{};
    std::optional<StartedTransmission> transmission;
    auto cancel_start = false;
    {
        std::unique_lock lock{mutex_};
        if (publication_state_ == MicrophonePublicationState::idle)
        {
            return VoiceSessionResult::failure({VoiceSessionErrorSource::client_state,
                                                "no_active_transmission",
                                                "no authorized transmission is active", 0});
        }
        generation = publication_generation_;
        if (publication_state_ == MicrophonePublicationState::stopping)
        {
            publication_changed_.wait(lock, [this, generation] {
                return publication_generation_ != generation ||
                       publication_state_ == MicrophonePublicationState::idle;
            });
            return last_release_result_;
        }
        if (publication_state_ == MicrophonePublicationState::starting)
        {
            publication_state_ = MicrophonePublicationState::stopping;
            cancel_start = voice_start_in_progress_;
        }
        else
        {
            publication_state_ = MicrophonePublicationState::stopping;
            transmission = active_transmission_;
        }
    }

    if (cancel_start)
    {
        const auto stopped = voice_client_.releasePushToTalk();
        std::unique_lock lock{mutex_};
        publication_changed_.wait(lock, [this, generation] {
            return publication_generation_ != generation ||
                   publication_state_ == MicrophonePublicationState::idle;
        });
        if (!stopped)
        {
            return transportFailure(stopped);
        }
        return last_release_result_;
    }
    if (!transmission.has_value())
    {
        std::unique_lock lock{mutex_};
        publication_changed_.wait(lock, [this, generation] {
            return publication_generation_ != generation ||
                   publication_state_ == MicrophonePublicationState::idle;
        });
        return last_release_result_;
    }

    const auto stopped = voice_client_.releasePushToTalk();
    auto ended = control_plane_.endTransmission(transmission->transmission_id);
    VoiceSessionResult result = VoiceSessionResult::success();
    if (!ended)
    {
        result = controlPlaneFailure(*ended.error);
    }
    else if (!stopped)
    {
        result = transportFailure(stopped);
    }
    {
        const std::scoped_lock lock{mutex_};
        if (generation == publication_generation_)
        {
            if (ended)
            {
                active_transmission_.reset();
            }
            publication_state_ = MicrophonePublicationState::idle;
            last_release_result_ = result;
            publication_changed_.notify_all();
        }
    }
    return result;
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
