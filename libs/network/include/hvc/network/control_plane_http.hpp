#pragma once

#include <hvc/application/control_plane.hpp>
#include <hvc/application/directory.hpp>
#include <map>
#include <string>
#include <string_view>

/**
 * Expose the application control plane through versioned network contracts.
 */
namespace hvc::network
{
/// Semantic version segment of the current control-plane HTTP contract.
inline constexpr std::string_view control_plane_api_version{"v1"};
/// Common path prefix of all current control-plane HTTP endpoints.
inline constexpr std::string_view control_plane_api_prefix{"/api/v1"};

/// Represent one decoded HTTP request at the application adapter boundary.
struct HttpRequest final
{
    /// Uppercase HTTP method.
    std::string method;
    /// Origin-form request target including an optional query.
    std::string target;
    /// Case-insensitively interpreted request headers.
    std::map<std::string, std::string, std::less<>> headers;
    /// Request body encoded according to the content-type header.
    std::string body;

    /**
     * Find a request header without case sensitivity.
     *
     * @param name Header field name.
     * @returns A view of the header value, or an empty view when absent.
     */
    [[nodiscard]] auto header(std::string_view name) const -> std::string_view;
};

/// Represent one complete HTTP response from an application adapter.
struct HttpResponse final
{
    /// HTTP status code.
    int status_code;
    /// Response headers.
    std::map<std::string, std::string, std::less<>> headers;
    /// Response body.
    std::string body;
};

/// Handle a decoded HTTP request at an authoritative point in time.
class IHttpRequestHandler
{
  public:
    /// Destroy the request-handler interface.
    virtual ~IHttpRequestHandler() = default;

    /**
     * Handle one HTTP request.
     *
     * @param request Decoded HTTP request.
     * @param now Authoritative server time for authentication and lifecycle
     *     decisions.
     * @returns Complete HTTP response.
     */
    [[nodiscard]] virtual auto handle(const HttpRequest& request, application::TimePoint now)
        -> HttpResponse = 0;
};

/**
 * Map the versioned JSON/HTTP contract to application services.
 *
 * The adapter owns no services. Optional administrative and voice-grant
 * endpoints are exposed only when all collaborators required by that endpoint
 * are supplied.
 */
class ControlPlaneHttpAdapter final : public IHttpRequestHandler
{
  public:
    /**
     * Construct an HTTP adapter.
     *
     * @param authenticator Session-authentication service.
     * @param sessions Mutable session repository.
     * @param memberships Authoritative membership provider.
     * @param transmissions Transmission lifecycle service.
     * @param administration Optional administrative membership service.
     * @param administration_authorizer Optional administrative authorizer.
     * @param voice_grants Optional voice-grant authorization service.
     * @param voice_grant_issuer Optional signed room-grant issuer.
     * @param voice_server_url Voice service URL included with issued grants.
     * @param directory Optional privacy-limited directory application service.
     * @throws std::invalid_argument Thrown when optional collaborators are
     *     supplied in an inconsistent combination.
     */
    ControlPlaneHttpAdapter(
        application::ISessionAuthenticator& authenticator,
        application::IMutableSessionRepository& sessions,
        const application::IAuthoritativeMembershipProvider& memberships,
        application::TransmissionApplicationService& transmissions,
        application::IAdministrativeMembershipService* administration = nullptr,
        const application::IAdministrativeMembershipAuthorizer* administration_authorizer = nullptr,
        const application::VoiceGrantAuthorizationService* voice_grants = nullptr,
        const application::IVoiceGrantIssuer* voice_grant_issuer = nullptr,
        std::string voice_server_url = {},
        application::DirectoryApplicationService* directory = nullptr);

    /// @copydoc IHttpRequestHandler::handle
    [[nodiscard]] auto handle(const HttpRequest& request, application::TimePoint now)
        -> HttpResponse override;

  private:
    application::ISessionAuthenticator& authenticator_;
    application::IMutableSessionRepository& sessions_;
    const application::IAuthoritativeMembershipProvider& memberships_;
    application::TransmissionApplicationService& transmissions_;
    application::IAdministrativeMembershipService* administration_;
    const application::IAdministrativeMembershipAuthorizer* administration_authorizer_;
    const application::VoiceGrantAuthorizationService* voice_grants_;
    const application::IVoiceGrantIssuer* voice_grant_issuer_;
    std::string voice_server_url_;
    application::DirectoryApplicationService* directory_;
};
} // namespace hvc::network
