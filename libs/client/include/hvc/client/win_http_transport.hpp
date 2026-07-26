#pragma once

#include <chrono>
#include <hvc/client/control_plane_client.hpp>
#include <memory>
#include <string>

namespace hvc::client
{
/**
 * Send synchronous HTTPS control-plane requests with Windows WinHTTP.
 *
 * The transport owns its WinHTTP handles and applies one timeout to connection,
 * send, and receive operations. Instances are non-copyable and non-movable.
 */
class WinHttpTransport final : public IClientHttpTransport
{
  public:
    /**
     * Construct a transport for one HTTPS server origin.
     *
     * @param base_url Absolute `https://` origin without a query or fragment.
     * @param request_timeout Positive timeout applied to WinHTTP operations.
     * @throws std::invalid_argument Thrown when the URL or timeout is invalid.
     * @throws std::runtime_error Thrown when WinHTTP initialization fails.
     * @exceptsafe Strong exception guarantee.
     */
    explicit WinHttpTransport(std::string base_url,
                              std::chrono::milliseconds request_timeout = std::chrono::seconds{10});
    /// Release all WinHTTP resources.
    ~WinHttpTransport() override;

    /// Copy construction is disabled.
    WinHttpTransport(const WinHttpTransport&) = delete;
    /// Copy assignment is disabled.
    auto operator=(const WinHttpTransport&) -> WinHttpTransport& = delete;
    /// Move construction is disabled.
    WinHttpTransport(WinHttpTransport&&) = delete;
    /// Move assignment is disabled.
    auto operator=(WinHttpTransport&&) -> WinHttpTransport& = delete;

    /// @copydoc IClientHttpTransport::send
    [[nodiscard]] auto send(const ClientHttpRequest& request) -> ClientHttpResponse override;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace hvc::client
