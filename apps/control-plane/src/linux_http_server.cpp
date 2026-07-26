#include "linux_http_server.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <netdb.h>
#include <optional>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <variant>
#include <vector>

namespace hvc::control_plane
{
namespace
{
constexpr std::size_t maximum_header_bytes{std::size_t{16U} * 1024U};
constexpr std::size_t maximum_body_bytes{std::size_t{64U} * 1024U};
volatile std::sig_atomic_t shutdown_requested{};

extern "C" void requestShutdown(int) noexcept
{
    shutdown_requested = 1;
}

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

    Socket(Socket&& other) noexcept : descriptor_(other.descriptor_)
    {
        other.descriptor_ = -1;
    }

    auto operator=(Socket&& other) noexcept -> Socket&
    {
        if (this != &other)
        {
            if (descriptor_ >= 0)
            {
                static_cast<void>(::close(descriptor_));
            }
            descriptor_ = other.descriptor_;
            other.descriptor_ = -1;
        }
        return *this;
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
        if (addresses_ != nullptr)
        {
            ::freeaddrinfo(addresses_);
        }
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

[[nodiscard]] auto lowerAscii(std::string_view value) -> std::string
{
    std::string lowered;
    lowered.reserve(value.size());
    for (const char character : value)
    {
        lowered.push_back(character >= 'A' && character <= 'Z'
                              ? static_cast<char>(character - 'A' + 'a')
                              : character);
    }
    return lowered;
}

[[nodiscard]] auto trim(std::string_view value) noexcept -> std::string_view
{
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
    {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
    {
        value.remove_suffix(1);
    }
    return value;
}

[[nodiscard]] auto statusReason(int status) noexcept -> std::string_view
{
    switch (status)
    {
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 409:
        return "Conflict";
    case 413:
        return "Content Too Large";
    case 429:
        return "Too Many Requests";
    case 500:
        return "Internal Server Error";
    case 502:
        return "Bad Gateway";
    case 503:
        return "Service Unavailable";
    default:
        return "Error";
    }
}

[[nodiscard]] auto protocolError(int status, std::string_view code) -> network::HttpResponse
{
    return network::HttpResponse{status,
                                 {{"cache-control", "no-store"},
                                  {"content-type", "application/json; charset=utf-8"},
                                  {"x-hvc-api-version", "v1"}},
                                 "{\"api_version\":\"v1\",\"error\":{\"code\":\"" +
                                     std::string{code} +
                                     "\",\"message\":\"The HTTP request is invalid.\"}}"};
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
            throw std::system_error{errno, std::generic_category(), "send"};
        }
        if (sent == 0)
        {
            throw std::runtime_error{"The peer closed while a response was being sent."};
        }
        bytes.remove_prefix(static_cast<std::size_t>(sent));
    }
}

void sendResponse(int descriptor, const network::HttpResponse& response)
{
    std::string header{"HTTP/1.1 " + std::to_string(response.status_code) + ' ' +
                       std::string{statusReason(response.status_code)} + "\r\n"};
    for (const auto& [name, value] : response.headers)
    {
        header.append(name);
        header.append(": ");
        header.append(value);
        header.append("\r\n");
    }
    header +=
        "content-length: " + std::to_string(response.body.size()) + "\r\nconnection: close\r\n\r\n";
    sendAll(descriptor, header);
    sendAll(descriptor, response.body);
}

void sendOverloadResponse(int descriptor)
{
    // The accept loop deliberately does not parse an overloaded request. Discarding the
    // unread receive side before closing prevents Linux from replacing the HTTP response
    // with a TCP reset.
    if (::shutdown(descriptor, SHUT_RD) != 0 && errno != ENOTCONN)
    {
        throw std::system_error{errno, std::generic_category(), "shutdown read"};
    }
    sendResponse(descriptor, protocolError(503, "server_overloaded"));
    if (::shutdown(descriptor, SHUT_WR) != 0 && errno != ENOTCONN)
    {
        throw std::system_error{errno, std::generic_category(), "shutdown write"};
    }
}

[[nodiscard]] auto readRequest(int descriptor)
    -> std::variant<network::HttpRequest, network::HttpResponse>
{
    std::string received;
    received.reserve(4096);
    std::array<char, 4096> buffer{};
    std::size_t header_end = std::string::npos;
    while (header_end == std::string::npos)
    {
        if (received.size() >= maximum_header_bytes)
        {
            return protocolError(413, "headers_too_large");
        }
        const auto count = ::recv(descriptor, buffer.data(), buffer.size(), 0);
        if (count < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return protocolError(400, "request_read_failed");
        }
        if (count == 0)
        {
            return protocolError(400, "incomplete_request");
        }
        received.append(buffer.data(), static_cast<std::size_t>(count));
        header_end = received.find("\r\n\r\n");
    }

    const auto request_line_end = received.find("\r\n");
    if (request_line_end == std::string::npos || request_line_end > header_end)
    {
        return protocolError(400, "invalid_request_line");
    }
    const std::string_view request_line{received.data(), request_line_end};
    const auto first_space = request_line.find(' ');
    const auto second_space = first_space == std::string_view::npos
                                  ? std::string_view::npos
                                  : request_line.find(' ', first_space + 1);
    if (first_space == std::string_view::npos || second_space == std::string_view::npos ||
        request_line.substr(second_space + 1) != "HTTP/1.1")
    {
        return protocolError(400, "invalid_request_line");
    }

    network::HttpRequest request;
    request.method = request_line.substr(0, first_space);
    request.target = request_line.substr(first_space + 1, second_space - first_space - 1);
    if (request.method.empty() || request.target.empty() || request.target.front() != '/')
    {
        return protocolError(400, "invalid_request_target");
    }

    std::size_t line_start = request_line_end + 2;
    while (line_start < header_end)
    {
        const auto line_end = received.find("\r\n", line_start);
        if (line_end == std::string::npos || line_end > header_end)
        {
            return protocolError(400, "invalid_header");
        }
        const std::string_view line{received.data() + line_start, line_end - line_start};
        const auto colon = line.find(':');
        if (colon == std::string_view::npos)
        {
            return protocolError(400, "invalid_header");
        }
        auto name = lowerAscii(trim(line.substr(0, colon)));
        auto value = std::string{trim(line.substr(colon + 1))};
        if (name.empty() || !request.headers.emplace(std::move(name), std::move(value)).second)
        {
            return protocolError(400, "duplicate_or_empty_header");
        }
        line_start = line_end + 2;
    }

    if (!request.header("transfer-encoding").empty())
    {
        return protocolError(400, "transfer_encoding_not_supported");
    }
    std::size_t content_length{};
    const auto content_length_value = request.header("content-length");
    if (!content_length_value.empty())
    {
        const auto conversion = std::from_chars(
            content_length_value.data(), content_length_value.data() + content_length_value.size(),
            content_length);
        if (conversion.ec != std::errc{} ||
            conversion.ptr != content_length_value.data() + content_length_value.size())
        {
            return protocolError(400, "invalid_content_length");
        }
    }
    if (content_length > maximum_body_bytes)
    {
        return protocolError(413, "body_too_large");
    }

    const std::size_t body_start = header_end + 4;
    const std::size_t total_size = body_start + content_length;
    while (received.size() < total_size)
    {
        const auto remaining = total_size - received.size();
        const auto count = ::recv(descriptor, buffer.data(), std::min(buffer.size(), remaining), 0);
        if (count < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return protocolError(400, "request_read_failed");
        }
        if (count == 0)
        {
            return protocolError(400, "incomplete_body");
        }
        received.append(buffer.data(), static_cast<std::size_t>(count));
    }
    request.body.assign(received.data() + body_start, content_length);
    return request;
}

[[nodiscard]] auto createListeningSocket(std::string_view bind_address, std::uint16_t port)
    -> Socket
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV;

