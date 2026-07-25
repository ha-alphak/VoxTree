#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <hvc/client/control_plane_client.hpp>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace hvc::client
{
namespace
{
enum class JsonType : std::uint8_t
{
    string,
    unsigned_integer,
    boolean,
    object,
    array
};

struct JsonValue final
{
    JsonValue() = default;
    explicit JsonValue(JsonType value_type) : type(value_type)
    {
    }
    JsonValue(const JsonValue&) = delete;
    auto operator=(const JsonValue&) -> JsonValue& = delete;
    JsonValue(JsonValue&&) = default; // NOLINT(bugprone-exception-escape)
    auto operator=(JsonValue&&) -> JsonValue& = default;
    ~JsonValue() = default;

    JsonType type{JsonType::object};
    std::string string_value;
    std::uint64_t unsigned_value{0};
    bool boolean_value{false};
    std::map<std::string, JsonValue, std::less<>> object_value;
    std::vector<JsonValue> array_value;
};

class JsonParser final
{
  public:
    explicit JsonParser(std::string_view input) : input_(input)
    {
    }

    [[nodiscard]] auto parse() -> std::optional<JsonValue>
    {
        skipWhitespace();
        auto value = parseValue();
        skipWhitespace();
        if (!value || offset_ != input_.size())
        {
            return std::nullopt;
        }
        return value;
    }

  private:
    void skipWhitespace() noexcept
    {
        while (offset_ < input_.size() &&
               std::isspace(static_cast<unsigned char>(input_[offset_])) != 0)
        {
            ++offset_;
        }
    }

    [[nodiscard]] auto consume(char expected) noexcept -> bool
    {
        if (offset_ >= input_.size() || input_[offset_] != expected)
        {
            return false;
        }
        ++offset_;
        return true;
    }

    [[nodiscard]] auto parseValue() -> std::optional<JsonValue>
    {
        skipWhitespace();
        if (offset_ >= input_.size())
        {
            return std::nullopt;
        }
        if (input_[offset_] == '"')
        {
            auto value = parseString();
            if (!value)
            {
                return std::nullopt;
            }
            JsonValue result{JsonType::string};
            result.string_value = std::move(*value);
            return result;
        }
        if (input_[offset_] == '{')
        {
            return parseObject();
        }
        if (input_[offset_] == '[')
        {
            return parseArray();
        }
        if (input_.substr(offset_).starts_with("true"))
        {
            offset_ += 4;
            JsonValue result{JsonType::boolean};
            result.boolean_value = true;
            return result;
        }
        if (input_.substr(offset_).starts_with("false"))
        {
            offset_ += 5;
            return JsonValue{JsonType::boolean};
        }
        return parseUnsigned();
    }

    [[nodiscard]] auto parseObject() -> std::optional<JsonValue>
    {
        if (!consume('{'))
        {
            return std::nullopt;
        }
        JsonValue result{JsonType::object};
        skipWhitespace();
        if (consume('}'))
        {
            return result;
        }
        while (true)
        {
            auto name = parseString();
            skipWhitespace();
            if (!name || !consume(':'))
            {
                return std::nullopt;
            }
            auto value = parseValue();
            if (!value || !result.object_value.emplace(std::move(*name), std::move(*value)).second)
            {
                return std::nullopt;
            }
            skipWhitespace();
            if (consume('}'))
            {
                return result;
            }
            if (!consume(','))
            {
                return std::nullopt;
            }
            skipWhitespace();
        }
    }

    [[nodiscard]] auto parseArray() -> std::optional<JsonValue>
    {
        if (!consume('['))
        {
            return std::nullopt;
        }
        JsonValue result{JsonType::array};
        skipWhitespace();
        if (consume(']'))
        {
            return result;
        }
        while (true)
        {
            auto value = parseValue();
            if (!value)
            {
                return std::nullopt;
            }
            result.array_value.push_back(std::move(*value));
            skipWhitespace();
            if (consume(']'))
            {
                return result;
            }
            if (!consume(','))
            {
                return std::nullopt;
            }
        }
    }

    [[nodiscard]] auto parseString() -> std::optional<std::string>
    {
        skipWhitespace();
        if (!consume('"'))
        {
            return std::nullopt;
        }
        std::string result;
        while (offset_ < input_.size())
        {
            const auto character = input_[offset_++];
            if (character == '"')
            {
                return result;
            }
            if (static_cast<unsigned char>(character) < 0x20U)
            {
                return std::nullopt;
            }
            if (character != '\\')
            {
                result.push_back(character);
                continue;
            }
            if (offset_ >= input_.size())
            {
                return std::nullopt;
            }
            const auto escaped = input_[offset_++];
            switch (escaped)
            {
            case '"':
            case '\\':
            case '/':
                result.push_back(escaped);
                break;
            case 'b':
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            case 'u':
                if (!appendEscapedCodePoint(result))
                {
                    return std::nullopt;
                }
                break;
            default:
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] auto parseHexCodeUnit() -> std::optional<std::uint16_t>
    {
        constexpr std::size_t digit_count = 4;
        if (offset_ + digit_count > input_.size())
        {
            return std::nullopt;
        }
        std::uint16_t value{0};
        for (std::size_t index = 0; index < digit_count; ++index)
        {
            const auto character = input_[offset_++];
            value = static_cast<std::uint16_t>(value << 4U);
            if (character >= '0' && character <= '9')
            {
                value = static_cast<std::uint16_t>(value + character - '0');
            }
            else if (character >= 'a' && character <= 'f')
            {
                value = static_cast<std::uint16_t>(value + character - 'a' + 10);
            }
            else if (character >= 'A' && character <= 'F')
            {
                value = static_cast<std::uint16_t>(value + character - 'A' + 10);
            }
            else
            {
                return std::nullopt;
            }
        }
        return value;
    }

    [[nodiscard]] auto appendEscapedCodePoint(std::string& output) -> bool
    {
        auto first = parseHexCodeUnit();
        if (!first)
        {
            return false;
        }
        std::uint32_t code_point = *first;
        constexpr std::uint16_t high_surrogate_begin = 0xD800U;
        constexpr std::uint16_t high_surrogate_end = 0xDBFFU;
        constexpr std::uint16_t low_surrogate_begin = 0xDC00U;
        constexpr std::uint16_t low_surrogate_end = 0xDFFFU;
        if (*first >= high_surrogate_begin && *first <= high_surrogate_end)
        {
            if (offset_ + 2U > input_.size() || input_[offset_] != '\\' ||
                input_[offset_ + 1U] != 'u')
            {
                return false;
            }
            offset_ += 2U;
            const auto second = parseHexCodeUnit();
            if (!second || *second < low_surrogate_begin || *second > low_surrogate_end)
            {
                return false;
            }
            code_point =
                0x10000U + ((static_cast<std::uint32_t>(*first - high_surrogate_begin) << 10U) |
                            static_cast<std::uint32_t>(*second - low_surrogate_begin));
        }
        else if (*first >= low_surrogate_begin && *first <= low_surrogate_end)
        {
            return false;
        }
        appendUtf8(output, code_point);
        return true;
    }

    static void appendUtf8(std::string& output, std::uint32_t code_point)
    {
        if (code_point <= 0x7FU)
        {
            output.push_back(static_cast<char>(code_point));
        }
        else if (code_point <= 0x7FFU)
        {
            output.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
            output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        }
        else if (code_point <= 0xFFFFU)
        {
            output.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        }
        else
        {
            output.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
            output.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        }
    }

    [[nodiscard]] auto parseUnsigned() -> std::optional<JsonValue>
    {
        const auto begin = offset_;
        while (offset_ < input_.size() && input_[offset_] >= '0' && input_[offset_] <= '9')
        {
            ++offset_;
        }
        if (begin == offset_ || (offset_ - begin > 1 && input_[begin] == '0'))
        {
            return std::nullopt;
        }
        std::uint64_t result{};
        const auto conversion =
            std::from_chars(input_.data() + begin, input_.data() + offset_, result);
        if (conversion.ec != std::errc{} || conversion.ptr != input_.data() + offset_)
        {
            return std::nullopt;
        }
        JsonValue value{JsonType::unsigned_integer};
        value.unsigned_value = result;
        return value;
    }

    std::string_view input_;
    std::size_t offset_{0};
};

[[nodiscard]] auto field(const JsonValue& object, std::string_view name, JsonType type)
    -> const JsonValue*
{
    if (object.type != JsonType::object)
    {
        return nullptr;
    }
    const auto value = object.object_value.find(name);
    if (value == object.object_value.end() || value->second.type != type)
    {
        return nullptr;
    }
    return &value->second;
}

[[nodiscard]] auto parseScope(std::string_view value) -> std::optional<domain::VoiceScope>
{
    if (value == "team")
    {
        return domain::VoiceScope::team;
    }
    if (value == "specialization")
    {
        return domain::VoiceScope::specialization;
    }
    if (value == "group")
    {
        return domain::VoiceScope::group;
    }
    return std::nullopt;
}

[[nodiscard]] auto scopeName(domain::VoiceScope scope) -> std::string_view
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

[[nodiscard]] auto escapeJson(std::string_view value) -> std::string
{
    std::string result{"\""};
    for (const auto character : value)
    {
        switch (character)
        {
        case '"':
        case '\\':
            result.push_back('\\');
            result.push_back(character);
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(character) < 0x20U)
            {
                throw std::invalid_argument{"JSON value contains an unsupported control character"};
            }
            result.push_back(character);
            break;
        }
    }
    result.push_back('"');
    return result;
}

[[nodiscard]] auto invalidState(std::string message) -> ControlPlaneError
{
    return {ControlPlaneErrorKind::invalid_state, 0, "invalid_state", std::move(message)};
}

[[nodiscard]] auto invalidArgument(std::string message) -> ControlPlaneError
{
    return {ControlPlaneErrorKind::invalid_argument, 0, "invalid_argument", std::move(message)};
}

[[nodiscard]] auto invalidResponse(std::string message) -> ControlPlaneError
{
    return {ControlPlaneErrorKind::invalid_response, 0, "invalid_response", std::move(message)};
}

[[nodiscard]] auto asciiHeaderNameEquals(std::string_view left, std::string_view right) noexcept
    -> bool
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index)
    {
        const auto lower = [](char character) noexcept {
            return character >= 'A' && character <= 'Z' ? static_cast<char>(character + ('a' - 'A'))
                                                        : character;
        };
        if (lower(left[index]) != lower(right[index]))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] auto responseHeader(const ClientHttpResponse& response, std::string_view name)
    -> std::optional<std::string_view>
{
    for (const auto& [header_name, value] : response.headers)
    {
        if (asciiHeaderNameEquals(header_name, name))
        {
            return value;
        }
    }
    return std::nullopt;
}

[[nodiscard]] auto responseRoot(const ClientHttpResponse& response)
    -> std::variant<JsonValue, ControlPlaneError>
{
    if (!response.transport_error.empty())
    {
        return ControlPlaneError{ControlPlaneErrorKind::transport, 0, "transport_error",
                                 response.transport_error};
    }
    const auto protocol_version = responseHeader(response, "x-hvc-api-version");
    if (!protocol_version || *protocol_version != "v1")
    {
        return invalidResponse("control-plane response has an unsupported protocol header");
    }
    auto parsed = JsonParser{response.body}.parse();
    if (!parsed)
    {
        return invalidResponse("control-plane response is not valid JSON");
    }
    const auto* version = field(*parsed, "api_version", JsonType::string);
    if (version == nullptr || version->string_value != "v1")
    {
        return invalidResponse("control-plane response has an unsupported API version");
    }
    if (response.status_code < 200 || response.status_code >= 300)
    {
        const auto* error = field(*parsed, "error", JsonType::object);
        const auto* code = error == nullptr ? nullptr : field(*error, "code", JsonType::string);
        const auto* message =
            error == nullptr ? nullptr : field(*error, "message", JsonType::string);
        if (code == nullptr || message == nullptr)
        {
            return invalidResponse("control-plane error response has no valid error envelope");
        }
        return ControlPlaneError{ControlPlaneErrorKind::server, response.status_code,
                                 code->string_value, message->string_value};
    }
    return std::move(*parsed);
}

[[nodiscard]] auto timePoint(std::uint64_t milliseconds)
    -> std::optional<std::chrono::system_clock::time_point>
{
    if (milliseconds > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    {
        return std::nullopt;
    }
    return std::chrono::system_clock::time_point{
        std::chrono::milliseconds{static_cast<std::int64_t>(milliseconds)}};
}

template <typename Value>
[[nodiscard]] auto responseFailure(const std::variant<JsonValue, ControlPlaneError>& response)
    -> ControlPlaneResult<Value>
{
    return ControlPlaneResult<Value>::failure(std::get<ControlPlaneError>(response));
}
} // namespace

ControlPlaneClient::ControlPlaneClient(IClientHttpTransport& transport,
                                       IClientIdentifierGenerator& identifiers,
                                       domain::DeviceId device_id)
    : transport_(transport), identifiers_(identifiers), device_id_(std::move(device_id))
{
}

auto ControlPlaneClient::createSession(std::string_view external_credential)
    -> ControlPlaneResult<ControlPlaneSession>
{
    if (external_credential.empty())
    {
        return ControlPlaneResult<ControlPlaneSession>::failure(
            invalidArgument("external credential must not be empty"));
    }

    ClientHttpRequest request{
        "POST",
        "/api/v1/sessions",
        {{"authorization", "Bearer " + std::string{external_credential}},
         {"x-correlation-id", std::string{identifiers_.nextCorrelationId().value()}},
         {"x-hvc-device-id", std::string{device_id_.value()}}},
        {}};
    ClientHttpResponse response;
    try
    {
        response = transport_.send(request);
    }
    catch (const std::exception& error)
    {
        return ControlPlaneResult<ControlPlaneSession>::failure(
            {ControlPlaneErrorKind::transport, 0, "transport_exception", error.what()});
    }
    auto root = responseRoot(response);
    if (std::holds_alternative<ControlPlaneError>(root))
    {
        return responseFailure<ControlPlaneSession>(root);
    }

    const auto& object = std::get<JsonValue>(root);
    const auto* session_id = field(object, "session_id", JsonType::string);
    const auto* player_id = field(object, "player_id", JsonType::string);
    const auto* device_id = field(object, "device_id", JsonType::string);
    const auto* expiration = field(object, "expires_at_unix_ms", JsonType::unsigned_integer);
    const auto expires_at =
        expiration == nullptr ? std::nullopt : timePoint(expiration->unsigned_value);
    if (session_id == nullptr || player_id == nullptr || device_id == nullptr || !expires_at ||
        device_id->string_value != device_id_.value())
    {
        return ControlPlaneResult<ControlPlaneSession>::failure(
            invalidResponse("session response is incomplete or device-mismatched"));
    }

    try
    {
        ControlPlaneSession created{domain::SessionId{session_id->string_value},
                                    domain::PlayerId{player_id->string_value}, device_id_,
                                    *expires_at};
        session_ = created;
        return ControlPlaneResult<ControlPlaneSession>::success(std::move(created));
    }
    catch (const std::invalid_argument&)
    {
        return ControlPlaneResult<ControlPlaneSession>::failure(
            invalidResponse("session response contains an invalid identifier"));
    }
}

auto ControlPlaneClient::membership() -> ControlPlaneResult<MembershipView>
{
    if (!session_)
    {
        return ControlPlaneResult<MembershipView>::failure(
            invalidState("a session is required before membership retrieval"));
    }
    auto root = responseRoot(sendSessionRequest("GET", "/api/v1/membership"));
    if (std::holds_alternative<ControlPlaneError>(root))
    {
        return responseFailure<MembershipView>(root);
    }

    const auto& object = std::get<JsonValue>(root);
    const auto* version = field(object, "membership_version", JsonType::unsigned_integer);
    const auto* hierarchy = field(object, "hierarchy_id", JsonType::string);
    const auto* player = field(object, "player_id", JsonType::string);
    const auto* group = field(object, "group_id", JsonType::string);
    const auto* specialization = field(object, "specialization_id", JsonType::string);
    const auto* team = field(object, "team_id", JsonType::string);
    const auto* roles = field(object, "role_ids", JsonType::array);
    const auto* connected = field(object, "connected", JsonType::boolean);
    const auto* can_receive = field(object, "can_receive_voice", JsonType::boolean);
    const auto* transmit_muted = field(object, "transmit_muted", JsonType::boolean);
    if (version == nullptr || version->unsigned_value == 0 || hierarchy == nullptr ||
        player == nullptr || group == nullptr || specialization == nullptr || team == nullptr ||
        roles == nullptr || connected == nullptr || can_receive == nullptr ||
        transmit_muted == nullptr || player->string_value != session_->player_id.value())
    {
        return ControlPlaneResult<MembershipView>::failure(
            invalidResponse("membership response is incomplete or session-mismatched"));
    }

    try
    {
        std::vector<domain::RoleId> role_ids;
        role_ids.reserve(roles->array_value.size());
        for (const auto& role : roles->array_value)
        {
            if (role.type != JsonType::string)
            {
                return ControlPlaneResult<MembershipView>::failure(
                    invalidResponse("membership role IDs must be strings"));
            }
            role_ids.emplace_back(role.string_value);
        }
        MembershipView membership_view{version->unsigned_value,
                                       domain::HierarchyId{hierarchy->string_value},
                                       domain::PlayerId{player->string_value},
                                       domain::GroupId{group->string_value},
                                       domain::SpecializationId{specialization->string_value},
                                       domain::TeamId{team->string_value},
                                       std::move(role_ids),
                                       connected->boolean_value,
                                       can_receive->boolean_value,
                                       transmit_muted->boolean_value};
        return ControlPlaneResult<MembershipView>::success(std::move(membership_view));
    }
    catch (const std::invalid_argument&)
    {
        return ControlPlaneResult<MembershipView>::failure(
            invalidResponse("membership response contains an invalid identifier"));
    }
}

auto ControlPlaneClient::voiceGrants() -> ControlPlaneResult<VoiceGrantSet>
{
    if (!session_)
    {
        return ControlPlaneResult<VoiceGrantSet>::failure(
            invalidState("a session is required before voice grant issuance"));
    }
    auto root = responseRoot(sendSessionRequest("POST", "/api/v1/voice-grants"));
    if (std::holds_alternative<ControlPlaneError>(root))
    {
        return responseFailure<VoiceGrantSet>(root);
    }

    const auto& object = std::get<JsonValue>(root);
    const auto* server_url = field(object, "server_url", JsonType::string);
    const auto* version = field(object, "membership_version", JsonType::unsigned_integer);
    const auto* expiration = field(object, "expires_at_unix_ms", JsonType::unsigned_integer);
    const auto* grants = field(object, "grants", JsonType::array);
    const auto expires_at =
        expiration == nullptr ? std::nullopt : timePoint(expiration->unsigned_value);
    if (server_url == nullptr || server_url->string_value.empty() || version == nullptr ||
        version->unsigned_value == 0 || !expires_at || grants == nullptr ||
        grants->array_value.empty())
    {
        return ControlPlaneResult<VoiceGrantSet>::failure(
            invalidResponse("voice grant response is incomplete"));
    }

    std::vector<VoiceRoomGrant> room_grants;
    room_grants.reserve(grants->array_value.size());
    constexpr std::size_t voice_scope_count = 3;
    if (grants->array_value.size() > voice_scope_count)
    {
        return ControlPlaneResult<VoiceGrantSet>::failure(
            invalidResponse("voice grant response contains too many scopes"));
    }
    std::array<bool, voice_scope_count> seen_scopes{};
    for (const auto& grant : grants->array_value)
    {
        const auto* scope_value = field(grant, "scope", JsonType::string);
        const auto* token = field(grant, "access_token", JsonType::string);
        const auto scope =
            scope_value == nullptr ? std::nullopt : parseScope(scope_value->string_value);
        if (!scope || token == nullptr || token->string_value.empty())
        {
            return ControlPlaneResult<VoiceGrantSet>::failure(
                invalidResponse("voice grant entry is incomplete"));
        }
        const auto scope_index = static_cast<std::size_t>(*scope);
        if (scope_index >= seen_scopes.size() || seen_scopes[scope_index])
        {
            return ControlPlaneResult<VoiceGrantSet>::failure(
                invalidResponse("voice grant response contains a duplicate scope"));
        }
        seen_scopes[scope_index] = true;
        room_grants.push_back({*scope, server_url->string_value, token->string_value});
    }
    return ControlPlaneResult<VoiceGrantSet>::success(
        {version->unsigned_value, *expires_at, std::move(room_grants)});
}

auto ControlPlaneClient::startTransmission(domain::VoiceScope scope,
                                           std::uint64_t membership_version)
    -> ControlPlaneResult<StartedTransmission>
{
    if (!session_)
    {
        return ControlPlaneResult<StartedTransmission>::failure(
            invalidState("a session is required before starting a transmission"));
    }
    if (membership_version == 0)
    {
        return ControlPlaneResult<StartedTransmission>::failure(
            invalidArgument("membership version must be positive"));
    }
    auto client_id = identifiers_.nextTransmissionId();
    const auto body = "{\"client_transmission_id\":" + escapeJson(client_id.value()) +
                      ",\"scope\":" + escapeJson(scopeName(scope)) +
                      ",\"membership_version\":" + std::to_string(membership_version) + '}';
    auto root = responseRoot(sendSessionRequest("POST", "/api/v1/transmissions", body));
    if (std::holds_alternative<ControlPlaneError>(root))
    {
        return responseFailure<StartedTransmission>(root);
    }

    const auto& object = std::get<JsonValue>(root);
    const auto* transmission_id = field(object, "transmission_id", JsonType::string);
    const auto* response_client_id = field(object, "client_transmission_id", JsonType::string);
    const auto* scope_value = field(object, "scope", JsonType::string);
    const auto* version = field(object, "membership_version", JsonType::unsigned_integer);
    const auto* recipients = field(object, "recipient_count", JsonType::unsigned_integer);
    const auto response_scope =
        scope_value == nullptr ? std::nullopt : parseScope(scope_value->string_value);
    if (transmission_id == nullptr || response_client_id == nullptr || !response_scope ||
        version == nullptr || recipients == nullptr ||
        response_client_id->string_value != client_id.value() || *response_scope != scope ||
        version->unsigned_value != membership_version ||
        recipients->unsigned_value > std::numeric_limits<std::size_t>::max())
    {
        return ControlPlaneResult<StartedTransmission>::failure(
            invalidResponse("transmission start response does not match the request"));
    }
    try
    {
        return ControlPlaneResult<StartedTransmission>::success(
            {domain::TransmissionId{transmission_id->string_value}, std::move(client_id),
             *response_scope, version->unsigned_value,
             static_cast<std::size_t>(recipients->unsigned_value)});
    }
    catch (const std::invalid_argument&)
    {
        return ControlPlaneResult<StartedTransmission>::failure(
            invalidResponse("transmission response contains an invalid identifier"));
    }
}

auto ControlPlaneClient::endTransmission(const domain::TransmissionId& transmission_id)
    -> ControlPlaneResult<EndedTransmissionView>
{
    if (!session_)
    {
        return ControlPlaneResult<EndedTransmissionView>::failure(
            invalidState("a session is required before ending a transmission"));
    }
    auto root = responseRoot(sendSessionRequest(
        "DELETE", "/api/v1/transmissions/" + std::string{transmission_id.value()}));
    if (std::holds_alternative<ControlPlaneError>(root))
    {
        return responseFailure<EndedTransmissionView>(root);
    }

    const auto& object = std::get<JsonValue>(root);
    const auto* response_id = field(object, "transmission_id", JsonType::string);
    const auto* status = field(object, "status", JsonType::string);
    const auto* reason = field(object, "stop_reason", JsonType::string);
    if (response_id == nullptr || status == nullptr || reason == nullptr ||
        response_id->string_value != transmission_id.value() || status->string_value != "ended")
    {
        return ControlPlaneResult<EndedTransmissionView>::failure(
            invalidResponse("transmission end response does not match the request"));
    }
    return ControlPlaneResult<EndedTransmissionView>::success(
        {transmission_id, reason->string_value});
}

auto ControlPlaneClient::session() const -> std::optional<ControlPlaneSession>
{
    return session_;
}

void ControlPlaneClient::clearSession() noexcept
{
    session_.reset();
}

auto ControlPlaneClient::sendSessionRequest(std::string method, std::string target,
                                            std::string body) -> ClientHttpResponse
{
    if (!session_)
    {
        return {0, {}, {}, "no active session"};
    }
    ClientHttpRequest request{
        std::move(method),
        std::move(target),
        {{"authorization", "Session " + std::string{session_->session_id.value()}},
         {"x-correlation-id", std::string{identifiers_.nextCorrelationId().value()}},
         {"x-hvc-device-id", std::string{device_id_.value()}}},
        std::move(body)};
    try
    {
        return transport_.send(request);
    }
    catch (const std::exception& error)
    {
        return {0, {}, {}, error.what()};
    }
}
} // namespace hvc::client
