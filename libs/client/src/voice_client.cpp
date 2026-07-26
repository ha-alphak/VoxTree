#include <algorithm>
#include <array>
#include <cmath>
#include <hvc/client/voice_client.hpp>
#include <tuple>
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

auto VoiceClient::setAudioEngineConfig(const AudioEngineConfig& config) -> VoiceTransportResult
{
    auto validation = validateAudioConfig(config);
    if (!validation)
    {
        return validation;
    }

    std::vector<PlaybackChange> changes;
    {
        const std::scoped_lock lock{mutex_};
        audio_config_ = config;
        changes = recomputePlaybackLocked();
    }
    return applyPlaybackChanges(std::move(changes));
}

auto VoiceClient::audioEngineConfig() const noexcept -> AudioEngineConfig
{
    const std::scoped_lock lock{mutex_};
    return audio_config_;
}

auto VoiceClient::setParticipantMuted(const std::string& participant_id, bool muted)
    -> VoiceTransportResult
{
    return updateParticipantFlag(participant_id, muted, muted_participants_);
}

auto VoiceClient::setParticipantBlocked(const std::string& participant_id, bool blocked)
    -> VoiceTransportResult
{
    return updateParticipantFlag(participant_id, blocked, blocked_participants_);
}

auto VoiceClient::setParticipantVolume(const std::string& participant_id, float volume)
    -> VoiceTransportResult
{
    if (participant_id.empty() || !std::isfinite(volume) || volume < 0.0F || volume > 1.0F)
    {
        return VoiceTransportResult::failure(
            VoiceTransportError::invalid_argument,
            "participant volume requires a non-empty ID and a finite value between zero and one");
    }

    std::vector<PlaybackChange> changes;
    {
        const std::scoped_lock lock{mutex_};
        if (volume == 1.0F)
        {
            participant_volumes_.erase(participant_id);
        }
        else
        {
            participant_volumes_.insert_or_assign(participant_id, volume);
        }
        changes = recomputePlaybackLocked();
    }
    return applyPlaybackChanges(std::move(changes));
}

