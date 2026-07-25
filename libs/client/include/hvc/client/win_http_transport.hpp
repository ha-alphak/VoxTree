#pragma once

#include <chrono>
#include <hvc/client/control_plane_client.hpp>
#include <memory>
#include <string>

namespace hvc::client
{
class WinHttpTransport final : public IClientHttpTransport
{
  public:
    explicit WinHttpTransport(std::string base_url,
                              std::chrono::milliseconds request_timeout = std::chrono::seconds{10});
    ~WinHttpTransport() override;

    WinHttpTransport(const WinHttpTransport&) = delete;
    auto operator=(const WinHttpTransport&) -> WinHttpTransport& = delete;
    WinHttpTransport(WinHttpTransport&&) = delete;
    auto operator=(WinHttpTransport&&) -> WinHttpTransport& = delete;

    [[nodiscard]] auto send(const ClientHttpRequest& request) -> ClientHttpResponse override;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace hvc::client