    addrinfo* raw_addresses{};
    const auto service = std::to_string(port);
    const auto host = std::string{bind_address};
    const int lookup = ::getaddrinfo(host.c_str(), service.c_str(), &hints, &raw_addresses);
    if (lookup != 0)
    {
        throw std::runtime_error{"Cannot resolve listen address: " +
                                 std::string{::gai_strerror(lookup)}};
    }
    const AddressInfo addresses{raw_addresses};

    int last_error{};
    for (auto* address = addresses.get(); address != nullptr; address = address->ai_next)
    {
        Socket listener{::socket(address->ai_family, address->ai_socktype, address->ai_protocol)};
        if (listener.get() < 0)
        {
            last_error = errno;
            continue;
        }
        constexpr int enabled{1};
        if (::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) != 0)
        {
            last_error = errno;
            continue;
        }
        if (::bind(listener.get(), address->ai_addr, address->ai_addrlen) != 0 ||
            ::listen(listener.get(), SOMAXCONN) != 0)
        {
            last_error = errno;
            continue;
        }
        return listener;
    }
    throw std::system_error{last_error, std::generic_category(), "bind/listen"};
}

void setClientTimeouts(int descriptor)
{
    constexpr timeval timeout{5, 0};
    if (::setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
        ::setsockopt(descriptor, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0)
    {
        throw std::system_error{errno, std::generic_category(), "setsockopt timeout"};
    }
}

void processClient(Socket client, network::IHttpRequestHandler& handler) noexcept
{
    try
    {
        setClientTimeouts(client.get());
        auto request = readRequest(client.get());
        if (std::holds_alternative<network::HttpResponse>(request))
        {
            sendResponse(client.get(), std::get<network::HttpResponse>(request));
            return;
        }
        const auto response =
            handler.handle(std::get<network::HttpRequest>(request), application::Clock::now());
        sendResponse(client.get(), response);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr,
                     "{\"level\":\"error\",\"event\":\"http_request_failed\","
                     "\"message\":\"%s\"}\n",
                     error.what());
    }
}
} // namespace

