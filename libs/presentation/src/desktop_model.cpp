#include <algorithm>
#include <cctype>
#include <cmath>
#include <hvc/presentation/desktop_model.hpp>
#include <ranges>
#include <utility>

namespace hvc::presentation
{
namespace
{
[[nodiscard]] auto success() -> ValidationResult
{
    return {};
}

[[nodiscard]] auto failure(ErrorCode error, std::string field) -> ValidationResult
{
    return {error, std::move(field)};
}

[[nodiscard]] auto isSessionAvailable(ConnectionPhase phase) noexcept -> bool
{
    return phase == ConnectionPhase::ready || phase == ConnectionPhase::reconnecting;
}

[[nodiscard]] auto hasRole(const client::MembershipView& membership, std::string_view expected_role)
    -> bool
{
    return std::ranges::any_of(membership.role_ids, [expected_role](const auto& role_id) {
        auto role = std::string{role_id.value()};
        std::ranges::transform(role, role.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return role == expected_role;
    });
}
} // namespace

ValidationResult::operator bool() const noexcept
{
    return error == ErrorCode::none;
}

auto DesktopModel::state() const noexcept -> const DesktopState&
{
    return state_;
}

auto DesktopModel::validate(const Command& command) const -> ValidationResult
{
    switch (command.kind)
    {
    case CommandKind::connect:
        return state_.connection == ConnectionPhase::signed_out
                   ? success()
                   : failure(ErrorCode::invalid_state, "connection");
    case CommandKind::disconnect:
        return isSessionAvailable(state_.connection)
                   ? success()
                   : failure(ErrorCode::invalid_state, "connection");
    case CommandKind::select_channel:
        if (!isSessionAvailable(state_.connection))
        {
            return failure(ErrorCode::invalid_state, "connection");
        }
        if (!command.channel.has_value() || command.channel->node_id.empty())
        {
            return failure(ErrorCode::invalid_argument, "channel");
        }
        return success();
    case CommandKind::open_settings:
    case CommandKind::open_diagnostics:
        return isSessionAvailable(state_.connection)
                   ? success()
                   : failure(ErrorCode::invalid_state, "connection");
    case CommandKind::open_administration:
        if (!isSessionAvailable(state_.connection))
        {
            return failure(ErrorCode::invalid_state, "connection");
        }
        return state_.administration.can_moderate || state_.administration.can_administrate
                   ? success()
                   : failure(ErrorCode::forbidden, "role");
    case CommandKind::apply_settings:
        if (!isSessionAvailable(state_.connection))
        {
            return failure(ErrorCode::invalid_state, "connection");
        }
        return command.settings.has_value() ? validateSettings(*command.settings)
                                            : failure(ErrorCode::invalid_argument, "settings");
    case CommandKind::set_participant_volume:
        if (!isSessionAvailable(state_.connection))
        {
            return failure(ErrorCode::invalid_state, "connection");
        }
        if (command.participant_id.empty() || !state_.participants.contains(command.participant_id))
        {
            return failure(ErrorCode::not_found, "participant_id");
        }
        return std::isfinite(command.participant_volume) && command.participant_volume >= 0.0F &&
                       command.participant_volume <= 1.0F
                   ? success()
                   : failure(ErrorCode::invalid_argument, "participant_volume");
    case CommandKind::set_participant_muted:
    case CommandKind::set_participant_blocked:
        if (!isSessionAvailable(state_.connection))
        {
            return failure(ErrorCode::invalid_state, "connection");
        }
        return !command.participant_id.empty() &&
                       state_.participants.contains(command.participant_id)
                   ? success()
                   : failure(ErrorCode::not_found, "participant_id");
    }
    return failure(ErrorCode::invalid_argument, "kind");
}

auto DesktopModel::beginConnect() -> ValidationResult
{
    const auto result = validate(Command{CommandKind::connect});
    if (result)
    {
        clearAuthenticatedState();
        state_.connection = ConnectionPhase::connecting;
        state_.diagnostics.voice_state = client::VoiceTransportState::connecting;
    }
    return result;
}

auto DesktopModel::connectionSucceeded(client::MembershipView membership) -> ValidationResult
{
    if (state_.connection != ConnectionPhase::connecting)
    {
        return failure(ErrorCode::invalid_state, "connection");
    }
    if (membership.version == 0 || membership.player_id.value().empty() ||
        membership.group_id.value().empty() || membership.specialization_id.value().empty() ||
        membership.team_id.value().empty())
    {
        return failure(ErrorCode::invalid_argument, "membership");
    }
    applyMembership(std::move(membership));
    state_.connection = ConnectionPhase::ready;
    state_.diagnostics.voice_state = client::VoiceTransportState::connected;
    return success();
}

void DesktopModel::connectionFailed(std::string error_code, std::string diagnostic)
{
    clearAuthenticatedState();
    state_.connection = ConnectionPhase::signed_out;
    recordError(std::move(error_code), std::move(diagnostic));
}

auto DesktopModel::beginDisconnect() -> ValidationResult
{
    const auto result = validate(Command{CommandKind::disconnect});
    if (result)
    {
        state_.connection = ConnectionPhase::disconnecting;
        state_.active_transmission_scope.reset();
    }
    return result;
}

void DesktopModel::disconnected() noexcept
{
    clearAuthenticatedState();
    state_.connection = ConnectionPhase::signed_out;
    state_.diagnostics.voice_state = client::VoiceTransportState::disconnected;
}

void DesktopModel::updateVoiceState(client::VoiceTransportState state) noexcept
{
    state_.diagnostics.voice_state = state;
    switch (state)
    {
    case client::VoiceTransportState::disconnected:
        if (state_.connection != ConnectionPhase::signed_out &&
            state_.connection != ConnectionPhase::disconnecting)
        {
            state_.connection = ConnectionPhase::reconnecting;
        }
        state_.active_transmission_scope.reset();
        break;
    case client::VoiceTransportState::connecting:
        if (state_.connection != ConnectionPhase::signed_out)
        {
            state_.connection = ConnectionPhase::connecting;
        }
        break;
    case client::VoiceTransportState::connected:
        if (state_.membership.has_value())
        {
            state_.connection = ConnectionPhase::ready;
        }
        break;
    case client::VoiceTransportState::reconnecting:
        if (state_.connection != ConnectionPhase::signed_out)
        {
            state_.connection = ConnectionPhase::reconnecting;
        }
        state_.active_transmission_scope.reset();
        break;
    }
}

auto DesktopModel::updateMembership(client::MembershipView membership) -> ValidationResult
{
    if (!state_.membership.has_value())
    {
        return failure(ErrorCode::invalid_state, "membership");
    }
    if (membership.version <= state_.membership->version)
    {
        return failure(ErrorCode::stale_version, "membership.version");
    }
    applyMembership(std::move(membership));
    return success();
}

auto DesktopModel::selectChannel(ChannelSelection selection) -> ValidationResult
{
    Command command{CommandKind::select_channel};
    command.channel = selection;
    const auto result = validate(command);
    if (result)
    {
        state_.selected_channel = std::move(selection);
    }
    return result;
}

void DesktopModel::speakerStarted(domain::VoiceScope scope, std::string participant_id)
{
    if (participant_id.empty())
    {
        return;
    }
    auto& participant = state_.participants[participant_id];
    participant.participant_id = participant_id;
    if (participant.display_name.empty())
    {
        participant.display_name = participant_id;
    }
    participant.audio_available = true;
    participant.speaking = true;
    participant.speaking_scope = scope;
}

void DesktopModel::speakerStopped(std::string_view participant_id) noexcept
{
    const auto participant = state_.participants.find(participant_id);
    if (participant == state_.participants.end())
    {
        return;
    }
    participant->second.speaking = false;
    participant->second.speaking_scope.reset();
}

void DesktopModel::transmissionStarted(domain::VoiceScope scope) noexcept
{
    state_.active_transmission_scope = scope;
}

void DesktopModel::transmissionStopped() noexcept
{
    state_.active_transmission_scope.reset();
}

void DesktopModel::replaceSettings(SettingsState settings)
{
    state_.settings = std::move(settings);
}

auto DesktopModel::setParticipantVolume(std::string_view participant_id, float volume)
    -> ValidationResult
{
    Command command{CommandKind::set_participant_volume};
    command.participant_id = participant_id;
    command.participant_volume = volume;
    const auto result = validate(command);
    if (result)
    {
        state_.participants.at(command.participant_id).volume = volume;
    }
    return result;
}

auto DesktopModel::setParticipantMuted(std::string_view participant_id, bool muted)
    -> ValidationResult
{
    Command command{CommandKind::set_participant_muted};
    command.participant_id = participant_id;
    command.enabled = muted;
    const auto result = validate(command);
    if (result)
    {
        state_.participants.at(command.participant_id).muted = muted;
    }
    return result;
}

void DesktopModel::recordError(std::string error_code, std::string diagnostic)
{
    state_.diagnostics.last_error_code = std::move(error_code);
    state_.diagnostics.last_diagnostic = std::move(diagnostic);
    ++state_.diagnostics.error_count;
}

auto DesktopModel::validateSettings(const SettingsState& settings) -> ValidationResult
{
    const auto valid_gain = [](float gain) {
        return std::isfinite(gain) && gain >= 0.0F && gain <= 1.0F;
    };
    if (settings.audio.maximum_streams == 0 ||
        std::ranges::any_of(settings.audio.maximum_streams_per_scope,
                            [](std::size_t value) { return value == 0; }))
    {
        return failure(ErrorCode::invalid_argument, "settings.audio.stream_limits");
    }
    if (!valid_gain(settings.audio.team_gain_under_specialization) ||
        !valid_gain(settings.audio.team_gain_under_group) ||
        !valid_gain(settings.audio.specialization_gain_under_group))
    {
        return failure(ErrorCode::invalid_argument, "settings.audio.ducking");
    }
    if (settings.text_scale_percent < 100 || settings.text_scale_percent > 200)
    {
        return failure(ErrorCode::invalid_argument, "settings.accessibility.text_scale_percent");
    }
    const auto device_exists = [](const auto& devices, const std::string& selected) {
        return selected.empty() || std::ranges::any_of(devices, [&selected](const auto& device) {
                   return device.id == selected;
               });
    };
    if (!device_exists(settings.recording_devices, settings.recording_device_id))
    {
        return failure(ErrorCode::not_found, "settings.recording_device_id");
    }
    if (!device_exists(settings.playout_devices, settings.playout_device_id))
    {
        return failure(ErrorCode::not_found, "settings.playout_device_id");
    }
    return success();
}

auto DesktopModel::administrationFor(const client::MembershipView& membership)
    -> AdministrationState
{
    const auto administrator = hasRole(membership, "administrator") || hasRole(membership, "admin");
    return {administrator || hasRole(membership, "moderator"), administrator, OperationPhase::idle};
}

void DesktopModel::applyMembership(client::MembershipView membership)
{
    state_.administration = administrationFor(membership);
    state_.selected_channel =
        ChannelSelection{domain::VoiceScope::team, std::string{membership.team_id.value()}};
    state_.membership = std::move(membership);
}

void DesktopModel::clearAuthenticatedState() noexcept
{
    state_.membership.reset();
    state_.selected_channel.reset();
    state_.participants.clear();
    state_.active_transmission_scope.reset();
    state_.administration = {};
    state_.diagnostics = {};
}

auto errorCodeName(ErrorCode error) noexcept -> std::string_view
{
    switch (error)
    {
    case ErrorCode::none:
        return "none";
    case ErrorCode::invalid_state:
        return "invalid_state";
    case ErrorCode::invalid_argument:
        return "invalid_argument";
    case ErrorCode::not_found:
        return "not_found";
    case ErrorCode::forbidden:
        return "forbidden";
    case ErrorCode::stale_version:
        return "stale_version";
    case ErrorCode::operation_failed:
        return "operation_failed";
    }
    return "invalid_argument";
}
} // namespace hvc::presentation
