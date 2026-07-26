#pragma once

#include <condition_variable>
#include <cstdint>
#include <hvc/client/control_plane_client.hpp>
#include <hvc/client/voice_client.hpp>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace hvc::client
{
/// Identify the subsystem that produced a coordinated voice-session failure.
enum class VoiceSessionErrorSource : std::uint8_t
{
    /// Local client state rejected the operation.
    client_state,
    /// The authoritative control plane rejected or failed the operation.
    control_plane,
    /// The voice transport failed the operation.
    voice_transport
};

/// Describe a failure from a coordinated voice-session operation.
struct VoiceSessionError final
{
    /// Subsystem that produced the failure.
    VoiceSessionErrorSource source{VoiceSessionErrorSource::client_state};
    /// Stable machine-readable error code.
    std::string code;
    /// Human-readable diagnostic message.
    std::string message;
    /// HTTP status code for control-plane failures, otherwise zero.
    int status_code{0};
};

/// Hold the outcome of a coordinated authorization and voice operation.
struct VoiceSessionResult final
{
    /**
     * Create a successful session result.
     *
     * @param started_transmission Newly authorized transmission, when applicable.
     * @returns A result that evaluates to `true`.
     */
    [[nodiscard]] static auto success(std::optional<StartedTransmission> started_transmission =
                                          std::nullopt) -> VoiceSessionResult;
    /**
     * Create a failed session result.
     *
     * @param action_error Failure details.
     * @returns A result that evaluates to `false`.
     */
    [[nodiscard]] static auto failure(VoiceSessionError action_error) -> VoiceSessionResult;
    /**
     * Return whether the coordinated operation succeeded.
     *
     * @returns `true` after success.
     */
    [[nodiscard]] explicit operator bool() const noexcept;

    /// Whether the operation succeeded.
    bool successful{false};
    /// Newly authorized transmission, when the operation started one.
    std::optional<StartedTransmission> transmission;
    /// Failure details, absent after success.
    std::optional<VoiceSessionError> error;
};

/// Accept authorized push-to-talk press and release actions.
class IPushToTalkTarget
{
  public:
    /// Construct a push-to-talk target interface.
    IPushToTalkTarget() = default;
    /// Copy construction is disabled.
    IPushToTalkTarget(const IPushToTalkTarget&) = delete;
    /// Copy assignment is disabled.
    auto operator=(const IPushToTalkTarget&) -> IPushToTalkTarget& = delete;
    /// Move construction is disabled.
    IPushToTalkTarget(IPushToTalkTarget&&) = delete;
    /// Move assignment is disabled.
    auto operator=(IPushToTalkTarget&&) -> IPushToTalkTarget& = delete;
    /// Destroy the push-to-talk target interface.
    virtual ~IPushToTalkTarget() = default;

    /**
     * Authorize and start transmission in one scope.
     *
     * @param scope Scope selected by the input action.
     * @returns The coordinated control-plane and transport outcome.
     */
    [[nodiscard]] virtual auto pressPushToTalk(domain::VoiceScope scope) -> VoiceSessionResult = 0;
    /**
     * Stop audio and end the active authorized transmission.
     *
     * @returns The coordinated transport and control-plane outcome.
     */
    [[nodiscard]] virtual auto releasePushToTalk() -> VoiceSessionResult = 0;
};

/**
 * Coordinate server authorization with local voice transport operations.
 *
 * Audio publication starts only after the control plane authorizes the
 * transmission. The referenced clients must outlive this coordinator.
 */
class AuthorizedVoiceClient final : public IPushToTalkTarget
{
  public:
    /**
     * Construct a coordinated voice client.
     *
     * @param control_plane Authoritative client that must outlive this object.
     * @param voice_client Voice client that must outlive this object.
     */
    AuthorizedVoiceClient(ControlPlaneClient& control_plane, VoiceClient& voice_client);

    /**
     * Authenticate, fetch membership and grants, and connect voice rooms.
     *
     * @param external_credential Credential accepted by the configured server
     *     authenticator.
     * @returns The first failure encountered or a successful ready session.
     */
    [[nodiscard]] auto connect(std::string_view external_credential) -> VoiceSessionResult;
    /**
     * Stop active media, end the server transmission, and clear the session.
     *
     * @returns The first failure encountered, or success when all local state is
     *     cleared.
     */
    [[nodiscard]] auto disconnect() -> VoiceSessionResult;
    /**
     * Apply a newer membership and replace all room grants and subscriptions.
     *
     * A changed membership first stops local publication and never restores an
     * interrupted transmission. Unchanged versions are a no-op.
     *
     * @returns Refresh outcome or the first control-plane/transport failure.
     */
    [[nodiscard]] auto refreshAuthorization() -> VoiceSessionResult;
    /// @copydoc IPushToTalkTarget::pressPushToTalk
    [[nodiscard]] auto pressPushToTalk(domain::VoiceScope scope) -> VoiceSessionResult override;
    /// @copydoc IPushToTalkTarget::releasePushToTalk
    [[nodiscard]] auto releasePushToTalk() -> VoiceSessionResult override;
    /**
     * End server state left active after an interrupted local publication.
     *
     * @returns The control-plane outcome, or success when no transmission is
     *     pending cleanup.
     */
    [[nodiscard]] auto endInterruptedTransmission() -> VoiceSessionResult;

    /**
     * Return the cached authoritative membership.
     *
     * @returns Membership fetched during connection, or no value while
     *     disconnected.
     */
    [[nodiscard]] auto membership() const -> std::optional<MembershipView>;
    /**
     * Return the cached active server transmission.
     *
     * @returns Active server transmission, or no value when none is active.
     */
    [[nodiscard]] auto activeTransmission() const -> std::optional<StartedTransmission>;

  private:
    [[nodiscard]] static auto controlPlaneFailure(const ControlPlaneError& error)
        -> VoiceSessionResult;
    [[nodiscard]] static auto transportFailure(const VoiceTransportResult& result)
        -> VoiceSessionResult;

    ControlPlaneClient& control_plane_;
    VoiceClient& voice_client_;
    std::optional<MembershipView> membership_;
    std::optional<StartedTransmission> active_transmission_;
    bool push_to_talk_enabled_{false};
    MicrophonePublicationState publication_state_{MicrophonePublicationState::idle};
    std::uint64_t publication_generation_{0};
    bool voice_start_in_progress_{false};
    VoiceSessionResult last_release_result_{};
    mutable std::mutex mutex_;
    std::condition_variable publication_changed_;
};
} // namespace hvc::client
