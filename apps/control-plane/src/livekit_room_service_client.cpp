#include "livekit_room_service_client.hpp"

#include <array>
#include <cerrno>
#include <netdb.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <system_error>
#include <unistd.h>

namespace hvc::control_plane
{
namespace
{
class Socket final
{
  public:
    explicit Socket(int descriptor) noexcept : descriptor_(descriptor)
    {
    }
    ~Socket()
    {
        if (descriptor_ >= 0)
        {
            static_cast<void>(::close(descriptor_));
        }
    }
    Socket(const Socket&) = delete;
    auto operator=(const Socket&) -> Socket& = delete;
    Socket(Socket&& other) noexcept : descriptor_(other.descriptor_)
    {
        other.descriptor_ = -1;
    }
    [[nodiscard]] auto get() const noexcept -> int
    {
        return descriptor_;
    }

  private:
    int descriptor_;
};

class AddressInfo final
{
  public:
    explicit AddressInfo(addrinfo* addresses) noexcept : addresses_(addresses)
    {
    }
    ~AddressInfo()
    {
        ::freeaddrinfo(addresses_);
    }
    AddressInfo(const AddressInfo&) = delete;
    auto operator=(const AddressInfo&) -> AddressInfo& = delete;
    [[nodiscard]] auto get() const noexcept -> addrinfo*
    {
        return addresses_;
    }

  private:
    addrinfo* addresses_;
};

[[nodiscard]] auto jsonEscape(std::string_view value) -> std::string
{
    std::string result{"\""};
    for (const char character : value)
    {
        if (character == '"' || character == '\\')
        {
            result.push_back('\\');
        }
        result.push_back(character);
    }
    result.push_back('"');
    return result;
}

void sendAll(int descriptor, std::string_view bytes)
{
    while (!bytes.empty())
    {
        const auto sent = ::send(descriptor, bytes.data(), bytes.size(), MSG_NOSIGNAL);
        if (sent < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            throw std::system_error{errno, std::generic_category(), "LiveKit send"};
        }
        bytes.remove_prefix(static_cast<std::size_t>(sent));
    }
}

[[nodiscard]] auto connectTo(std::string_view host, std::string_view port) -> Socket
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* raw_addresses{};
    const auto host_text = std::string{host};
    const auto port_text = std::string{port};
    const int lookup = ::getaddrinfo(host_text.c_str(), port_text.c_str(), &hints, &raw_addresses);
    if (lookup != 0)
    {
        throw std::runtime_error{"Cannot resolve LiveKit server."};
    }
    const AddressInfo addresses{raw_addresses};
    for (auto* address = addresses.get(); address != nullptr; address = address->ai_next)
    {
        const int descriptor =
            ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (descriptor < 0)
        {
            continue;
        }
        Socket socket{descriptor};
        constexpr timeval timeout{2, 0};
        static_cast<void>(
            ::setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)));
        static_cast<void>(
            ::setsockopt(descriptor, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)));
        if (::connect(descriptor, address->ai_addr, address->ai_addrlen) == 0)
        {
            return socket;
        }
    }
    throw std::runtime_error{"Cannot connect to LiveKit RoomService."};
}
} // namespace

LiveKitRoomServiceClient::LiveKitRoomServiceClient(std::string_view server_url)
{
    constexpr std::string_view websocket_prefix{"ws://"};
    constexpr std::string_view http_prefix{"http://"};
    if (server_url.starts_with(websocket_prefix))
    {
        server_url.remove_prefix(websocket_prefix.size());
    }
    else if (server_url.starts_with(http_prefix))
    {
        server_url.remove_prefix(http_prefix.size());
    }
    else
    {
        throw std::invalid_argument{"LiveKit control URL must use ws:// or http://."};
    }
    const auto path = server_url.find('/');
    const auto authority = server_url.substr(0, path);
    const auto colon = authority.rfind(':');
    host_ = std::string{authority.substr(0, colon)};
    port_ = colon == std::string_view::npos ? "7880" : std::string{authority.substr(colon + 1)};
    if (host_.empty() || port_.empty())
    {
        throw std::invalid_argument{"LiveKit control URL has an empty host or port."};
    }
}

auto LiveKitRoomServiceClient::updateParticipant(std::string_view room_name,
                                                 std::string_view participant_identity,
                                                 bool can_publish, bool can_subscribe,
                                                 std::string_view authorization_token) -> bool
{
    const auto body =
        "{\"room\":" + jsonEscape(room_name) + ",\"identity\":" + jsonEscape(participant_identity) +
        ",\"permission\":{\"canPublish\":" + (can_publish ? "true" : "false") +
        ",\"canSubscribe\":" + (can_subscribe ? "true" : "false") + ",\"canPublishData\":false}}";
    const auto request =
        "POST /twirp/livekit.RoomService/UpdateParticipant HTTP/1.1\r\nHost: " + host_ +
        "\r\nAuthorization: Bearer " + std::string{authorization_token} +
        "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
        "\r\nConnection: close\r\n\r\n" + body;
    auto socket = connectTo(host_, port_);
    sendAll(socket.get(), request);
    std::array<char, 512> response{};
    const auto received = ::recv(socket.get(), response.data(), response.size(), 0);
    if (received <= 0)
    {
        return false;
    }
    const std::string_view status_line{response.data(), static_cast<std::size_t>(received)};
    return status_line.starts_with("HTTP/1.1 200 ") || status_line.starts_with("HTTP/1.1 204 ");
}
} // namespace hvc::control_plane
