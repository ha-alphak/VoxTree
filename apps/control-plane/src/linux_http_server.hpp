#pragma once

#include <cstddef>
#include <cstdint>
#include <hvc/network/control_plane_http.hpp>
#include <string_view>

/// Assemble and run the Linux control-plane executable.
namespace hvc::control_plane
{
/// Bound HTTP concurrency and queued work.
struct LinuxHttpServerOptions final
{
    /// Fixed number of request workers.
    std::size_t worker_count{8};
    /// Maximum accepted connections waiting for a worker.
    std::size_t maximum_queued_connections{64};
};

/**
 * Run the blocking Linux HTTP server loop.
 *
 * @param bind_address IPv4 address on which to listen.
 * @param port TCP port on which to listen.
 * @param handler Application request handler that remains alive for the loop.
 * @param options Bounded worker and overload configuration.
 * @returns Zero after an orderly shutdown, otherwise a process error code.
 */
[[nodiscard]] auto runLinuxHttpServer(std::string_view bind_address, std::uint16_t port,
                                      network::IHttpRequestHandler& handler,
                                      LinuxHttpServerOptions options = {}) -> int;
} // namespace hvc::control_plane
