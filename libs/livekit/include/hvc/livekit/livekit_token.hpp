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

struct SignedRoomGrant final
{
    domain::VoiceScope scope;
    std::string room_name;
    std::string token;
};

class LiveKitTokenAdapter final
{
  public:
    explicit LiveKitTokenAdapter(LiveKitCredentials credentials);

    [[nodiscard]] auto sign(const application::VoiceGrantClaims& claims) const
        -> std::vector<SignedRoomGrant>;

  private:
    LiveKitCredentials credentials_;
};
} // namespace hvc::livekit
