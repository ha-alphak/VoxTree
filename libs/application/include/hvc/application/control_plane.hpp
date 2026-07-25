#pragma once

#include <chrono>
#include <cstdint>
#include <hvc/domain/routing.hpp>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hvc::application
{
using Clock = std::chrono::system_clock;
using TimePoint = Clock::time_point;

struct AuthenticateSessionCommand final
{
    AuthenticateSessionCommand(std::string credential_value, domain::DeviceId device,
                               domain::CorrelationId correlation)
        : credential(std::move(credential_value)), device_id(std::move(device)),
          correlation_id(std::move(correlation))
    {
    }

    std::string credential;
    domain::DeviceId device_id;
    domain::CorrelationId correlation_id;
};

struct AuthenticatedSession final
{
    AuthenticatedSession(domain::SessionId session, domain::PlayerId player,
                         domain::DeviceId device, TimePoint expiration)
        : session_id(std::move(session)), player_id(std::move(player)),
          device_id(std::move(device)), expires_at(expiration)
    {
    }

    [[nodiscard]] auto activeAt(TimePoint now) const noexcept -> bool
    {
        return now < expires_at;
    }

    domain::SessionId session_id;
    domain::PlayerId player_id;
    domain::DeviceId device_id;
    TimePoint expires_at;
};

enum class SessionAuthenticationError : std::uint8_t
{
    invalid_credentials,
    device_not_allowed,
    account_disabled,
    rate_limited
};

struct SessionAuthenticationResult final
{
    [[nodiscard]] static auto accepted(AuthenticatedSession authenticated_session)
        -> SessionAuthenticationResult;
    [[nodiscard]] static auto rejected(SessionAuthenticationError authentication_error)
        -> SessionAuthenticationResult;

    [[nodiscard]] auto authenticated() const noexcept -> bool
    {
        return session.has_value();
    }

    std::optional<AuthenticatedSession> session;
    std::optional<SessionAuthenticationError> error;

  private:
    SessionAuthenticationResult(std::optional<AuthenticatedSession> authenticated_session,
                                std::optional<SessionAuthenticationError> authentication_error);
};

class ISessionAuthenticator
{
  public:
    virtual ~ISessionAuthenticator() = default;

    [[nodiscard]] virtual auto authenticate(const AuthenticateSessionCommand& command,
                                            TimePoint now) -> SessionAuthenticationResult = 0;
};

class ISessionRepository
{
  public:
    virtual ~ISessionRepository() = default;

    [[nodiscard]] virtual auto find(const domain::SessionId& session_id) const
        -> std::optional<AuthenticatedSession> = 0;
};

struct AuthoritativeMembershipContext final
{
    AuthoritativeMembershipContext(std::shared_ptr<const domain::MembershipSnapshot> snapshot_value,
                                   std::shared_ptr<const domain::RolePolicy> role_policy_value);

    std::shared_ptr<const domain::MembershipSnapshot> snapshot;
    std::shared_ptr<const domain::RolePolicy> role_policy;
};

class IAuthoritativeMembershipProvider
{
  public:
    virtual ~IAuthoritativeMembershipProvider() = default;

    [[nodiscard]] virtual auto currentFor(const domain::PlayerId& player_id) const
        -> std::optional<AuthoritativeMembershipContext> = 0;
};

struct StartTransmissionCommand final
{
    StartTransmissionCommand(domain::SessionId session, domain::DeviceId device,
                             domain::ClientTransmissionId client_transmission,
                             domain::VoiceScope requested_scope,
                             std::uint64_t client_membership_version,
                             domain::CorrelationId correlation)
        : session_id(std::move(session)), device_id(std::move(device)),
          client_transmission_id(std::move(client_transmission)), scope(requested_scope),
          membership_version(client_membership_version), correlation_id(std::move(correlation))
    {
    }

    domain::SessionId session_id;
    domain::DeviceId device_id;
    domain::ClientTransmissionId client_transmission_id;
    domain::VoiceScope scope;
    std::uint64_t membership_version;
    domain::CorrelationId correlation_id;
};

enum class TransmissionAuthorizationError : std::uint8_t
{
    session_not_found,
    session_device_mismatch,
    session_expired,
    membership_unavailable,
    voice_not_connected,
    voice_no_active_membership,
    voice_scope_not_found,
    voice_scope_not_authorized,
    voice_transmit_muted,
    voice_membership_stale
};

struct AuthorizedTransmission final
{
    AuthorizedTransmission(domain::TransmissionId transmission,
                           domain::ClientTransmissionId client_transmission,
                           domain::PlayerId sender, domain::VoiceScope authorized_scope,
                           std::uint64_t authorized_membership_version,
                           std::vector<domain::PlayerId> resolved_recipients,
                           domain::CorrelationId correlation)
        : transmission_id(std::move(transmission)),
          client_transmission_id(std::move(client_transmission)),
          sender_player_id(std::move(sender)), scope(authorized_scope),
          membership_version(authorized_membership_version),
          recipients(std::move(resolved_recipients)), correlation_id(std::move(correlation))
    {
    }

    domain::TransmissionId transmission_id;
    domain::ClientTransmissionId client_transmission_id;
    domain::PlayerId sender_player_id;
    domain::VoiceScope scope;
    std::uint64_t membership_version;
    std::vector<domain::PlayerId> recipients;
    domain::CorrelationId correlation_id;
};

struct TransmissionAuthorizationResult final
{
    [[nodiscard]] static auto accepted(AuthorizedTransmission authorized_transmission)
        -> TransmissionAuthorizationResult;
    [[nodiscard]] static auto rejected(TransmissionAuthorizationError authorization_error)
        -> TransmissionAuthorizationResult;

    [[nodiscard]] auto authorized() const noexcept -> bool
    {
        return transmission.has_value();
    }

    std::optional<AuthorizedTransmission> transmission;
    std::optional<TransmissionAuthorizationError> error;

  private:
    TransmissionAuthorizationResult(
        std::optional<AuthorizedTransmission> authorized_transmission,
        std::optional<TransmissionAuthorizationError> authorization_error);
};

class ITransmissionIdGenerator
{
  public:
    virtual ~ITransmissionIdGenerator() = default;

    [[nodiscard]] virtual auto next() -> domain::TransmissionId = 0;
};

class TransmissionAuthorizationService final
{
  public:
    TransmissionAuthorizationService(const ISessionRepository& sessions,
                                     const IAuthoritativeMembershipProvider& memberships,
                                     ITransmissionIdGenerator& transmission_ids);

    [[nodiscard]] auto authorizeStart(const StartTransmissionCommand& command, TimePoint now)
        -> TransmissionAuthorizationResult;

  private:
    const ISessionRepository& sessions_;
    const IAuthoritativeMembershipProvider& memberships_;
    ITransmissionIdGenerator& transmission_ids_;
};
} // namespace hvc::application
