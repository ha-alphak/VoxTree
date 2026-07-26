#pragma once

#include <cstdint>
#include <hvc/network/control_plane_http.hpp>
#include <string_view>

/// Assemble and run the Linux control-plane executable.
namespace hvc::control_plane
{
/**
 * Run the blocking Linux HTTP server loop.
 *
 * @param bind_address IPv4 address on which to listen.
 * @param port TCP port on which to listen.
 * @param handler Application request handler that remains alive for the loop.
 * @returns Zero after an orderly shutdown, otherwise a process error code.
 */
[[nodiscard]] auto runLinuxHttpServer(std::string_view bind_address, std::uint16_t port,
                                      network::IHttpRequestHandler& handler) -> int;
} // namespace hvc::control_plane
