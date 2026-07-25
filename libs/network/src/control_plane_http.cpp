#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <hvc/network/control_plane_http.hpp>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>
#include <variant>

namespace hvc::network
{
namespace
{
struct JsonScalar final
{
    std::optional<std::string> string_value;
    std::optional<std::uint64_t> unsigned_value;
};

using JsonObject = std::map<std::string, JsonScalar, std::less<>>;

struct RequestSession final
{
    application::AuthenticatedSession session;
    domain::DeviceId device_id;
    domain::CorrelationId correlation_id;
};

[[nodiscard]] auto asciiLower(std::string_view value) -> std::string
{
    std::string lowered;
    lowered.reserve(value.size());
    for (const char character : value)
    {
        if (character >= 'A' && character <= 'Z')
        {
            lowered.push_back(static_cast<char>(character - 'A' + 'a'));
        }
        else
        {
            lowered.push_back(character);
        }
    }
    return lowered;
}

[[nodiscard]] auto escapeJson(std::string_view value) -> std::string
{
    constexpr char hex_digits[] = "0123456789abcdef";
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (const char raw_character : value)
    {
        const auto character = static_cast<unsigned char>(raw_character);
        switch (character)
        {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (character < 0x20)
            {
                escaped += "\\u00";
                escaped.push_back(hex_digits[character >> 4U]);
                escaped.push_back(hex_digits[character & 0x0FU]);
            }
            else
            {
                escaped.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    escaped.push_back('"');
    return escaped;
}

void skipWhitespace(std::string_view input, std::size_t& position) noexcept
{
    while (position < input.size())
    {
        const char character = input[position];
        if (character != ' ' && character != '\t' && character != '\r' && character != '\n')
        {
            break;
        }
        ++position;
    }
}

[[nodiscard]] auto parseJsonString(std::string_view input, std::size_t& position)
    -> std::optional<std::string>
{
    if (position >= input.size() || input[position] != '"')
    {
        return std::nullopt;
    }
    ++position;

    std::string value;
    while (position < input.size())
    {
        const char character = input[position++];
        if (character == '"')
        {
            return value;
        }
        if (character != '\\')
        {
            if (static_cast<unsigned char>(character) < 0x20U)
            {
                return std::nullopt;
            }
            value.push_back(character);
            continue;
        }

        if (position >= input.size())
        {
            return std::nullopt;
        }
        switch (input[position++])
        {
        case '"':
            value.push_back('"');
            break;
        case '\\':
            value.push_back('\\');
            break;
        case '/':
            value.push_back('/');
            break;
        case 'b':
            value.push_back('\b');
            break;
        case 'f':
            value.push_back('\f');
            break;
        case 'n':
            value.push_back('\n');
            break;
        case 'r':
            value.push_back('\r');
            break;
        case 't':
            value.push_back('\t');
            break;
        default:
            return std::nullopt;
        }
    }
    return std::nullopt;
}

[[nodiscard]] auto parseJsonUnsigned(std::string_view input, std::size_t& position)
    -> std::optional<std::uint64_t>
{
    const auto first = input.data() + position;
    auto last = first;
    while (last != input.data() + input.size() && *last >= '0' && *last <= '9')
    {
        ++last;
    }
    if (last == first)
    {
        return std::nullopt;
    }

    std::uint64_t value{};
    const auto conversion = std::from_chars(first, last, value);
    if (conversion.ec != std::errc{} || conversion.ptr != last)
    {
        return std::nullopt;
    }
    position += static_cast<std::size_t>(last - first);
    return value;
}

[[nodiscard]] auto parseFlatJsonObject(std::string_view input) -> std::optional<JsonObject>
{
    std::size_t position{};
    skipWhitespace(input, position);
    if (position >= input.size() || input[position++] != '{')
    {
        return std::nullopt;
    }

    JsonObject object;
    skipWhitespace(input, position);
    if (position < input.size() && input[position] == '}')
    {
        ++position;
        skipWhitespace(input, position);
        return position == input.size() ? std::optional<JsonObject>{std::move(object)}
                                        : std::nullopt;
    }

    while (position < input.size())
    {
        auto key = parseJsonString(input, position);
        if (!key)
        {
            return std::nullopt;
        }
        skipWhitespace(input, position);
        if (position >= input.size() || input[position++] != ':')
        {
            return std::nullopt;
        }
        skipWhitespace(input, position);

        JsonScalar scalar;
        if (position < input.size() && input[position] == '"')
        {
            scalar.string_value = parseJsonString(input, position);
            if (!scalar.string_value)
            {
                return std::nullopt;
            }
        }
        else
        {
            scalar.unsigned_value = parseJsonUnsigned(input, position);
            if (!scalar.unsigned_value)
            {
                return std::nullopt;
            }
        }

        if (!object.emplace(std::move(*key), std::move(scalar)).second)
        {
            return std::nullopt;
        }
        skipWhitespace(input, position);
        if (position >= input.size())
        {
            return std::nullopt;
        }
        if (input[position] == '}')
        {
            ++position;
            skipWhitespace(input, position);
            return position == input.size() ? std::optional<JsonObject>{std::move(object)}
                                            : std::nullopt;
        }
        if (input[position++] != ',')
        {
            return std::nullopt;
        }
        skipWhitespace(input, position);
    }
    return std::nullopt;
}

[[nodiscard]] auto hasOnlyFields(const JsonObject& object,
                                 std::initializer_list<std::string_view> allowed) -> bool
{
    const std::set<std::string_view> allowed_fields{allowed};
    return std::ranges::all_of(object, [&allowed_fields](const auto& field) {
        return allowed_fields.contains(field.first);
    });
}

[[nodiscard]] auto stringField(const JsonObject& object, std::string_view name)
    -> std::optional<std::string_view>
{
    const auto field = object.find(name);
    if (field == object.end() || !field->second.string_value)
    {
        return std::nullopt;
    }
    return *field->second.string_value;
}

[[nodiscard]] auto unsignedField(const JsonObject& object, std::string_view name)
    -> std::optional<std::uint64_t>
{
    const auto field = object.find(name);
    if (field == object.end())
    {
        return std::nullopt;
    }
    return field->second.unsigned_value;
}

[[nodiscard]] auto jsonResponse(int status_code, std::string body) -> HttpResponse
{
    return HttpResponse{status_code,
                        {{"cache-control", "no-store"},
                         {"content-type", "application/json; charset=utf-8"},
                         {"x-hvc-api-version", std::string{control_plane_api_version}}},
                        std::move(body)};
}

[[nodiscard]] auto errorResponse(int status_code, std::string_view code, std::string_view message)
    -> HttpResponse
{
    return jsonResponse(status_code,
                        "{\"api_version\":\"v1\",\"error\":{\"code\":" + escapeJson(code) +
                            ",\"message\":" + escapeJson(message) + "}}");
}

[[nodiscard]] auto authorizationValue(const HttpRequest& request, std::string_view scheme)
    -> std::optional<std::string_view>
{
    const auto authorization = request.header("authorization");
    if (!authorization.starts_with(scheme) || authorization.size() == scheme.size())
    {
        return std::nullopt;
    }
    return authorization.substr(scheme.size());
}

[[nodiscard]] auto unixMilliseconds(application::TimePoint value) -> std::int64_t
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(value.time_since_epoch()).count();
}

[[nodiscard]] auto scopeName(domain::VoiceScope scope) noexcept -> std::string_view
{
    switch (scope)
    {
    case domain::VoiceScope::team:
        return "team";
    case domain::VoiceScope::specialization:
        return "specialization";
    case domain::VoiceScope::group:
        return "group";
    }
    return "unknown";
}

[[nodiscard]] auto parseScope(std::string_view scope) -> std::optional<domain::VoiceScope>
{
    if (scope == "team")
    {
        return domain::VoiceScope::team;
    }
    if (scope == "specialization")
    {
        return domain::VoiceScope::specialization;
    }
    if (scope == "group")
    {
        return domain::VoiceScope::group;
    }
    return std::nullopt;
}

[[nodiscard]] auto authenticationError(application::SessionAuthenticationError error)
    -> HttpResponse
{
    switch (error)
    {
    case application::SessionAuthenticationError::invalid_credentials:
        return errorResponse(401, "invalid_credentials", "The credential was rejected.");
    case application::SessionAuthenticationError::device_not_allowed:
        return errorResponse(403, "device_not_allowed", "The device is not allowed.");
    case application::SessionAuthenticationError::account_disabled:
        return errorResponse(403, "account_disabled", "The account is disabled.");
    case application::SessionAuthenticationError::rate_limited:
        return errorResponse(429, "rate_limited", "Too many authentication attempts.");
    }
    return errorResponse(500, "internal_error", "The request could not be completed.");
}

[[nodiscard]] auto startErrorCode(application::StartTransmissionError error) noexcept
    -> std::string_view
{
    switch (error)
    {
    case application::StartTransmissionError::session_not_found:
        return "session_not_found";
    case application::StartTransmissionError::session_device_mismatch:
        return "session_device_mismatch";
    case application::StartTransmissionError::session_expired:
        return "session_expired";
    case application::StartTransmissionError::membership_unavailable:
        return "membership_unavailable";
    case application::StartTransmissionError::voice_not_connected:
        return "voice_not_connected";
    case application::StartTransmissionError::voice_no_active_membership:
        return "voice_no_active_membership";
    case application::StartTransmissionError::voice_scope_not_found:
        return "voice_scope_not_found";
    case application::StartTransmissionError::voice_scope_not_authorized:
        return "voice_scope_not_authorized";
    case application::StartTransmissionError::voice_transmit_muted:
        return "voice_transmit_muted";
    case application::StartTransmissionError::voice_membership_stale:
        return "voice_membership_stale";
    case application::StartTransmissionError::session_changed_during_start:
        return "session_changed_during_start";
    case application::StartTransmissionError::membership_changed_during_start:
        return "membership_changed_during_start";
    case application::StartTransmissionError::sender_already_transmitting:
        return "sender_already_transmitting";
    case application::StartTransmissionError::transmission_id_conflict:
        return "transmission_id_conflict";
    case application::StartTransmissionError::rate_limited:
        return "rate_limited";
    }
    return "internal_error";
}

[[nodiscard]] auto startErrorStatus(application::StartTransmissionError error) noexcept -> int
{
    switch (error)
    {
    case application::StartTransmissionError::session_not_found:
    case application::StartTransmissionError::session_expired:
        return 401;
    case application::StartTransmissionError::session_device_mismatch:
    case application::StartTransmissionError::voice_scope_not_authorized:
    case application::StartTransmissionError::voice_transmit_muted:
        return 403;
    case application::StartTransmissionError::rate_limited:
        return 429;
    case application::StartTransmissionError::membership_unavailable:
    case application::StartTransmissionError::voice_not_connected:
    case application::StartTransmissionError::voice_no_active_membership:
    case application::StartTransmissionError::voice_scope_not_found:
    case application::StartTransmissionError::voice_membership_stale:
    case application::StartTransmissionError::session_changed_during_start:
    case application::StartTransmissionError::membership_changed_during_start:
    case application::StartTransmissionError::sender_already_transmitting:
    case application::StartTransmissionError::transmission_id_conflict:
        return 409;
    }
    return 500;
}

[[nodiscard]] auto endErrorCode(application::EndTransmissionError error) noexcept
    -> std::string_view
{
    switch (error)
    {
    case application::EndTransmissionError::session_not_found:
        return "session_not_found";
    case application::EndTransmissionError::session_device_mismatch:
        return "session_device_mismatch";
    case application::EndTransmissionError::session_expired:
        return "session_expired";
    case application::EndTransmissionError::transmission_not_found:
        return "transmission_not_found";
    case application::EndTransmissionError::transmission_not_owned:
        return "transmission_not_owned";
    case application::EndTransmissionError::rate_limited:
        return "rate_limited";
    }
    return "internal_error";
}

[[nodiscard]] auto endErrorStatus(application::EndTransmissionError error) noexcept -> int
{
    switch (error)
    {
    case application::EndTransmissionError::session_not_found:
    case application::EndTransmissionError::session_expired:
        return 401;
    case application::EndTransmissionError::session_device_mismatch:
    case application::EndTransmissionError::transmission_not_owned:
        return 403;
    case application::EndTransmissionError::transmission_not_found:
        return 404;
    case application::EndTransmissionError::rate_limited:
        return 429;
    }
    return 500;
}

[[nodiscard]] auto moderationErrorCode(application::ModerateTransmissionError error) noexcept
    -> std::string_view
{
    switch (error)
    {
    case application::ModerateTransmissionError::session_not_found:
        return "session_not_found";
    case application::ModerateTransmissionError::session_device_mismatch:
        return "session_device_mismatch";
    case application::ModerateTransmissionError::session_expired:
        return "session_expired";
    case application::ModerateTransmissionError::not_authorized:
        return "not_authorized";
    case application::ModerateTransmissionError::transmission_not_found:
        return "transmission_not_found";
    }
    return "internal_error";
}

[[nodiscard]] auto moderationErrorStatus(application::ModerateTransmissionError error) noexcept
    -> int
{
    switch (error)
    {
    case application::ModerateTransmissionError::session_not_found:
    case application::ModerateTransmissionError::session_expired:
        return 401;
    case application::ModerateTransmissionError::session_device_mismatch:
    case application::ModerateTransmissionError::not_authorized:
        return 403;
    case application::ModerateTransmissionError::transmission_not_found:
        return 404;
    }
    return 500;
}

[[nodiscard]] auto authenticateRequestSession(const HttpRequest& request,
                                              application::TimePoint now,
                                              const application::ISessionRepository& sessions)
    -> std::variant<RequestSession, HttpResponse>
{
    const auto session_value = authorizationValue(request, "Session ");
    if (!session_value)
    {
        return errorResponse(401, "session_required", "Authorization must use the Session scheme.");
    }
    const auto device_value = request.header("x-hvc-device-id");
    const auto correlation_value = request.header("x-correlation-id");
    if (device_value.empty() || correlation_value.empty())
    {
        return errorResponse(400, "required_header_missing",
                             "X-HVC-Device-ID and X-Correlation-ID are required.");
    }

    const domain::SessionId session_id{std::string{*session_value}};
    const domain::DeviceId device_id{std::string{device_value}};
    const domain::CorrelationId correlation_id{std::string{correlation_value}};
    auto session = sessions.find(session_id);
    if (!session || !session->activeAt(now))
    {
        return errorResponse(401, "invalid_session", "The session is missing or expired.");
    }
    if (session->device_id != device_id)
    {
        return errorResponse(403, "session_device_mismatch",
                             "The session belongs to another device.");
    }
    return RequestSession{std::move(*session), device_id, correlation_id};
}

[[nodiscard]] auto sessionResponse(const application::AuthenticatedSession& session) -> HttpResponse
{
    return jsonResponse(
        201, "{\"api_version\":\"v1\",\"session_id\":" + escapeJson(session.session_id.value()) +
                 ",\"player_id\":" + escapeJson(session.player_id.value()) +
                 ",\"device_id\":" + escapeJson(session.device_id.value()) +
                 ",\"expires_at_unix_ms\":" + std::to_string(unixMilliseconds(session.expires_at)) +
                 '}');
}

[[nodiscard]] auto membershipResponse(
    const application::AuthenticatedSession& session,
    const application::IAuthoritativeMembershipProvider& memberships) -> HttpResponse
{
    const auto context = memberships.currentFor(session.player_id);
    if (!context)
    {
        return errorResponse(404, "membership_unavailable",
                             "No authoritative membership is available.");
    }
    const auto* membership = context->snapshot->find(session.player_id);
    if (membership == nullptr)
    {
        return errorResponse(409, "membership_inconsistent",
                             "The authoritative membership does not contain the session player.");
    }

    std::string roles{"["};
    for (std::size_t index = 0; index < membership->role_ids.size(); ++index)
    {
        if (index != 0)
        {
            roles.push_back(',');
        }
        roles += escapeJson(membership->role_ids[index].value());
    }
    roles.push_back(']');

    return jsonResponse(
        200, "{\"api_version\":\"v1\",\"membership_version\":" +
                 std::to_string(context->snapshot->version()) +
                 ",\"hierarchy_id\":" + escapeJson(context->snapshot->hierarchy().id().value()) +
                 ",\"player_id\":" + escapeJson(membership->player_id.value()) +
                 ",\"group_id\":" + escapeJson(membership->group_id.value()) +
                 ",\"specialization_id\":" + escapeJson(membership->specialization_id.value()) +
                 ",\"team_id\":" + escapeJson(membership->team_id.value()) + ",\"role_ids\":" +
                 roles + ",\"connected\":" + (membership->connected ? "true" : "false") +
                 ",\"can_receive_voice\":" + (membership->can_receive_voice ? "true" : "false") +
                 ",\"transmit_muted\":" + (membership->transmit_muted ? "true" : "false") + '}');
}

[[nodiscard]] auto startTransmissionResponse(const RequestSession& authenticated,
                                             const JsonObject& body,
                                             application::TransmissionApplicationService& service,
                                             application::TimePoint now) -> HttpResponse
{
    if (!hasOnlyFields(body, {"client_transmission_id", "scope", "membership_version"}))
    {
        return errorResponse(400, "unknown_field", "The request contains an unknown field.");
    }
    const auto client_id = stringField(body, "client_transmission_id");
    const auto scope_value = stringField(body, "scope");
    const auto membership_version = unsignedField(body, "membership_version");
    const auto scope = scope_value ? parseScope(*scope_value) : std::nullopt;
    if (!client_id || !scope || !membership_version)
    {
        return errorResponse(400, "invalid_body",
                             "client_transmission_id, scope and membership_version are required.");
    }

    const application::StartTransmissionCommand command{
        authenticated.session.session_id,
        authenticated.device_id,
        domain::ClientTransmissionId{std::string{*client_id}},
        *scope,
        *membership_version,
        authenticated.correlation_id};
    const auto result = service.start(command, now);
    if (!result.successful())
    {
        const auto error = *result.error;
        return errorResponse(startErrorStatus(error), startErrorCode(error),
                             "The transmission could not be started.");
    }

    const auto& transmission = *result.transmission;
    return jsonResponse(
        201, "{\"api_version\":\"v1\",\"transmission_id\":" +
                 escapeJson(transmission.authorization.transmission_id.value()) +
                 ",\"client_transmission_id\":" +
                 escapeJson(transmission.authorization.client_transmission_id.value()) +
                 ",\"scope\":" + escapeJson(scopeName(transmission.authorization.scope)) +
                 ",\"membership_version\":" +
                 std::to_string(transmission.authorization.membership_version) +
                 ",\"recipient_count\":" +
                 std::to_string(transmission.authorization.recipients.size()) + '}');
}

[[nodiscard]] auto endTransmissionResponse(const RequestSession& authenticated,
                                           std::string_view transmission_id,
                                           application::TransmissionApplicationService& service,
                                           application::TimePoint now) -> HttpResponse
{
    const application::EndTransmissionCommand command{
        authenticated.session.session_id, authenticated.device_id,
        domain::TransmissionId{std::string{transmission_id}}, authenticated.correlation_id};
    const auto result = service.end(command, now);
    if (!result.successful())
    {
        const auto error = *result.error;
        return errorResponse(endErrorStatus(error), endErrorCode(error),
                             "The transmission could not be ended.");
    }
    return jsonResponse(
        200, "{\"api_version\":\"v1\",\"transmission_id\":" + escapeJson(transmission_id) +
                 ",\"status\":\"ended\",\"stop_reason\":"
                 "\"push_to_talk_released\"}");
}

[[nodiscard]] auto moderateTransmissionResponse(
    const RequestSession& authenticated, std::string_view transmission_id,
    application::TransmissionApplicationService& service, application::TimePoint now)
    -> HttpResponse
{
    const application::ModerateTransmissionCommand command{
        authenticated.session.session_id, authenticated.device_id,
        domain::TransmissionId{std::string{transmission_id}}, authenticated.correlation_id};
    const auto result = service.interruptForModeration(command, now);
    if (!result.successful())
    {
        const auto error = *result.error;
        return errorResponse(moderationErrorStatus(error), moderationErrorCode(error),
                             "The transmission could not be interrupted.");
    }
    return jsonResponse(
        200, "{\"api_version\":\"v1\",\"transmission_id\":" + escapeJson(transmission_id) +
                 ",\"status\":\"ended\",\"stop_reason\":"
                 "\"moderation_interrupted\"}");
}
} // namespace

auto HttpRequest::header(std::string_view name) const -> std::string_view
{
    const auto value = headers.find(asciiLower(name));
    return value == headers.end() ? std::string_view{} : std::string_view{value->second};
}

ControlPlaneHttpAdapter::ControlPlaneHttpAdapter(
    application::ISessionAuthenticator& authenticator,
    application::IMutableSessionRepository& sessions,
    const application::IAuthoritativeMembershipProvider& memberships,
    application::TransmissionApplicationService& transmissions,
    application::IAdministrativeMembershipService* administration,
    const application::IAdministrativeMembershipAuthorizer* administration_authorizer) noexcept
    : authenticator_(authenticator), sessions_(sessions), memberships_(memberships),
      transmissions_(transmissions), administration_(administration),
      administration_authorizer_(administration_authorizer)
{
}

auto ControlPlaneHttpAdapter::handle(const HttpRequest& request, application::TimePoint now)
    -> HttpResponse
{
    try
    {
        if (request.target == "/api/v1/health" && request.method == "GET")
        {
            if (!request.body.empty())
            {
                return errorResponse(400, "body_not_allowed",
                                     "Health retrieval does not accept a request body.");
            }
            return jsonResponse(200, "{\"api_version\":\"v1\",\"status\":\"ready\"}");
        }

        if (request.target == "/api/v1/sessions" && request.method == "POST")
        {
            const auto credential = authorizationValue(request, "Bearer ");
            const auto device = request.header("x-hvc-device-id");
            const auto correlation = request.header("x-correlation-id");
            if (!credential)
            {
                return errorResponse(
                    401, "credential_required",
                    "Authorization must use the Bearer scheme at the session boundary.");
            }
            if (device.empty() || correlation.empty())
            {
                return errorResponse(400, "required_header_missing",
                                     "X-HVC-Device-ID and X-Correlation-ID are required.");
            }
            if (!request.body.empty())
            {
                return errorResponse(400, "body_not_allowed",
                                     "Session creation does not accept a request body.");
            }

            const application::AuthenticateSessionCommand command{
                std::string{*credential}, domain::DeviceId{std::string{device}},
                domain::CorrelationId{std::string{correlation}}};
            auto result = authenticator_.authenticate(command, now);
            if (!result.authenticated())
            {
                return authenticationError(*result.error);
            }
            if (result.session->device_id != command.device_id || !result.session->activeAt(now))
            {
                return errorResponse(
                    502, "invalid_authenticator_result",
                    "The authenticator returned an invalid or device-mismatched session.");
            }
            sessions_.upsert(*result.session);
            return sessionResponse(*result.session);
        }

        auto authenticated = authenticateRequestSession(request, now, sessions_);
        if (std::holds_alternative<HttpResponse>(authenticated))
        {
            return std::get<HttpResponse>(std::move(authenticated));
        }
        const auto& session = std::get<RequestSession>(authenticated);

        constexpr std::string_view membership_admin_prefix{"/api/v1/admin/memberships/"};
        if (request.target.starts_with(membership_admin_prefix))
        {
            const auto subject_value =
                std::string_view{request.target}.substr(membership_admin_prefix.size());
            if (subject_value.empty() || subject_value.find('/') != std::string_view::npos)
            {
                return errorResponse(400, "invalid_membership_path",
                                     "The administrative membership path is invalid.");
            }
            if (administration_ == nullptr || administration_authorizer_ == nullptr)
            {
                return errorResponse(403, "administration_unavailable",
                                     "Membership administration is not enabled.");
            }
            const domain::PlayerId subject{std::string{subject_value}};
            if (request.method == "GET" && request.body.empty())
            {
                if (!administration_authorizer_->canRead(session.session.player_id, subject))
                {
                    return errorResponse(403, "not_authorized",
                                         "The session cannot read administrative memberships.");
                }
                const application::AuthenticatedSession subject_session{session.session.session_id,
                                                                        subject, session.device_id,
                                                                        session.session.expires_at};
                return membershipResponse(subject_session, *administration_);
            }
            if (request.method == "DELETE" && request.body.empty())
            {
                if (!administration_authorizer_->canRemove(session.session.player_id, subject))
                {
                    return errorResponse(403, "not_authorized",
                                         "The session cannot remove administrative memberships.");
                }
                if (!administration_->currentFor(subject))
                {
                    return errorResponse(404, "membership_unavailable",
                                         "No authoritative membership is available.");
                }
                const auto interrupted =
                    administration_->removeMembership(subject, now, session.correlation_id);
                return jsonResponse(
                    200, "{\"api_version\":\"v1\",\"player_id\":" + escapeJson(subject.value()) +
                             ",\"status\":\"removed\",\"interrupted_transmission_count\":" +
                             std::to_string(interrupted.size()) + '}');
            }
            return errorResponse(404, "route_not_found", "No v1 route matches the request.");
        }

        if (request.target == "/api/v1/membership" && request.method == "GET")
        {
            if (!request.body.empty())
            {
                return errorResponse(400, "body_not_allowed",
                                     "Membership retrieval does not accept a request body.");
            }
            return membershipResponse(session.session, memberships_);
        }

        if (request.target == "/api/v1/transmissions" && request.method == "POST")
        {
            const auto body = parseFlatJsonObject(request.body);
            if (!body)
            {
                return errorResponse(400, "invalid_json",
                                     "The request body must be a flat JSON object.");
            }
            return startTransmissionResponse(session, *body, transmissions_, now);
        }

        constexpr std::string_view transmission_prefix{"/api/v1/transmissions/"};
        if (request.target.starts_with(transmission_prefix))
        {
            auto transmission_id =
                std::string_view{request.target}.substr(transmission_prefix.size());
            constexpr std::string_view interrupt_suffix{"/interrupt"};
            if (request.method == "POST" && transmission_id.ends_with(interrupt_suffix))
            {
                transmission_id.remove_suffix(interrupt_suffix.size());
                if (transmission_id.empty() ||
                    transmission_id.find('/') != std::string_view::npos || !request.body.empty())
                {
                    return errorResponse(400, "invalid_transmission_path",
                                         "The transmission path or body is invalid.");
                }
                return moderateTransmissionResponse(session, transmission_id, transmissions_, now);
            }
            if (request.method == "DELETE" && !transmission_id.empty() &&
                transmission_id.find('/') == std::string_view::npos && request.body.empty())
            {
                return endTransmissionResponse(session, transmission_id, transmissions_, now);
            }
        }

        return errorResponse(404, "route_not_found", "No v1 route matches the request.");
    }
    catch (const std::invalid_argument&)
    {
        return errorResponse(400, "invalid_identifier", "An identifier is empty or invalid.");
    }
    catch (const std::exception&)
    {
        return errorResponse(500, "internal_error", "The request could not be completed.");
    }
}
} // namespace hvc::network
