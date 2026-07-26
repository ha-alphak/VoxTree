#include "linux_http_server.hpp"

#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <hvc/network/control_plane_http.hpp>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace
{
using namespace std::chrono_literals;
namespace control_plane = hvc::control_plane;
namespace network = hvc::network;

class Socket final
{
  public:
    explicit Socket(int descriptor = -1) noexcept : descriptor_(descriptor)
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
    Socket(Socket&&) = delete;
    auto operator=(Socket&&) -> Socket& = delete;

    [[nodiscard]] auto get() const noexcept -> int
    {
        return descriptor_;
    }

    void close() noexcept
    {
        if (descriptor_ >= 0)
        {
            static_cast<void>(::close(descriptor_));
            descriptor_ = -1;
        }
    }

  private:
    int descriptor_;
};

class Handler final : public network::IHttpRequestHandler
{
  public:
    [[nodiscard]] auto handle(const network::HttpRequest&, hvc::application::TimePoint)
        -> network::HttpResponse override
    {
        return network::HttpResponse{
            200, {{"content-type", "application/json"}}, R"({"status":"ready"})"};
    }
};

[[nodiscard]] auto reservePort() -> std::uint16_t
{
    Socket socket{::socket(AF_INET, SOCK_STREAM, 0)};
    if (socket.get() < 0)
    {
        return 0;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(socket.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
    {
        return 0;
    }

    socklen_t address_size{sizeof(address)};
    if (::getsockname(socket.get(), reinterpret_cast<sockaddr*>(&address), &address_size) != 0)
    {
        return 0;
    }
    return ntohs(address.sin_port);
}

[[nodiscard]] auto connectTo(std::uint16_t port) -> int
{
    const int descriptor = ::socket(AF_INET, SOCK_STREAM, 0);
    if (descriptor < 0)
    {
        return -1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (::connect(descriptor, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
    {
        static_cast<void>(::close(descriptor));
        return -1;
    }
    return descriptor;
}

[[nodiscard]] auto sendBytes(int descriptor, std::string_view bytes) -> bool
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
            return false;
        }
        bytes.remove_prefix(static_cast<std::size_t>(sent));
    }
    return true;
}

[[nodiscard]] auto waitUntilListening(std::uint16_t port) -> bool
{
    constexpr std::string_view request{
        "GET /api/v1/health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"};
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        Socket socket{connectTo(port)};
        if (socket.get() >= 0 && sendBytes(socket.get(), request))
        {
            std::array<char, 512> response{};
            if (::recv(socket.get(), response.data(), response.size(), 0) > 0)
            {
                return true;
            }
        }
        std::this_thread::sleep_for(20ms);
    }
    return false;
}

[[nodiscard]] auto readResponse(int descriptor) -> std::string
{
    std::string response;
    std::array<char, 1024> buffer{};
    while (true)
    {
        const auto received = ::recv(descriptor, buffer.data(), buffer.size(), 0);
        if (received > 0)
        {
            response.append(buffer.data(), static_cast<std::size_t>(received));
            continue;
        }
        if (received == 0)
        {
            return response;
        }
        if (errno != EINTR)
        {
            return {};
        }
    }
}

[[nodiscard]] auto stopServer(pid_t process) -> bool
{
    if (::kill(process, SIGTERM) != 0)
    {
        return false;
    }
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        int status{};
        const auto waited = ::waitpid(process, &status, WNOHANG);
        if (waited == process)
        {
            return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        }
        if (waited < 0)
        {
            return false;
        }
        std::this_thread::sleep_for(20ms);
    }
    static_cast<void>(::kill(process, SIGKILL));
    static_cast<void>(::waitpid(process, nullptr, 0));
    return false;
}
} // namespace

auto main() -> int
{
    const auto port = reservePort();
    if (port == 0)
    {
        std::fputs("Could not reserve a loopback test port.\n", stderr);
        return 1;
    }

    const pid_t process = ::fork();
    if (process < 0)
    {
        std::fprintf(stderr, "fork failed: %s\n", std::strerror(errno));
        return 1;
    }
    if (process == 0)
    {
        Handler handler;
        const int result = control_plane::runLinuxHttpServer(
            "127.0.0.1", port, handler,
            control_plane::LinuxHttpServerOptions{.worker_count = 1,
                                                  .maximum_queued_connections = 1});
        ::_exit(result);
    }

    if (!waitUntilListening(port))
    {
        static_cast<void>(::kill(process, SIGKILL));
        static_cast<void>(::waitpid(process, nullptr, 0));
        std::fputs("Linux HTTP server did not become ready.\n", stderr);
        return 1;
    }

    Socket worker{connectTo(port)};
    if (worker.get() < 0 ||
        !sendBytes(worker.get(), "GET /api/v1/health HTTP/1.1\r\nHost: localhost\r\n"))
    {
        worker.close();
        static_cast<void>(stopServer(process));
        std::fputs("Could not occupy the HTTP worker.\n", stderr);
        return 1;
    }
    std::this_thread::sleep_for(100ms);

    Socket queued{connectTo(port)};
    if (queued.get() < 0 ||
        !sendBytes(queued.get(), "GET /api/v1/health HTTP/1.1\r\nHost: localhost\r\n"))
    {
        worker.close();
        queued.close();
        static_cast<void>(stopServer(process));
        std::fputs("Could not occupy the HTTP queue.\n", stderr);
        return 1;
    }
    std::this_thread::sleep_for(100ms);

    Socket overloaded{connectTo(port)};
    constexpr std::string_view request{
        "GET /api/v1/health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"};
    if (overloaded.get() < 0 || !sendBytes(overloaded.get(), request))
    {
        worker.close();
        queued.close();
        overloaded.close();
        static_cast<void>(stopServer(process));
        std::fputs("Could not submit the overloaded request.\n", stderr);
        return 1;
    }

    const auto response = readResponse(overloaded.get());
    if (response.find("HTTP/1.1 503 Service Unavailable\r\n") == std::string::npos ||
        response.find("\"code\":\"server_overloaded\"") == std::string::npos)
    {
        worker.close();
        queued.close();
        overloaded.close();
        static_cast<void>(stopServer(process));
        std::fputs("The overloaded request did not receive a complete HTTP 503 response.\n",
                   stderr);
        return 1;
    }

    worker.close();
    queued.close();
    overloaded.close();
    if (!stopServer(process))
    {
        std::fputs("The Linux HTTP server did not stop gracefully.\n", stderr);
        return 1;
    }
    return 0;
}
