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

[[nodiscard]] auto scopeFor(client::DirectoryNodeKind kind) noexcept -> domain::VoiceScope
{
    switch (kind)
    {
    case client::DirectoryNodeKind::group:
        return domain::VoiceScope::group;
    case client::DirectoryNodeKind::specialization:
        return domain::VoiceScope::specialization;
    case client::DirectoryNodeKind::team:
        return domain::VoiceScope::team;
    }
    return domain::VoiceScope::team;
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
        return isKnownChannel(*command.channel) ? success()
                                                : failure(ErrorCode::not_found, "channel");
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
        state_.directory_phase = DirectoryPhase::loading;
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

void DesktopModel::updateVoiceState(const client::VoiceConnectionEvent& event) noexcept
{
    if (event.generation < voice_generation_)
    {
        return;
    }
    if (event.generation > voice_generation_)
    {
        voice_generation_ = event.generation;
        clearRemoteTransportState();
        participant_voice_sequences_.clear();
        last_connection_sequence_ = 0;
    }
    if (event.sequence <= last_connection_sequence_)
    {
        return;
    }
    last_connection_sequence_ = event.sequence;
    state_.diagnostics.voice_state = event.state;
    switch (event.state)
    {
    case client::VoiceTransportState::disconnected:
        if (state_.connection != ConnectionPhase::signed_out &&
            state_.connection != ConnectionPhase::disconnecting)
        {
            state_.connection = ConnectionPhase::reconnecting;
        }
        if (state_.directory.has_value())
        {
            state_.directory_phase = DirectoryPhase::stale;
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
        if (state_.directory.has_value())
        {
            state_.directory_phase = DirectoryPhase::stale;
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
        refreshSelectedParticipants();
    }
    return result;
}

auto DesktopModel::applyDirectory(client::DirectoryView directory) -> ValidationResult
{
    if (!state_.membership.has_value())
    {
        return failure(ErrorCode::invalid_state, "directory");
    }
    if (directory.group_id != state_.membership->group_id.value())
    {
        return failure(ErrorCode::forbidden, "directory.group_id");
    }
    if (directory.version == 0 ||
        (state_.directory.has_value() && directory.version <= state_.directory->version))
    {
        return failure(ErrorCode::stale_version, "directory.version");
    }

    std::map<std::string, ParticipantState, std::less<>> participants;
    for (const auto& source : directory.participants)
    {
        ParticipantState target;
        if (const auto existing = state_.participants.find(source.player_id);
            existing != state_.participants.end())
        {
            target = existing->second;
        }
        target.participant_id = source.player_id;
        target.display_name = source.display_name;
        target.primary_team_id = source.primary_team_id;
        target.role_ids = source.public_role_ids;
        refreshParticipantDerivedState(target);
        participants.emplace(target.participant_id, std::move(target));
    }
    state_.participants = std::move(participants);
    state_.directory = std::move(directory);
    state_.directory_phase = DirectoryPhase::loading;
    state_.presence_version.reset();
    state_.presence_observed_at.reset();
    server_presence_.clear();
    rebuildChannelState();
    if (!state_.selected_channel.has_value() || !isKnownChannel(*state_.selected_channel))
    {
        state_.selected_channel = ChannelSelection{domain::VoiceScope::team,
                                                   std::string{state_.membership->team_id.value()}};
    }
    for (auto& [participant_id, participant] : state_.participants)
    {
        static_cast<void>(participant_id);
        refreshParticipantDerivedState(participant);
    }
    refreshSelectedParticipants();
    return success();
}

auto DesktopModel::applyPresence(client::DirectoryPresenceView presence) -> ValidationResult
{
    if (!state_.directory.has_value())
    {
        return failure(ErrorCode::invalid_state, "presence");
    }
    if (presence.version == 0 ||
        (state_.presence_version.has_value() && presence.version <= *state_.presence_version))
    {
        return failure(ErrorCode::stale_version, "presence.version");
    }
    if (presence.mode == client::DirectoryPresenceMode::delta &&
        !state_.presence_version.has_value())
    {
        return failure(ErrorCode::invalid_state, "presence.mode");
    }
    if (presence.mode == client::DirectoryPresenceMode::snapshot)
    {
        server_presence_.clear();
    }
    for (const auto& entry : presence.entries)
    {
        if (state_.participants.contains(entry.player_id))
        {
            server_presence_[entry.player_id] = entry.online;
        }
    }
    state_.presence_version = presence.version;
    state_.presence_observed_at = presence.observed_at;
    state_.directory_phase = DirectoryPhase::ready;
    for (auto& [participant_id, participant] : state_.participants)
    {
        static_cast<void>(participant_id);
        refreshParticipantDerivedState(participant);
    }
    return success();
}

void DesktopModel::directoryRefreshFailed(std::string_view error_code) noexcept
{
    if (error_code == "forbidden" || error_code == "directory_forbidden")
    {
        state_.directory.reset();
        state_.channel_nodes.clear();
        state_.selected_channel.reset();
        state_.participants.clear();
        state_.selected_participant_ids.clear();
        state_.presence_version.reset();
        state_.presence_observed_at.reset();
        server_presence_.clear();
        participant_voice_sequences_.clear();
        state_.directory_phase = DirectoryPhase::unauthorized;
        return;
    }
    if (error_code == "directory_unavailable")
    {
        state_.directory.reset();
        state_.channel_nodes.clear();
        state_.selected_channel.reset();
        state_.participants.clear();
        state_.selected_participant_ids.clear();
        state_.presence_version.reset();
        state_.presence_observed_at.reset();
        server_presence_.clear();
        participant_voice_sequences_.clear();
        state_.directory_phase = DirectoryPhase::unavailable;
        return;
    }
    if (!state_.directory.has_value())
    {
        state_.directory_phase = DirectoryPhase::unavailable;
        return;
    }
    state_.directory_phase = DirectoryPhase::stale;
}

void DesktopModel::applyVoiceRemoteEvent(const client::VoiceRemoteEvent& event)
{
    if (event.participant_id.empty() || event.generation < voice_generation_)
    {
        return;
    }
    if (event.generation > voice_generation_)
    {
        voice_generation_ = event.generation;
        clearRemoteTransportState();
        participant_voice_sequences_.clear();
        last_connection_sequence_ = 0;
    }

    const auto scope_index = static_cast<std::size_t>(event.scope);
    if (scope_index >= 3)
    {
        return;
    }
    auto& sequences = participant_voice_sequences_[event.participant_id];
    const auto advance = [&event](std::uint64_t& sequence) {
        if (event.sequence <= sequence)
        {
            return false;
        }
        sequence = event.sequence;
        return true;
    };
    auto update_connected = false;
    auto update_audio = false;
    auto update_speaking = false;
    switch (event.kind)
    {
    case client::VoiceRemoteEventKind::participant_connected:
        update_connected = advance(sequences.connected[scope_index]);
        break;
    case client::VoiceRemoteEventKind::participant_disconnected:
        update_connected = advance(sequences.connected[scope_index]);
        update_audio = advance(sequences.audio_available[scope_index]);
        update_speaking = advance(sequences.speaking[scope_index]);
        break;
    case client::VoiceRemoteEventKind::audio_available:
        update_audio = advance(sequences.audio_available[scope_index]);
        break;
    case client::VoiceRemoteEventKind::audio_unavailable:
    case client::VoiceRemoteEventKind::speaker_started:
        update_audio = advance(sequences.audio_available[scope_index]);
        update_speaking = advance(sequences.speaking[scope_index]);
        break;
    case client::VoiceRemoteEventKind::speaker_stopped:
        update_speaking = advance(sequences.speaking[scope_index]);
        break;
    }
    if (!update_connected && !update_audio && !update_speaking)
    {
        return;
    }

    auto participant = state_.participants.find(event.participant_id);
    if (participant == state_.participants.end())
    {
        if (state_.directory.has_value())
        {
            return;
        }
        ParticipantState placeholder;
        placeholder.participant_id = event.participant_id;
        placeholder.display_name = event.participant_id;
        participant =
            state_.participants.emplace(event.participant_id, std::move(placeholder)).first;
    }
    switch (event.kind)
    {
    case client::VoiceRemoteEventKind::participant_connected:
        if (update_connected)
        {
            participant->second.connected_scopes[scope_index] = true;
        }
        break;
    case client::VoiceRemoteEventKind::participant_disconnected:
        if (update_connected)
        {
            participant->second.connected_scopes[scope_index] = false;
        }
        if (update_audio)
        {
            participant->second.audio_available_scopes[scope_index] = false;
        }
        if (update_speaking && participant->second.speaking_scope == event.scope)
        {
            participant->second.speaking = false;
            participant->second.speaking_scope.reset();
        }
        break;
    case client::VoiceRemoteEventKind::audio_available:
        if (update_audio)
        {
            participant->second.audio_available_scopes[scope_index] = true;
        }
        break;
    case client::VoiceRemoteEventKind::audio_unavailable:
        if (update_audio)
        {
            participant->second.audio_available_scopes[scope_index] = false;
        }
        if (update_speaking && participant->second.speaking_scope == event.scope)
        {
            participant->second.speaking = false;
            participant->second.speaking_scope.reset();
        }
        break;
    case client::VoiceRemoteEventKind::speaker_started:
        if (update_audio)
        {
            participant->second.audio_available_scopes[scope_index] = true;
        }
        if (update_speaking)
        {
            participant->second.speaking = true;
            participant->second.speaking_scope = event.scope;
        }
        break;
    case client::VoiceRemoteEventKind::speaker_stopped:
        if (update_speaking && participant->second.speaking_scope == event.scope)
        {
            participant->second.speaking = false;
            participant->second.speaking_scope.reset();
        }
        break;
    }
    refreshParticipantDerivedState(participant->second);
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

auto DesktopModel::setParticipantBlocked(std::string_view participant_id, bool blocked)
    -> ValidationResult
{
    Command command{CommandKind::set_participant_blocked};
    command.participant_id = participant_id;
    command.enabled = blocked;
    const auto result = validate(command);
    if (result)
    {
        state_.participants.at(command.participant_id).blocked = blocked;
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

auto DesktopModel::isKnownChannel(const ChannelSelection& selection) const noexcept -> bool
{
    if (state_.directory.has_value())
    {
        return std::ranges::any_of(state_.channel_nodes, [&selection](const auto& node) {
            return node.channel == selection;
        });
    }
    if (!state_.membership.has_value())
    {
        return false;
    }
    switch (selection.scope)
    {
    case domain::VoiceScope::group:
        return selection.node_id == state_.membership->group_id.value();
    case domain::VoiceScope::specialization:
        return selection.node_id == state_.membership->specialization_id.value();
    case domain::VoiceScope::team:
        return selection.node_id == state_.membership->team_id.value();
    }
    return false;
}

auto DesktopModel::participantBelongsToChannel(const ParticipantState& participant,
                                               const ChannelSelection& selection) const noexcept
    -> bool
{
    if (!state_.directory.has_value())
    {
        return false;
    }
    if (selection.scope == domain::VoiceScope::group)
    {
        return selection.node_id == state_.directory->group_id;
    }
    if (selection.scope == domain::VoiceScope::team)
    {
        return participant.primary_team_id == selection.node_id;
    }
    const auto team =
        std::ranges::find_if(state_.directory->nodes, [&participant](const auto& node) {
            return node.kind == client::DirectoryNodeKind::team &&
                   node.node_id == participant.primary_team_id;
        });
    return team != state_.directory->nodes.end() && team->parent_node_id.has_value() &&
           *team->parent_node_id == selection.node_id;
}

void DesktopModel::applyMembership(client::MembershipView membership)
{
    const auto group_changed = state_.membership.has_value() &&
                               state_.membership->group_id.value() != membership.group_id.value();
    if (group_changed)
    {
        state_.directory.reset();
        state_.channel_nodes.clear();
        state_.presence_version.reset();
        state_.presence_observed_at.reset();
        state_.participants.clear();
        state_.selected_participant_ids.clear();
        server_presence_.clear();
        participant_voice_sequences_.clear();
        clearRemoteTransportState();
    }
    state_.administration = administrationFor(membership);
    state_.selected_channel =
        ChannelSelection{domain::VoiceScope::team, std::string{membership.team_id.value()}};
    state_.membership = std::move(membership);
    if (state_.directory.has_value())
    {
        state_.directory_phase = DirectoryPhase::stale;
        rebuildChannelState();
        refreshSelectedParticipants();
    }
    else
    {
        state_.directory_phase = DirectoryPhase::loading;
    }
}

void DesktopModel::rebuildChannelState()
{
    state_.channel_nodes.clear();
    if (!state_.directory.has_value())
    {
        return;
    }

    const auto sorted_nodes = [this](client::DirectoryNodeKind kind,
                                     const std::optional<std::string_view>& parent) {
        std::vector<const client::DirectoryNodeView*> nodes;
        for (const auto& node : state_.directory->nodes)
        {
            const auto parent_matches = !parent.has_value() ? !node.parent_node_id.has_value()
                                                            : node.parent_node_id.has_value() &&
                                                                  *node.parent_node_id == *parent;
            if (node.kind == kind && parent_matches)
            {
                nodes.push_back(&node);
            }
        }
        std::ranges::sort(nodes, [](const auto* left, const auto* right) {
            if (left->sort_index != right->sort_index)
            {
                return left->sort_index < right->sort_index;
            }
            if (left->display_name != right->display_name)
            {
                return left->display_name < right->display_name;
            }
            return left->node_id < right->node_id;
        });
        return nodes;
    };
    const auto append_node = [this](const client::DirectoryNodeView& node, std::uint8_t depth) {
        ChannelNodeState state;
        state.channel = {scopeFor(node.kind), node.node_id};
        state.parent_node_id = node.parent_node_id;
        state.display_name = node.display_name;
        state.depth = depth;
        state.sort_index = node.sort_index;
        if (state_.membership.has_value())
        {
            switch (state.channel.scope)
            {
            case domain::VoiceScope::group:
                state.contains_local_player =
                    state.channel.node_id == state_.membership->group_id.value();
                break;
            case domain::VoiceScope::specialization:
                state.contains_local_player =
                    state.channel.node_id == state_.membership->specialization_id.value();
                break;
            case domain::VoiceScope::team:
                state.contains_local_player =
                    state.channel.node_id == state_.membership->team_id.value();
                break;
            }
        }
        state.participant_count = static_cast<std::size_t>(
            std::ranges::count_if(state_.participants, [this, &state](const auto& entry) {
                return participantBelongsToChannel(entry.second, state.channel);
            }));
        state_.channel_nodes.push_back(std::move(state));
    };

    const auto groups = sorted_nodes(client::DirectoryNodeKind::group, std::nullopt);
    for (const auto* group : groups)
    {
        append_node(*group, 0);
        const auto specializations = sorted_nodes(client::DirectoryNodeKind::specialization,
                                                  std::string_view{group->node_id});
        for (const auto* specialization : specializations)
        {
            append_node(*specialization, 1);
            const auto teams = sorted_nodes(client::DirectoryNodeKind::team,
                                            std::string_view{specialization->node_id});
            for (const auto* team : teams)
            {
                append_node(*team, 2);
            }
        }
    }
}

void DesktopModel::refreshSelectedParticipants()
{
    state_.selected_participant_ids.clear();
    if (!state_.selected_channel.has_value() || !state_.directory.has_value())
    {
        return;
    }
    for (const auto& [participant_id, participant] : state_.participants)
    {
        if (participantBelongsToChannel(participant, *state_.selected_channel))
        {
            state_.selected_participant_ids.push_back(participant_id);
        }
    }
    std::ranges::sort(state_.selected_participant_ids,
                      [this](const auto& left_id, const auto& right_id) {
                          const auto& left = state_.participants.at(left_id);
                          const auto& right = state_.participants.at(right_id);
                          if (left.display_name != right.display_name)
                          {
                              return left.display_name < right.display_name;
                          }
                          return left_id < right_id;
                      });
}

void DesktopModel::clearRemoteTransportState() noexcept
{
    for (auto& [participant_id, participant] : state_.participants)
    {
        static_cast<void>(participant_id);
        participant.connected_scopes = {};
        participant.audio_available_scopes = {};
        participant.audio_available = false;
        participant.speaking = false;
        participant.speaking_scope.reset();
        refreshParticipantDerivedState(participant);
    }
}

void DesktopModel::refreshParticipantDerivedState(ParticipantState& participant) noexcept
{
    const auto locally_connected =
        std::ranges::any_of(participant.connected_scopes, [](bool connected) { return connected; });
    participant.audio_available = std::ranges::any_of(participant.audio_available_scopes,
                                                      [](bool available) { return available; });
    if (const auto server = server_presence_.find(participant.participant_id);
        server != server_presence_.end())
    {
        participant.presence =
            server->second || locally_connected ? PresenceState::online : PresenceState::offline;
    }
    else
    {
        participant.presence = locally_connected ? PresenceState::online : PresenceState::unknown;
    }
}

void DesktopModel::clearAuthenticatedState() noexcept
{
    state_.membership.reset();
    state_.selected_channel.reset();
    state_.directory_phase = DirectoryPhase::unavailable;
    state_.directory.reset();
    state_.channel_nodes.clear();
    state_.presence_version.reset();
    state_.presence_observed_at.reset();
    state_.participants.clear();
    state_.selected_participant_ids.clear();
    state_.active_transmission_scope.reset();
    state_.administration = {};
    state_.diagnostics = {};
    server_presence_.clear();
    participant_voice_sequences_.clear();
    voice_generation_ = 0;
    last_connection_sequence_ = 0;
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