auto VoiceClient::remoteAudioPlayback() const -> std::vector<RemoteAudioPlayback>
{
    const std::scoped_lock lock{mutex_};
    auto streams = available_remote_audio_;
    std::ranges::sort(streams, [](const auto& left, const auto& right) {
        return std::tuple{-static_cast<int>(left.scope), left.admission_order,
                          left.participant_id} < std::tuple{-static_cast<int>(right.scope),
                                                            right.admission_order,
                                                            right.participant_id};
    });

    std::vector<RemoteAudioPlayback> result;
    result.reserve(streams.size());
    for (const auto& stream : streams)
    {
        result.push_back(
            RemoteAudioPlayback{stream.scope, stream.participant_id, stream.admitted, stream.gain});
    }
    return result;
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
        if (state != VoiceTransportState::connected)
        {
            available_remote_audio_.clear();
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

void VoiceClient::onRemoteAudioAvailable(domain::VoiceScope scope,
                                         const std::string& participant_id)
{
    if (participant_id.empty())
    {
        onTransportError(VoiceTransportError::invalid_argument,
                         "remote audio publication has an empty participant ID");
        return;
    }

    std::vector<PlaybackChange> changes;
    {
        const std::scoped_lock lock{mutex_};
        const auto duplicate =
            std::ranges::find_if(available_remote_audio_, [&](const auto& stream) {
                return stream.scope == scope && stream.participant_id == participant_id;
            });
        if (duplicate != available_remote_audio_.end())
        {
            return;
        }
        available_remote_audio_.push_back(
            AvailableRemoteAudio{scope, participant_id, next_admission_order_++, false, 0.0F});
        changes = recomputePlaybackLocked();
    }
    static_cast<void>(applyPlaybackChanges(std::move(changes)));
}

void VoiceClient::onRemoteAudioUnavailable(domain::VoiceScope scope,
                                           const std::string& participant_id)
{
    std::vector<PlaybackChange> changes;
    {
        const std::scoped_lock lock{mutex_};
        std::erase_if(available_remote_audio_, [&](const auto& stream) {
            return stream.scope == scope && stream.participant_id == participant_id;
        });
        changes = recomputePlaybackLocked();
    }
    static_cast<void>(applyPlaybackChanges(std::move(changes)));
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

auto VoiceClient::validateAudioConfig(const AudioEngineConfig& config) -> VoiceTransportResult
{
    const auto valid_gain = [](float gain) {
        return std::isfinite(gain) && gain >= 0.0F && gain <= 1.0F;
    };
    if (config.maximum_streams == 0 ||
        std::ranges::any_of(config.maximum_streams_per_scope,
                            [](std::size_t value) { return value == 0; }) ||
        !valid_gain(config.team_gain_under_specialization) ||
        !valid_gain(config.team_gain_under_group) ||
        !valid_gain(config.specialization_gain_under_group))
    {
        return VoiceTransportResult::failure(
            VoiceTransportError::invalid_argument,
            "audio engine limits must be positive and ducking gains must be between zero and one");
    }
    return VoiceTransportResult::success();
}

auto VoiceClient::recomputePlaybackLocked() -> std::vector<PlaybackChange>
{
    std::vector<AvailableRemoteAudio*> ordered;
    ordered.reserve(available_remote_audio_.size());
    for (auto& stream : available_remote_audio_)
    {
        ordered.push_back(&stream);
    }
    std::ranges::sort(ordered, [](const auto* left, const auto* right) {
        return std::tuple{-static_cast<int>(left->scope), left->admission_order,
                          left->participant_id} < std::tuple{-static_cast<int>(right->scope),
                                                             right->admission_order,
                                                             right->participant_id};
    });

    std::array<std::size_t, 3> admitted_per_scope{};
    std::size_t admitted_total = 0;
    for (auto* stream : ordered)
    {
        const auto scope_index = static_cast<std::size_t>(stream->scope);
        const auto suppressed = muted_participants_.contains(stream->participant_id) ||
                                blocked_participants_.contains(stream->participant_id);
        const auto within_limits = scope_index < admitted_per_scope.size() &&
                                   admitted_per_scope[scope_index] <
                                       audio_config_.maximum_streams_per_scope[scope_index] &&
                                   admitted_total < audio_config_.maximum_streams;
        stream->admitted = !suppressed && within_limits;
        if (stream->admitted)
        {
            ++admitted_per_scope[scope_index];
            ++admitted_total;
        }
    }

    const auto group_active =
        admitted_per_scope[static_cast<std::size_t>(domain::VoiceScope::group)] != 0;
    const auto specialization_active =
        admitted_per_scope[static_cast<std::size_t>(domain::VoiceScope::specialization)] != 0;

    std::vector<PlaybackChange> changes;
    for (auto* stream : ordered)
    {
        auto gain = 0.0F;
        if (stream->admitted)
        {
            const auto volume = participant_volumes_.find(stream->participant_id);
            gain = volume == participant_volumes_.end() ? 1.0F : volume->second;
            if (stream->scope == domain::VoiceScope::team)
            {
                if (group_active)
                {
                    gain *= audio_config_.team_gain_under_group;
                }
                else if (specialization_active)
                {
                    gain *= audio_config_.team_gain_under_specialization;
                }
            }
            else if (stream->scope == domain::VoiceScope::specialization && group_active)
            {
                gain *= audio_config_.specialization_gain_under_group;
            }
        }

        if (stream->gain != gain)
        {
            stream->gain = gain;
        }
        changes.push_back(
            PlaybackChange{stream->scope, stream->participant_id, stream->admitted, stream->gain});
    }
    return changes;
}

auto VoiceClient::applyPlaybackChanges(std::vector<PlaybackChange> changes) -> VoiceTransportResult
{
    auto result = VoiceTransportResult::success();
    for (const auto& change : changes)
    {
        auto current = transport_.configureRemoteAudio(change.scope, change.participant_id,
                                                       change.admitted, change.gain);
        if (!current && result)
        {
            result = current;
        }
    }
    if (!result)
    {
        onTransportError(result.error, result.message);
    }
    return result;
}

auto VoiceClient::updateParticipantFlag(const std::string& participant_id, bool enabled,
                                        std::unordered_set<std::string>& values)
    -> VoiceTransportResult
{
    if (participant_id.empty())
    {
        return VoiceTransportResult::failure(VoiceTransportError::invalid_argument,
                                             "participant ID must not be empty");
    }

    std::vector<PlaybackChange> changes;
    {
        const std::scoped_lock lock{mutex_};
        if (enabled)
        {
            values.insert(participant_id);
        }
        else
        {
            values.erase(participant_id);
        }
        changes = recomputePlaybackLocked();
    }
    return applyPlaybackChanges(std::move(changes));
}

auto VoiceClient::observer() const noexcept -> IVoiceClientObserver*
{
    const std::scoped_lock lock{mutex_};
    return observer_;
}
} // namespace hvc::client
