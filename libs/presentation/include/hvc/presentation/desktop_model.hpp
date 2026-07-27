#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <hvc/client/control_plane_client.hpp>
#include <hvc/client/ptt_input.hpp>
#include <hvc/client/voice_client.hpp>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * Provide UI-toolkit-independent state and commands for HVC desktop clients.
 */
namespace hvc::presentation
{
/// Identify the top-level connection phase presented by a desktop shell.
enum class ConnectionPhase : std::uint8_t
{
    /// No authenticated client session exists.
    signed_out,
    /// A client session is being established off the UI thread.
    connecting,
    /// Control-plane, voice, and input services are ready.
    ready,
    /// Voice connectivity is recovering and PTT remains stopped.
    reconnecting,
    /// A client session is being torn down off the UI thread.
    disconnecting
};

/// Identify the lifecycle of a user-requested presentation operation.
enum class OperationPhase : std::uint8_t
{
    /// No operation has been requested.
    idle,
    /// The operation is executing outside the UI thread.
    pending,
    /// The latest operation completed successfully.
    succeeded,
    /// The latest operation failed without invalidating the model.
    failed
};

/// Identify whether a participant is known to be connected.
enum class PresenceState : std::uint8_t
{
    /// No authoritative presence observation exists.
    unknown,
    /// All observed presences are disconnected.
    offline,
    /// At least one authorized scope presence is connected.
    online
};

/// Identify stable failures shared by WinUI and Qt presentation adapters.
enum class ErrorCode : std::uint8_t
{
    /// No failure occurred.
    none,
    /// The command is not valid in the current lifecycle phase.
    invalid_state,
    /// A command field is empty, out of range, or inconsistent.
    invalid_argument,
    /// The referenced participant or hierarchy node is unknown.
    not_found,
    /// The current public role view does not expose the requested area.
    forbidden,
    /// An older membership update was ignored.
    stale_version,
    /// A session or platform adapter reported an operation failure.
    operation_failed
};

/// Identify commands emitted by either desktop window toolkit.
enum class CommandKind : std::uint8_t
{
    /// Establish a new authenticated client session.
    connect,
    /// Tear down the active client session.
    disconnect,
    /// Select a server-defined hierarchy node for presentation.
    select_channel,
    /// Open the independent settings window.
    open_settings,
    /// Open the independent diagnostics window.
    open_diagnostics,
    /// Open the role-gated administration area.
    open_administration,
    /// Apply a complete settings snapshot.
    apply_settings,
    /// Change local playback volume for one participant.
    set_participant_volume,
    /// Change local mute for one participant.
    set_participant_muted,
    /// Change local block for one participant.
    set_participant_blocked
};

/// Identify a selected server-defined hierarchy node.
struct ChannelSelection final
{
    /// Scope represented by the hierarchy node.
    domain::VoiceScope scope{domain::VoiceScope::team};
    /// Stable server-provided node identifier.
    std::string node_id;

