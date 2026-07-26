#pragma once

#include <hvc/livekit/livekit_token.hpp>
#include <string>
#include <string_view>

namespace hvc::control_plane
{
/**
 * Call LiveKit's internal RoomService endpoint over HTTP.
 *
 * The adapter is intended for the private Compose network. TLS termination is
 * handled by the deployment proxy, while API credentials remain server-side.
 */
class LiveKitRoomServiceClient final : public livekit::ILiveKitRoomServiceClient
{
  public:
    /**
     * Construct a RoomService client from a LiveKit WebSocket or HTTP URL.
     *
     * @param server_url URL using `ws://` or `http://` with an optional port.
     * @throws std::invalid_argument Thrown for unsupported or malformed URLs.
     */
    explicit LiveKitRoomServiceClient(std::string_view server_url);

    /// @copydoc livekit::ILiveKitRoomServiceClient::updateParticipant
    [[nodiscard]] auto updateParticipant(std::string_view room_name,
                                         std::string_view participant_identity, bool can_publish,
                                         bool can_subscribe, std::string_view authorization_token)
        -> bool override;

  private:
    std::string host_;
    std::string port_;
};
} // namespace hvc::control_plane