auto runLinuxHttpServer(std::string_view bind_address, std::uint16_t port,
                        network::IHttpRequestHandler& handler, LinuxHttpServerOptions options)
    -> int
{
    if (options.worker_count == 0 || options.maximum_queued_connections == 0)
    {
        throw std::invalid_argument{"HTTP worker and queue limits must be positive."};
    }
    const auto listener = createListeningSocket(bind_address, port);
    std::signal(SIGINT, requestShutdown);
    std::signal(SIGTERM, requestShutdown);
    std::printf("{\"level\":\"info\",\"event\":\"http_listening\",\"address\":\"%.*s\","
                "\"port\":%u,\"workers\":%zu,\"queue_capacity\":%zu}\n",
                static_cast<int>(bind_address.size()), bind_address.data(),
                static_cast<unsigned int>(port), options.worker_count,
                options.maximum_queued_connections);

    std::mutex queue_mutex;
    std::condition_variable queue_changed;
    std::deque<Socket> queue;
    bool stopping{};
    std::vector<std::thread> workers;
    workers.reserve(options.worker_count);
    for (std::size_t worker_index = 0; worker_index < options.worker_count; ++worker_index)
    {
        workers.emplace_back([&] {
            while (true)
            {
                std::optional<Socket> client;
                {
                    std::unique_lock lock{queue_mutex};
                    queue_changed.wait(lock, [&] { return stopping || !queue.empty(); });
                    if (queue.empty())
                    {
                        return;
                    }
                    client.emplace(std::move(queue.front()));
                    queue.pop_front();
                }
                processClient(std::move(*client), handler);
            }
        });
    }

    int server_result{};
    while (shutdown_requested == 0)
    {
        pollfd readiness{listener.get(), POLLIN, 0};
        const int poll_result = ::poll(&readiness, 1, 500);
        if (poll_result < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            std::fprintf(stderr,
                         "{\"level\":\"error\",\"event\":\"http_poll_failed\","
                         "\"error\":%d}\n",
                         errno);
            server_result = 1;
            break;
        }
        if (poll_result == 0)
        {
            continue;
        }
        const int descriptor = ::accept(listener.get(), nullptr, nullptr);
        if (descriptor < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            std::fprintf(stderr,
                         "{\"level\":\"error\",\"event\":\"http_accept_failed\","
                         "\"error\":%d}\n",
                         errno);
            server_result = 1;
            break;
        }
        Socket client{descriptor};
        bool queued{};
        {
            std::scoped_lock lock{queue_mutex};
            if (queue.size() < options.maximum_queued_connections)
            {
                queue.push_back(std::move(client));
                queued = true;
            }
        }
        if (queued)
        {
            queue_changed.notify_one();
        }
        else
        {
            std::fputs("{\"level\":\"warning\",\"event\":\"http_queue_overloaded\"}\n", stderr);
            try
            {
                sendOverloadResponse(client.get());
            }
            catch (const std::exception&)
            {
                std::fputs("{\"level\":\"warning\",\"event\":\"http_overload_response_failed\"}\n",
                           stderr);
            }
        }
    }

    {
        std::scoped_lock lock{queue_mutex};
        stopping = true;
    }
    queue_changed.notify_all();
    for (auto& worker : workers)
    {
        worker.join();
    }
    std::fputs("{\"level\":\"info\",\"event\":\"http_stopped\"}\n", stdout);
    return server_result;
}
} // namespace hvc::control_plane
