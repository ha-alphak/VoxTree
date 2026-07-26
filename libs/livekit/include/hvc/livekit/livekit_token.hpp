#pragma once

#include <atomic>
#include <cstdint>
#include <hvc/application/control_plane.hpp>
#include <string>
#include <string_view>
#include <vector>

/**
 * Adapt HVC voice grants and transport operations to the native LiveKit SDK.
 */
namespace hvc::livekit
{
/// Hold validated LiveKit API credentials used to sign access tokens.
struct LiveKitCredentials final
{
    /**
     * Construct validated API credentials.
     *
     * @param key Non-empty LiveKit API key.
     * @param secret Non-empty LiveKit API secret.
     * @throws std::invalid_argument Thrown when either credential is empty.
     * @exceptsafe Strong exception guarantee.
     */
    LiveKitCredentials(std::string key, std::string secret);

    /// LiveKit API key encoded into token claims.
    std::string api_key;
    /// Secret used to sign tokens; must not be logged or persisted.
    std::string api_secret;
};

/// Signed room grant representation returned to the application layer.
using SignedRoomGrant = application::IssuedVoiceRoomGrant;

/**
 * Sign scope-isolated LiveKit room grants from authorized voice claims.
 *
 * Tokens contain only the identity, room, and publish/subscribe permissions
 * required by each scope.
 */
class LiveKitTokenAdapter final : public application::IVoiceGrantIssuer
{
  public:
    /**
     * Construct a token adapter.
     *
     * @param credentials Validated API credentials retained by the adapter.
     */
    explicit LiveKitTokenAdapter(LiveKitCredentials credentials);

    /**
     * Sign all room grants authorized by a claim set.
     *
     * @param claims Authoritative player, membership, scope, and expiry claims.
     * @returns One signed grant for each authorized room scope.
     * @throws std::invalid_argument Thrown when claims are inconsistent or
     *     contain no permitted scope.
     */
    [[nodiscard]] auto sign(const application::VoiceGrantClaims& claims) const
        -> std::vector<SignedRoomGrant>;
    /// @copydoc application::IVoiceGrantIssuer::issue
    [[nodiscard]] auto issue(const application::VoiceGrantClaims& claims) const
        -> std::vector<application::IssuedVoiceRoomGrant> override;

  private:
    LiveKitCredentials credentials_;
};

/**
 * Send one authenticated participant-permission update to LiveKit RoomService.
 */
class ILiveKitRoomServiceClient
{
  public:
    /// Destroy the RoomService client interface.
    virtual ~ILiveKitRoomServiceClient() = default;

    /**
     * Replace one participant's publish permission.
     *
     * @param room_name Exact LiveKit room.
     * @param participant_identity Exact LiveKit participant identity.
     * @param can_publish Whether audio publication is permitted.
     * @param can_subscribe Whether room-track subscription is permitted.
     * @param authorization_token Short-lived RoomService bearer token.
     * @returns `true` only after LiveKit confirms the update.
     */
    [[nodiscard]] virtual auto updateParticipant(std::string_view room_name,
                                                 std::string_view participant_identity,
                                                 bool can_publish, bool can_subscribe,
                                                 std::string_view authorization_token) -> bool = 0;
};

/**
 * Couple LiveKit publication permission to server-side transmission state.
 *
 * Every issued room token starts with publication disabled. This observer
 * enables exactly the active scope and disables it again for every terminal
 * lifecycle event.
 */
class LiveKitPublicationController final : public application::ITransmissionLifecycleObserver
{
  public:
    /**
     * Construct a publication controller.
     *
     * @param credentials LiveKit API credentials retained by the controller.
     * @param room_service RoomService client that must outlive the controller.
     */
    LiveKitPublicationController(LiveKitCredentials credentials,
                                 ILiveKitRoomServiceClient& room_service);

    /// @copydoc application::ITransmissionLifecycleObserver::onStarted
    [[nodiscard]] auto onStarted(const application::ActiveTransmission& transmission)
        -> bool override;
    /// @copydoc application::ITransmissionLifecycleObserver::onEnded
    void onEnded(const application::EndedTransmission& transmission) noexcept override;

    /**
     * Return the number of failed RoomService operations.
     *
     * @returns Process-local cumulative failure count.
     */
    [[nodiscard]] auto failureCount() const noexcept -> std::uint64_t;

  private:
    [[nodiscard]] auto update(const application::ActiveTransmission& transmission, bool can_publish)
        -> bool;

    LiveKitCredentials credentials_;
    ILiveKitRoomServiceClient& room_service_;
    std::atomic<std::uint64_t> failure_count_{0};
};
} // namespace hvc::livekit
