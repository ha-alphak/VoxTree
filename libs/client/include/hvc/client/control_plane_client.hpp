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
struct ClientHttpRequest final
{
    std::string method;
    std::string target;
    std::map<std::string, std::string, std::less<>> headers;
    std::string body;
};

struct ClientHttpResponse final
{
    int status_code{0};
    std::map<std::string, std::string, std::less<>> headers;
    std::string body;
    std::string transport_error;
};

class IClientHttpTransport
{
  public:
    virtual ~IClientHttpTransport() = default;

    [[nodiscard]] virtual auto send(const ClientHttpRequest& request) -> ClientHttpResponse = 0;
};

class IClientIdentifierGenerator
{
  public:
    virtual ~IClientIdentifierGenerator() = default;

    [[nodiscard]] virtual auto nextCorrelationId() -> domain::CorrelationId = 0;
    [[nodiscard]] virtual auto nextTransmissionId() -> domain::ClientTransmissionId = 0;
};

enum class ControlPlaneErrorKind : std::uint8_t
{
    invalid_state,
    invalid_argument,
    transport,
    invalid_response,
    server
};

struct ControlPlaneError final
{
    ControlPlaneErrorKind kind{ControlPlaneErrorKind::invalid_response};
    int status_code{0};
    std::string code;
    std::string message;
};

template <typename Value> struct ControlPlaneResult final
{
    [[nodiscard]] static auto success(Value result) -> ControlPlaneResult
    {
        return ControlPlaneResult{std::move(result), std::nullopt};
    }

    [[nodiscard]] static auto failure(ControlPlaneError result_error) -> ControlPlaneResult
    {
        return ControlPlaneResult{std::nullopt, std::move(result_error)};
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return value.has_value();
    }

    std::optional<Value> value;
    std::optional<ControlPlaneError> error;
};

struct ControlPlaneSession final
{
    domain::SessionId session_id;
    domain::PlayerId player_id;
    domain::DeviceId device_id;
    std::chrono::system_clock::time_point expires_at;
};

struct MembershipView final
{
    std::uint64_t version{0};
    domain::HierarchyId hierarchy_id;
    domain::PlayerId player_id;
    domain::GroupId group_id;
    domain::SpecializationId specialization_id;
    domain::TeamId team_id;
    std::vector<domain::RoleId> role_ids;
    bool connected{false};
    bool can_receive_voice{false};
    bool transmit_muted{false};
};

struct VoiceGrantSet final
{
    std::uint64_t membership_version{0};
    std::chrono::system_clock::time_point expires_at;
    std::vector<VoiceRoomGrant> room_grants;
};

struct StartedTransmission final
{
    domain::TransmissionId transmission_id;
    domain::ClientTransmissionId client_transmission_id;
    domain::VoiceScope scope{domain::VoiceScope::team};
    std::uint64_t membership_version{0};
    std::size_t recipient_count{0};
};

struct EndedTransmissionView final
{
    domain::TransmissionId transmission_id;
    std::string stop_reason;
};

class ControlPlaneClient final
{
  public:
    ControlPlaneClient(IClientHttpTransport& transport, IClientIdentifierGenerator& identifiers,
                       domain::DeviceId device_id);

    [[nodiscard]] auto createSession(std::string_view external_credential)
        -> ControlPlaneResult<ControlPlaneSession>;
    [[nodiscard]] auto membership() -> ControlPlaneResult<MembershipView>;
    [[nodiscard]] auto voiceGrants() -> ControlPlaneResult<VoiceGrantSet>;
    [[nodiscard]] auto startTransmission(domain::VoiceScope scope, std::uint64_t membership_version)
        -> ControlPlaneResult<StartedTransmission>;
    [[nodiscard]] auto endTransmission(const domain::TransmissionId& transmission_id)
        -> ControlPlaneResult<EndedTransmissionView>;

    [[nodiscard]] auto session() const -> std::optional<ControlPlaneSession>;
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