    /**
     * Compare complete selections.
     *
     * @returns `true` when scope and node identifier match.
     */
    [[nodiscard]] auto operator==(const ChannelSelection&) const -> bool = default;
};

/// Hold a toolkit-independent participant row.
struct ParticipantState final
{
    /// Stable public participant identifier.
    std::string participant_id;
    /// Public display name, or the participant identifier when not yet available.
    std::string display_name;
    /// Stable primary-team identifier when provided by the directory.
    std::string primary_team_id;
    /// Public roles exposed by the directory contract.
    std::vector<std::string> role_ids;
    /// Aggregated transport presence.
    PresenceState presence{PresenceState::unknown};
    /// Locally observed connected Voice scopes.
    std::array<bool, 3> connected_scopes{};
    /// Locally observed remote-audio availability per Voice scope.
    std::array<bool, 3> audio_available_scopes{};
    /// Whether a remote microphone track is available.
    bool audio_available{false};
    /// Whether admitted remote audio is currently audible.
    bool speaking{false};
    /// Scope of the currently audible track.
    std::optional<domain::VoiceScope> speaking_scope;
    /// Local linear participant gain in the inclusive range `[0.0F, 1.0F]`.
    float volume{1.0F};
    /// Whether local playout is muted.
    bool muted{false};
    /// Whether all local tracks from the participant are blocked.
    bool blocked{false};
};

/// Hold the complete editable settings state shared by desktop shells.
struct SettingsState final
{
    /// Stream-admission and ducking configuration.
    client::AudioEngineConfig audio;
    /// Recording devices supplied by the active platform adapter.
    std::vector<client::AudioDevice> recording_devices;
    /// Playout devices supplied by the active platform adapter.
    std::vector<client::AudioDevice> playout_devices;
    /// Selected recording-device identifier, empty when no device is available.
    std::string recording_device_id;
    /// Selected playout-device identifier, empty when no device is available.
    std::string playout_device_id;
    /// Complete atomic PTT binding set.
    std::vector<client::InputBinding> bindings;
    /// Input-device capabilities supplied by the active platform adapter.
    std::vector<client::InputDeviceProfile> input_devices;
    /// UI text scale as an integer percentage.
    std::uint16_t text_scale_percent{100};
    /// Whether speaking text receives additional non-color emphasis.
    bool strong_speaker_indicators{false};
    /// Lifecycle of the latest apply request.
    OperationPhase apply_phase{OperationPhase::idle};
};

/// Hold role-derived visibility and administration operation state.
struct AdministrationState final
{
    /// Whether the current public roles expose moderation features.
    bool can_moderate{false};
    /// Whether the current public roles expose account and policy management.
    bool can_administrate{false};
    /// Lifecycle of the latest administration operation.
    OperationPhase operation_phase{OperationPhase::idle};
};

/// Hold privacy-bounded diagnostics surfaced by a desktop shell.
struct DiagnosticsState final
{
    /// Latest observed voice connection state.
    client::VoiceTransportState voice_state{client::VoiceTransportState::disconnected};
    /// Stable machine-readable error code, empty when no error was reported.
    std::string last_error_code;
    /// Technical diagnostic supplied by an adapter, never a UI string contract.
    std::string last_diagnostic;
    /// Number of failures observed during the current authenticated session.
    std::uint64_t error_count{0};
};

/// Hold all desktop presentation state without WinUI or Qt types.
struct DesktopState final
{
    /// Current client lifecycle phase.
    ConnectionPhase connection{ConnectionPhase::signed_out};
    /// Latest authoritative membership.
    std::optional<client::MembershipView> membership;
    /// Selected hierarchy node.
    std::optional<ChannelSelection> selected_channel;
    /// Latest complete visible Directory snapshot.
    std::optional<client::DirectoryView> directory;
    /// Latest applied server Presence version.
    std::optional<std::uint64_t> presence_version;
    /// Observation time of the latest applied Presence response.
    std::optional<std::chrono::system_clock::time_point> presence_observed_at;
    /// Participants keyed by stable public identifier.
    std::map<std::string, ParticipantState, std::less<>> participants;
    /// Confirmed active local transmission scope.
    std::optional<domain::VoiceScope> active_transmission_scope;
    /// Editable settings snapshot.
    SettingsState settings;
    /// Role-derived administration state.
    AdministrationState administration;
    /// Privacy-bounded diagnostic summary.
    DiagnosticsState diagnostics;
};

/// Describe a toolkit-independent user command and its typed payload.
struct Command final
{
    /// Construct a connection command with default payload values.
    Command() = default;
    /**
     * Construct one command classification with default payload values.
     *
     * @param command_kind Classification assigned to `kind`.
     */
    constexpr explicit Command(CommandKind command_kind) noexcept : kind(command_kind)
    {
    }

    /// Command classification.
    CommandKind kind{CommandKind::connect};
    /// Server-defined channel selection for `select_channel`.
    std::optional<ChannelSelection> channel;
    /// Participant identifier for local participant operations.
    std::string participant_id;
    /// Participant volume for `set_participant_volume`.
    float participant_volume{1.0F};
    /// Boolean value for mute and block commands.
    bool enabled{false};
    /// Complete candidate settings for `apply_settings`.
    std::optional<SettingsState> settings;
};

/// Hold a stable validation result without localized presentation text.
struct ValidationResult final
{
    /// Stable failure classification.
    ErrorCode error{ErrorCode::none};
    /// Machine-readable field or invariant associated with the failure.
    std::string field;

    /**
     * Return whether validation succeeded.
     *
     * @returns `true` when `error` is `ErrorCode::none`.
     */
    [[nodiscard]] explicit operator bool() const noexcept;
};

/**
 * Reduce client and user events into deterministic desktop presentation state.
 *
 * The model is not internally synchronized. A platform shell owns one instance
 * on its UI thread and queues transport events to that thread before mutation.
 */
class DesktopModel final
{
  public:
    /**
     * Return the current immutable presentation snapshot.
     *
     * @returns State owned by this model.
     */
    [[nodiscard]] auto state() const noexcept -> const DesktopState&;

    /**
     * Validate a user command against the current state.
     *
     * @param command Complete command and payload.
     * @returns A stable error and field name; success contains `ErrorCode::none`.
     */
    [[nodiscard]] auto validate(const Command& command) const -> ValidationResult;

