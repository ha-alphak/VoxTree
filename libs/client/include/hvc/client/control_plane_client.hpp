#pragma once

#include <chrono>
#include <cstdint>
#include <hvc/client/voice_transport.hpp>
#include <hvc/domain/id.hpp>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hvc::client
{
/// Describe an HTTP request at the platform-independent client boundary.
struct ClientHttpRequest final
{
    /// Uppercase HTTP method.
    std::string method;
    /// Origin-form request target beginning with `/`.
    std::string target;
    /// Case-insensitively interpreted request headers.
    std::map<std::string, std::string, std::less<>> headers;
    /// Request body encoded according to the content-type header.
    std::string body;
};

/// Describe an HTTP response or a transport-level failure.
struct ClientHttpResponse final
{
    /// HTTP status code, or zero when no response was received.
    int status_code{0};
    /// Response headers returned by the server.
    std::map<std::string, std::string, std::less<>> headers;
    /// Response body returned by the server.
    std::string body;
    /// Transport diagnostic, empty when an HTTP response was received.
    std::string transport_error;
};

/// Send platform-independent requests through a concrete HTTP implementation.
class IClientHttpTransport
{
  public:
    /// Destroy the HTTP transport interface.
    virtual ~IClientHttpTransport() = default;

    /**
     * Send one complete HTTP request synchronously.
     *
     * @param request Request method, target, headers, and body.
     * @returns Either the received response or a transport diagnostic.
     */
    [[nodiscard]] virtual auto send(const ClientHttpRequest& request) -> ClientHttpResponse = 0;
};

/// Generate unique client-side correlation and transmission identifiers.
class IClientIdentifierGenerator
{
  public:
    /// Destroy the identifier-generator interface.
    virtual ~IClientIdentifierGenerator() = default;

    /**
     * Generate the next correlation identifier.
     *
     * @returns An identifier unique within the operational client context.
     */
    [[nodiscard]] virtual auto nextCorrelationId() -> domain::CorrelationId = 0;
    /**
     * Generate the next client transmission identifier.
     *
     * @returns An identifier unique within the operational client context.
     */
    [[nodiscard]] virtual auto nextTransmissionId() -> domain::ClientTransmissionId = 0;
};

/// Classify failures exposed by the typed control-plane client.
enum class ControlPlaneErrorKind : std::uint8_t
{
    /// Local session state does not allow the operation.
    invalid_state,
    /// A caller-supplied argument is invalid.
    invalid_argument,
    /// No valid HTTP response was received.
    transport,
    /// The server response could not be validated or decoded.
    invalid_response,
    /// The server returned a structured non-success response.
    server
};

/// Describe a typed control-plane client failure.
struct ControlPlaneError final
{
    /// Broad failure classification.
    ControlPlaneErrorKind kind{ControlPlaneErrorKind::invalid_response};
    /// HTTP status code, or zero for local and transport failures.
    int status_code{0};
    /// Stable machine-readable error code.
    std::string code;
    /// Human-readable diagnostic message.
    std::string message;
};

/**
 * Hold either a decoded control-plane value or a typed error.
 *
 * @tparam Value Successful response representation.
 */
template <typename Value> struct ControlPlaneResult final
{
    /**
     * Create a successful result.
     *
     * @param result Decoded response value.
     * @returns A result that evaluates to `true`.
     */
    [[nodiscard]] static auto success(Value result) -> ControlPlaneResult
    {
        return ControlPlaneResult{std::move(result), std::nullopt};
    }

    /**
     * Create a failed result.
     *
     * @param result_error Failure details.
     * @returns A result that evaluates to `false`.
     */
    [[nodiscard]] static auto failure(ControlPlaneError result_error) -> ControlPlaneResult
    {
        return ControlPlaneResult{std::nullopt, std::move(result_error)};
    }

    /**
     * Return whether a successful value is present.
     *
     * @returns `true` after success.
     */
    [[nodiscard]] explicit operator bool() const noexcept
    {
        return value.has_value();
    }

    /// Decoded response value, absent after failure.
    std::optional<Value> value;
    /// Failure details, absent after success.
    std::optional<ControlPlaneError> error;
};

/// Represent an authenticated server session.
struct ControlPlaneSession final
{
    /// Server-assigned session identifier.
    domain::SessionId session_id;
    /// Authenticated participant identifier.
    domain::PlayerId player_id;
    /// Device bound to the session.
    domain::DeviceId device_id;
    /// Absolute session expiration time.
    std::chrono::system_clock::time_point expires_at;
};

/// Represent the authenticated participant's authoritative membership.
struct MembershipView final
{
    /// Monotonically increasing membership version.
    std::uint64_t version{0};
    /// Hierarchy definition referenced by the membership.
    domain::HierarchyId hierarchy_id;
    /// Authenticated participant.
    domain::PlayerId player_id;
    /// Participant's group.
    domain::GroupId group_id;
    /// Participant's specialization.
    domain::SpecializationId specialization_id;
    /// Participant's team.
    domain::TeamId team_id;
    /// Roles used to derive permissions.
    std::vector<domain::RoleId> role_ids;
    /// Whether the server considers the participant voice-connected.
    bool connected{false};
    /// Whether the participant may receive voice.
    bool can_receive_voice{false};
    /// Whether the participant is prohibited from transmitting.
    bool transmit_muted{false};
};

/// Hold short-lived voice-room grants for one membership version.
struct VoiceGrantSet final
{
    /// Membership version on which the grants are based.
    std::uint64_t membership_version{0};
    /// Absolute grant expiration time.
    std::chrono::system_clock::time_point expires_at;
    /// Room grants independently scoped by hierarchy level.
    std::vector<VoiceRoomGrant> room_grants;
};

/// Represent a server-authorized active transmission.
struct StartedTransmission final
{
    /// Server-assigned transmission identifier.
    domain::TransmissionId transmission_id;
    /// Client identifier supplied with the start request.
    domain::ClientTransmissionId client_transmission_id;
    /// Authorized hierarchy scope.
    domain::VoiceScope scope{domain::VoiceScope::team};
    /// Membership version used for authorization.
    std::uint64_t membership_version{0};
    /// Number of server-resolved recipients.
    std::size_t recipient_count{0};
};

/// Represent server confirmation that a transmission ended.
struct EndedTransmissionView final
{
    /// Server-assigned transmission identifier.
    domain::TransmissionId transmission_id;
    /// Stable textual stop reason returned by the protocol.
    std::string stop_reason;
};

/**
 * Provide typed access to the versioned HVC control-plane HTTP contract.
 *
 * The client owns no transport or identifier generator. A successful session
 * is cached and attached to subsequent authenticated requests.
 */
class ControlPlaneClient final
{
  public:
    /**
     * Construct a control-plane client for one device.
     *
     * @param transport HTTP transport that must outlive this client.
     * @param identifiers Identifier generator that must outlive this client.
     * @param device_id Device bound to newly created sessions.
     */
    ControlPlaneClient(IClientHttpTransport& transport, IClientIdentifierGenerator& identifiers,
                       domain::DeviceId device_id);

    /**
     * Authenticate and cache a new session.
     *
     * @param external_credential Credential accepted by the server authenticator.
     * @returns The decoded session or a typed failure.
     */
    [[nodiscard]] auto createSession(std::string_view external_credential)
        -> ControlPlaneResult<ControlPlaneSession>;
    /**
     * Fetch the authenticated participant's membership.
     *
     * @returns The authoritative membership or a typed failure.
     */
    [[nodiscard]] auto membership() -> ControlPlaneResult<MembershipView>;
    /**
     * Fetch voice-room grants for the current membership.
     *
     * @returns Short-lived room grants or a typed failure.
     */
    [[nodiscard]] auto voiceGrants() -> ControlPlaneResult<VoiceGrantSet>;
    /**
     * Request server authorization for a transmission.
     *
     * @param scope Requested hierarchy scope.
     * @param membership_version Membership version currently held by the client.
     * @returns The authorized transmission or a typed failure.
     */
    [[nodiscard]] auto startTransmission(domain::VoiceScope scope, std::uint64_t membership_version)
        -> ControlPlaneResult<StartedTransmission>;
    /**
     * End a server-authorized transmission.
     *
     * @param transmission_id Active server transmission identifier.
     * @returns The confirmed stop reason or a typed failure.
     */
    [[nodiscard]] auto endTransmission(const domain::TransmissionId& transmission_id)
        -> ControlPlaneResult<EndedTransmissionView>;

    /**
     * Return a copy of the cached authenticated session.
     *
     * @returns Cached session, or no value before authentication or after
     *     `clearSession()`.
     */
    [[nodiscard]] auto session() const -> std::optional<ControlPlaneSession>;
    /// Clear the cached session without sending a server request.
    void clearSession() noexcept;

  private:
    [[nodiscard]] auto sendSessionRequest(std::string method, std::string target,
                                          std::string body = {}) -> ClientHttpResponse;

    IClientHttpTransport& transport_;
    IClientIdentifierGenerator& identifiers_;
    domain::DeviceId device_id_;
    std::optional<ControlPlaneSession> session_;
};
} // namespace hvc::client
