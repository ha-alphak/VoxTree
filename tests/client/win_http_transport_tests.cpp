#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <hvc/client/win_http_transport.hpp>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>

namespace
{
class WinsockLifetime final
{
  public:
    WinsockLifetime() : available_(WSAStartup(MAKEWORD(2, 2), &data_) == 0)
    {
    }

    ~WinsockLifetime()
    {
        if (available_)
        {
            WSACleanup();
        }
    }

    WinsockLifetime(const WinsockLifetime&) = delete;
    auto operator=(const WinsockLifetime&) -> WinsockLifetime& = delete;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return available_;
    }

  private:
    WSADATA data_{};
    bool available_{false};
};

[[nodiscard]] auto sendAll(SOCKET socket, std::string_view bytes) -> bool
{
    while (!bytes.empty())
    {
        const auto size = static_cast<int>(bytes.size());
        // Winsock receives the explicit byte count; no terminator is required.
        // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
        const auto sent = ::send(socket, bytes.data(), size, 0);
        if (sent <= 0)
        {
            return false;
        }
        bytes.remove_prefix(static_cast<std::size_t>(sent));
    }
    return true;
}

[[nodiscard]] auto testResponseHeaders() -> bool
{
    WinsockLifetime winsock;
    if (!winsock)
    {
        return false;
    }

    const auto listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET)
    {
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(listener, 1) != 0)
    {
        closesocket(listener);
        return false;
    }

    int address_size = sizeof(address);
    if (::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &address_size) != 0)
    {
        closesocket(listener);
        return false;
    }

    std::atomic_bool server_success{false};
    std::jthread server{[listener, &server_success] {
        const auto connection = ::accept(listener, nullptr, nullptr);
        if (connection == INVALID_SOCKET)
        {
            return;
        }

        std::string request;
        std::array<char, 1024> buffer{};
        while (request.find("\r\n\r\n") == std::string::npos && request.size() < 8192)
        {
            const auto received =
                ::recv(connection, buffer.data(), static_cast<int>(buffer.size()), 0);
            if (received <= 0)
            {
                closesocket(connection);
                return;
            }
            request.append(buffer.data(), static_cast<std::size_t>(received));
        }

        constexpr std::string_view response{"HTTP/1.1 200 OK\r\n"
                                            "X-HVC-API-Version: v1\r\n"
                                            "ETag: \"directory-42\"\r\n"
                                            "Retry-After: 7\r\n"
                                            "Content-Type: application/json\r\n"
                                            "Content-Length: 2\r\n"
                                            "Connection: close\r\n"
                                            "\r\n"
                                            "{}"};
        server_success.store(sendAll(connection, response), std::memory_order_release);
        ::shutdown(connection, SD_BOTH);
        closesocket(connection);
    }};

    const auto port = ntohs(address.sin_port);
    hvc::client::WinHttpTransport transport{"http://127.0.0.1:" + std::to_string(port),
                                            std::chrono::seconds{5}};
    const auto response = transport.send(hvc::client::ClientHttpRequest{"GET", "/headers", {}, {}});

    server.join();
    closesocket(listener);
    return server_success.load(std::memory_order_acquire) && response.status_code == 200 &&
           response.body == "{}" && response.transport_error.empty() &&
           response.headers.at("x-hvc-api-version") == "v1" &&
           response.headers.at("etag") == "\"directory-42\"" &&
           response.headers.at("retry-after") == "7";
}
} // namespace

// All test work is guarded below; the check does not model iostream's default no-throw state.
// NOLINTNEXTLINE(bugprone-exception-escape)
auto main() noexcept -> int
{
    try
    {
        if (!testResponseHeaders())
        {
            std::cerr << "WinHTTP response-header propagation regression\n";
            return 1;
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "WinHTTP response-header test failed: " << error.what() << '\n';
        return 1;
    }
    catch (...)
    {
        std::cerr << "WinHTTP response-header test failed with an unknown exception\n";
        return 1;
    }
}
