#pragma once

#include <cstdint>
#include <hvc/network/control_plane_http.hpp>
#include <string_view>

namespace hvc::control_plane
{
[[nodiscard]] auto runLinuxHttpServer(std::string_view bind_address, std::uint16_t port,
                                      network::IHttpRequestHandler& handler) -> int;
}