    /**
     * Begin establishing a client session.
     *
     * @returns Validation outcome; success changes the phase to `connecting`.
     */
    [[nodiscard]] auto beginConnect() -> ValidationResult;
    /**
     * Publish a successfully established authoritative membership.
     *
     * @param membership Initial authoritative membership.
     * @returns Validation outcome; an invalid or stale membership is not applied.
     */
    [[nodiscard]] auto connectionSucceeded(client::MembershipView membership) -> ValidationResult;
    /**
     * Return to signed-out state after connection failure.
     *
     * @param error_code Stable adapter or protocol error.
     * @param diagnostic Non-localized technical detail.
     */
    void connectionFailed(std::string error_code, std::string diagnostic);
    /**
     * Begin tearing down the current client session.
     *
     * @returns Validation outcome; success changes the phase to `disconnecting`.
     */
    [[nodiscard]] auto beginDisconnect() -> ValidationResult;
    /// Clear all authenticated and transient state after teardown.
    void disconnected() noexcept;

    /**
     * Apply an aggregate voice transport state.
     *
     * @param event Ordered aggregate transport transition.
     */
    void updateVoiceState(const client::VoiceConnectionEvent& event) noexcept;
    /**
     * Apply a strictly newer authoritative membership.
     *
     * @param membership Candidate membership.
     * @returns `stale_version` when the candidate does not advance the version.
     */
    [[nodiscard]] auto updateMembership(client::MembershipView membership) -> ValidationResult;
    /**
     * Select a server-defined hierarchy node.
     *
     * @param selection Stable scope and node identifier.
     * @returns Validation outcome.
     */
    [[nodiscard]] auto selectChannel(ChannelSelection selection) -> ValidationResult;

    /**
     * Apply a complete, strictly newer visible Directory snapshot.
     *
     * @param directory Candidate snapshot for the current Membership Group.
     * @returns Validation outcome; stale or cross-Group snapshots are ignored.
     */
    [[nodiscard]] auto applyDirectory(client::DirectoryView directory) -> ValidationResult;
    /**
     * Apply a Presence snapshot or versioned delta.
     *
     * @param presence Candidate response for the current Directory.
     * @returns Validation outcome; deltas require an established version.
     */
    [[nodiscard]] auto applyPresence(client::DirectoryPresenceView presence) -> ValidationResult;
    /**
     * Apply one ordered, generation-bound remote transport transition.
     *
     * @param event Normalized VoiceClient event.
     */
    void applyVoiceRemoteEvent(const client::VoiceRemoteEvent& event);
    /**
     * Publish the confirmed local transmission scope.
     *
     * @param scope Confirmed scope.
     */
    void transmissionStarted(domain::VoiceScope scope) noexcept;
    /// Clear the confirmed local transmission scope.
    void transmissionStopped() noexcept;

    /**
     * Replace the settings snapshot obtained from platform adapters.
     *
     * @param settings Complete settings state.
     */
    void replaceSettings(SettingsState settings);
    /**
     * Apply a validated local participant volume.
     *
     * @param participant_id Stable participant identifier.
     * @param volume Linear gain in the inclusive range `[0.0F, 1.0F]`.
     * @returns Validation outcome.
     */
    [[nodiscard]] auto setParticipantVolume(std::string_view participant_id, float volume)
        -> ValidationResult;
    /**
     * Apply a confirmed local participant mute state.
     *
     * @param participant_id Stable participant identifier.
     * @param muted Whether local playout is muted.
     * @returns Validation outcome.
     */
    [[nodiscard]] auto setParticipantMuted(std::string_view participant_id, bool muted)
        -> ValidationResult;

    /**
     * Record a privacy-bounded adapter or protocol failure.
     *
     * @param error_code Stable machine-readable error code.
     * @param diagnostic Non-localized technical detail.
     */
    void recordError(std::string error_code, std::string diagnostic);

  private:
    struct ParticipantVoiceSequences final
    {
        std::array<std::uint64_t, 3> connected{};
        std::array<std::uint64_t, 3> audio_available{};
        std::array<std::uint64_t, 3> speaking{};
    };

    [[nodiscard]] static auto validateSettings(const SettingsState& settings) -> ValidationResult;
    [[nodiscard]] static auto administrationFor(const client::MembershipView& membership)
        -> AdministrationState;
    void applyMembership(client::MembershipView membership);
    void clearRemoteTransportState() noexcept;
    void refreshParticipantDerivedState(ParticipantState& participant) noexcept;
    void clearAuthenticatedState() noexcept;

    DesktopState state_;
    std::map<std::string, bool, std::less<>> server_presence_;
    std::map<std::string, ParticipantVoiceSequences, std::less<>> participant_voice_sequences_;
    std::uint64_t voice_generation_{0};
    std::uint64_t last_connection_sequence_{0};
};

/**
 * Return the stable ASCII name of a presentation error.
 *
 * @param error Error classification.
 * @returns Stable machine-readable name.
 */
[[nodiscard]] auto errorCodeName(ErrorCode error) noexcept -> std::string_view;
} // namespace hvc::presentation
