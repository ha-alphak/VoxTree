#pragma once

#include <hvc/application/control_plane.hpp>
#include <map>
#include <string>
#include <string_view>

namespace hvc::network
{
inline constexpr std::string_view control_plane_api_version{"v1"};
inline constexpr std::string_view control_plane_api_prefix{"/api/v1"};

struct HttpRequest final
{
    std::string method;
    std::string target;
    std::map<std::string, std::string, std::less<>> headers;
    std::string body;

    [[nodiscard]] auto header(std::string_view name) const -> std::string_view;
};

struct HttpResponse final
{
    int status_code;
    std::map<std::string, std::string, std::less<>> headers;
    std::string body;
};

class IHttpRequestHandler
{
  public:
    virtual ~IHttpRequestHandler() = default;

    [[nodiscard]] virtual auto handle(const HttpRequest& request, application::TimePoint now)
        -> HttpResponse = 0;
};

class ControlPlaneHttpAdapter final : public IHttpRequestHandler
{
  public:
    ControlPlaneHttpAdapter(application::ISessionAuthenticator& authenticator,
                            application::IMutableSessionRepository& sessions,
                            const application::IAuthoritativeMembershipProvider& memberships,
                            application::TransmissionApplicationService& transmissions,
                            application::IAdministrativeMembershipService* administration = nullptr,
                            const application::IAdministrativeMembershipAuthorizer*
                                administration_authorizer = nullptr) noexcept;

    [[nodiscard]] auto handle(const HttpRequest& request, application::TimePoint now)
        -> HttpResponse override;

  private:
    application::ISessionAuthenticator& authenticator_;
    application::IMutableSessionRepository& sessions_;
    const application::IAuthoritativeMembershipProvider& memberships_;
    application::TransmissionApplicationService& transmissions_;
    application::IAdministrativeMembershipService* administration_;
    const application::IAdministrativeMembershipAuthorizer* administration_authorizer_;
};
} // namespace hvc::network
