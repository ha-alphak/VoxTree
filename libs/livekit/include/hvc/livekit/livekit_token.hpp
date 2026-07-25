#pragma once

#include <hvc/application/control_plane.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace hvc::livekit
{
struct LiveKitCredentials final
{
    LiveKitCredentials(std::string key, std::string secret);

    std::string api_key;
    std::string api_secret;
};

using SignedRoomGrant = application::IssuedVoiceRoomGrant;

class LiveKitTokenAdapter final : public application::IVoiceGrantIssuer
{
  public:
    explicit LiveKitTokenAdapter(LiveKitCredentials credentials);

    [[nodiscard]] auto sign(const application::VoiceGrantClaims& claims) const
        -> std::vector<SignedRoomGrant>;
    [[nodiscard]] auto issue(const application::VoiceGrantClaims& claims) const
        -> std::vector<application::IssuedVoiceRoomGrant> override;

  private:
    LiveKitCredentials credentials_;
};
} // namespace hvc::livekit
