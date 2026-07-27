#include <cmath>
#include <cstdio>
#include <exception>
#include <hvc/presentation/desktop_model.hpp>
#include <string>
#include <vector>

namespace
{
using namespace hvc;
using namespace hvc::presentation;

[[nodiscard]] auto membership(std::uint64_t version, std::string role = "participant")
    -> client::MembershipView
{
    return {version,
            domain::HierarchyId{"hierarchy-1"},
            domain::PlayerId{"player-1"},
            domain::GroupId{"group-1"},
            domain::SpecializationId{"specialization-1"},
            domain::TeamId{"team-1"},
            {domain::RoleId{std::move(role)}},
            true,
            true,
            false};
}

[[nodiscard]] auto directory(std::uint64_t version = 4) -> client::DirectoryView
{
    return {version,
            "group-1",
            {{"group-1", client::DirectoryNodeKind::group, std::nullopt, "Group", 0},
             {"specialization-1", client::DirectoryNodeKind::specialization, "group-1",
              "Specialization", 0},
             {"team-1", client::DirectoryNodeKind::team, "specialization-1", "Team", 0}},
            {{"leader", "Leader"}},
            {{"player-1", "Local Player", "team-1", {}},
             {"speaker-1", "Remote Player", "team-1", {"leader"}}}};
}

auto testConnectionAndMembershipLifecycle() -> bool
{
    DesktopModel model;
    if (!model.beginConnect() || model.state().connection != ConnectionPhase::connecting ||
        model.beginConnect())
    {
        return false;
    }
    if (!model.connectionSucceeded(membership(7)) ||
        model.state().connection != ConnectionPhase::ready ||
        !model.state().selected_channel.has_value() ||
        model.state().selected_channel->node_id != "team-1")
    {
        return false;
    }
    if (model.updateMembership(membership(7)).error != ErrorCode::stale_version ||
        !model.updateMembership(membership(8)))
    {
        return false;
    }
    model.transmissionStarted(domain::VoiceScope::group);
    model.updateVoiceState({client::VoiceTransportState::reconnecting, 2, 10});
    if (model.state().connection != ConnectionPhase::reconnecting ||
        model.state().active_transmission_scope.has_value())
    {
        return false;
    }
    if (!model.beginDisconnect() || model.state().connection != ConnectionPhase::disconnecting)
    {
        return false;
    }
    model.disconnected();
    return model.state().connection == ConnectionPhase::signed_out &&
           !model.state().membership.has_value() && model.state().participants.empty();
}

auto testCommandKindConstructorDefaultsPayload() -> bool
{
    const Command command{CommandKind::disconnect};
    return command.kind == CommandKind::disconnect && !command.channel.has_value() &&
           command.participant_id.empty() && command.participant_volume == 1.0F &&
           !command.enabled && !command.settings.has_value();
}

auto testSeparatedParticipantState() -> bool
{
    DesktopModel model;
    if (!model.beginConnect() || !model.connectionSucceeded(membership(1)))
    {
        return false;
    }
    if (!model.applyDirectory(directory()))
    {
        return false;
    }
    model.applyVoiceRemoteEvent({client::VoiceRemoteEventKind::audio_available,
                                 domain::VoiceScope::specialization, "speaker-1", 1, 1});
    model.applyVoiceRemoteEvent({client::VoiceRemoteEventKind::speaker_started,
                                 domain::VoiceScope::specialization, "speaker-1", 1, 2});
    const auto& active = model.state().participants.at("speaker-1");
    if (!active.audio_available || !active.speaking || active.presence != PresenceState::unknown ||
        active.speaking_scope != domain::VoiceScope::specialization)
    {
        return false;
    }
    if (!model.setParticipantVolume("speaker-1", 0.4F) ||
        !model.setParticipantMuted("speaker-1", true))
    {
        return false;
    }
    model.applyVoiceRemoteEvent({client::VoiceRemoteEventKind::speaker_stopped,
                                 domain::VoiceScope::specialization, "speaker-1", 1, 3});
    const auto& stopped = model.state().participants.at("speaker-1");
    return !stopped.speaking && !stopped.speaking_scope.has_value() && stopped.muted &&
           std::abs(stopped.volume - 0.4F) < 0.001F;
}

auto testDirectoryPresenceAndTransportAggregation() -> bool
{
    DesktopModel model;
    if (!model.beginConnect() || !model.connectionSucceeded(membership(1)) ||
        !model.applyDirectory(directory()))
    {
        return false;
    }
    const auto observed = std::chrono::system_clock::time_point{std::chrono::seconds{100}};
    if (!model.applyPresence({10,
                              client::DirectoryPresenceMode::snapshot,
                              observed,
                              {{"player-1", true}, {"speaker-1", false}},
                              std::chrono::seconds{1}}))
    {
        return false;
    }
    model.applyVoiceRemoteEvent({client::VoiceRemoteEventKind::participant_connected,
                                 domain::VoiceScope::team, "speaker-1", 1, 1});
    model.applyVoiceRemoteEvent({client::VoiceRemoteEventKind::participant_connected,
                                 domain::VoiceScope::group, "speaker-1", 1, 2});
    if (model.state().participants.at("speaker-1").presence != PresenceState::online)
    {
        return false;
    }
    model.applyVoiceRemoteEvent({client::VoiceRemoteEventKind::participant_disconnected,
                                 domain::VoiceScope::team, "speaker-1", 1, 3});
    if (model.state().participants.at("speaker-1").presence != PresenceState::online)
    {
        return false;
    }
    model.applyVoiceRemoteEvent({client::VoiceRemoteEventKind::participant_disconnected,
                                 domain::VoiceScope::group, "speaker-1", 1, 4});
    if (model.state().participants.at("speaker-1").presence != PresenceState::offline)
    {
        return false;
    }
    if (!model.applyPresence({11,
                              client::DirectoryPresenceMode::delta,
                              observed + std::chrono::seconds{1},
                              {{"speaker-1", true}},
                              std::chrono::seconds{1}}) ||
        model.state().participants.at("speaker-1").presence != PresenceState::online ||
        model.applyPresence(
                 {10, client::DirectoryPresenceMode::delta, observed, {}, std::chrono::seconds{1}})
                .error != ErrorCode::stale_version)
    {
        return false;
    }

    model.applyVoiceRemoteEvent({client::VoiceRemoteEventKind::participant_disconnected,
                                 domain::VoiceScope::group, "speaker-1", 1, 3});
    model.applyVoiceRemoteEvent({client::VoiceRemoteEventKind::audio_available,
                                 domain::VoiceScope::group, "speaker-1", 1, 5});
    model.applyVoiceRemoteEvent({client::VoiceRemoteEventKind::audio_unavailable,
                                 domain::VoiceScope::group, "speaker-1", 1, 6});
    model.applyVoiceRemoteEvent({client::VoiceRemoteEventKind::speaker_stopped,
                                 domain::VoiceScope::group, "speaker-1", 1, 8});
    model.applyVoiceRemoteEvent({client::VoiceRemoteEventKind::audio_available,
                                 domain::VoiceScope::group, "speaker-1", 1, 7});
    if (!model.state().participants.at("speaker-1").audio_available ||
        model.state().participants.at("speaker-1").speaking)
    {
        return false;
    }
    model.updateVoiceState({client::VoiceTransportState::reconnecting, 2, 9});
    const auto& reset = model.state().participants.at("speaker-1");
    return reset.presence == PresenceState::online && !reset.audio_available && !reset.speaking &&
           model.applyDirectory(directory(4)).error == ErrorCode::stale_version;
}

auto testCommandValidationAndAdministration() -> bool
{
    DesktopModel participant_model;
    if (!participant_model.beginConnect() ||
        !participant_model.connectionSucceeded(membership(1, "not-moderator")))
    {
        return false;
    }
    if (participant_model.state().administration.can_moderate ||
        participant_model.validate(Command{CommandKind::open_administration}).error !=
            ErrorCode::forbidden)
    {
        return false;
    }

    DesktopModel administrator_model;
    if (!administrator_model.beginConnect() ||
        !administrator_model.connectionSucceeded(membership(1, "Administrator")) ||
        !administrator_model.state().administration.can_administrate ||
        !administrator_model.validate(Command{CommandKind::open_administration}))
    {
        return false;
    }

    Command missing_participant{CommandKind::set_participant_volume};
    missing_participant.participant_id = "unknown";
    missing_participant.participant_volume = 0.5F;
    return administrator_model.validate(missing_participant).error == ErrorCode::not_found;
}

auto testSettingsValidation() -> bool
{
    DesktopModel model;
    if (!model.beginConnect() || !model.connectionSucceeded(membership(1)))
    {
        return false;
    }
    SettingsState settings;
    settings.recording_devices.push_back({"recording-1", "Microphone"});
    settings.playout_devices.push_back({"playout-1", "Speakers"});
    settings.recording_device_id = "recording-1";
    settings.playout_device_id = "playout-1";
    settings.text_scale_percent = 150;

    Command command{CommandKind::apply_settings};
    command.settings = settings;
    if (!model.validate(command))
    {
        return false;
    }
    command.settings->text_scale_percent = 250;
    if (model.validate(command).field != "settings.accessibility.text_scale_percent")
    {
        return false;
    }
    command.settings = settings;
    command.settings->recording_device_id = "missing";
    return model.validate(command).field == "settings.recording_device_id";
}

auto testDiagnosticsAreSessionBounded() -> bool
{
    DesktopModel model;
    if (!model.beginConnect() || !model.connectionSucceeded(membership(1)))
    {
        return false;
    }
    model.recordError("voice_transport_publication_failed", "publication rejected");
    if (model.state().diagnostics.error_count != 1 ||
        model.state().diagnostics.last_error_code != "voice_transport_publication_failed")
    {
        return false;
    }
    model.disconnected();
    return model.state().diagnostics.error_count == 0 &&
           model.state().diagnostics.last_diagnostic.empty();
}
} // namespace

auto main() noexcept -> int
{
    try
    {
        const auto passed =
            testCommandKindConstructorDefaultsPayload() && testConnectionAndMembershipLifecycle() &&
            testSeparatedParticipantState() && testDirectoryPresenceAndTransportAggregation() &&
            testCommandValidationAndAdministration() && testSettingsValidation() &&
            testDiagnosticsAreSessionBounded();
        if (!passed)
        {
            std::fputs("presentation desktop-model tests failed\n", stderr);
            return 1;
        }
        std::puts("presentation desktop-model tests passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "unexpected exception: %s\n", error.what());
        return 1;
    }
}
