#pragma once

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
} // namespace hvc::livekit
