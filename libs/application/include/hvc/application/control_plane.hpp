#pragma once

#include <chrono>
#include <cstdint>
#include <hvc/application/transmission_audit.hpp>
#include <hvc/domain/routing.hpp>
#include <hvc/domain/state_machine.hpp>
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

enum class TransmissionRateLimitAction : std::uint8_t
{
    start,
    end
};

class ITransmissionRateLimiter
{
  public:
    virtual ~ITransmissionRateLimiter() = default;

    [[nodiscard]] virtual auto allow(const domain::PlayerId& player_id,
                                     TransmissionRateLimitAction action, TimePoint now) -> bool = 0;
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

enum class TransmissionActivationError : std::uint8_t
{
    session_changed,
    membership_changed,
    sender_already_transmitting,
    transmission_id_conflict
};

struct ActiveTransmission final
{
    ActiveTransmission(AuthorizedTransmission authorized_transmission, domain::SessionId session,
                       domain::DeviceId device, TimePoint start_time)
        : authorization(std::move(authorized_transmission)), session_id(std::move(session)),
          device_id(std::move(device)), started_at(start_time)
    {
    }

    AuthorizedTransmission authorization;
    domain::SessionId session_id;
    domain::DeviceId device_id;
    TimePoint started_at;
};

struct TransmissionActivationResult final
{
    [[nodiscard]] static auto activated(ActiveTransmission active_transmission)
        -> TransmissionActivationResult;
    [[nodiscard]] static auto rejected(TransmissionActivationError activation_error)
        -> TransmissionActivationResult;

    [[nodiscard]] auto active() const noexcept -> bool
    {
        return transmission.has_value();
    }

    std::optional<ActiveTransmission> transmission;
    std::optional<TransmissionActivationError> error;

  private:
    TransmissionActivationResult(std::optional<ActiveTransmission> active_transmission,
                                 std::optional<TransmissionActivationError> activation_error);
};

struct EndedTransmission final
{
    EndedTransmission(ActiveTransmission active_transmission,
                      domain::TransmissionStopReason transmission_stop_reason, TimePoint end_time,
                      domain::CorrelationId correlation)
        : transmission(std::move(active_transmission)), stop_reason(transmission_stop_reason),
          ended_at(end_time), correlation_id(std::move(correlation))
    {
    }

    ActiveTransmission transmission;
    domain::TransmissionStopReason stop_reason;
    TimePoint ended_at;
    domain::CorrelationId correlation_id;
};

enum class TransmissionEndRepositoryError : std::uint8_t
{
    transmission_not_found,
    transmission_not_owned
};

struct TransmissionEndRepositoryResult final
{
    [[nodiscard]] static auto ended(EndedTransmission ended_transmission)
        -> TransmissionEndRepositoryResult;
    [[nodiscard]] static auto rejected(TransmissionEndRepositoryError repository_error)
        -> TransmissionEndRepositoryResult;

    [[nodiscard]] auto successful() const noexcept -> bool
    {
        return transmission.has_value();
    }

    std::optional<EndedTransmission> transmission;
    std::optional<TransmissionEndRepositoryError> error;

  private:
    TransmissionEndRepositoryResult(std::optional<EndedTransmission> ended_transmission,
                                    std::optional<TransmissionEndRepositoryError> repository_error);
};

class IActiveTransmissionRepository
{
  public:
    virtual ~IActiveTransmissionRepository() = default;

    // Activation must verify session, device and authoritative membership version in the same
    // atomic operation that makes the transmission visible.
    [[nodiscard]] virtual auto activate(AuthorizedTransmission transmission,
                                        const domain::SessionId& session_id,
                                        const domain::DeviceId& device_id, TimePoint started_at)
        -> TransmissionActivationResult = 0;
    [[nodiscard]] virtual auto end(const domain::TransmissionId& transmission_id,
                                   const domain::SessionId& session_id,
                                   const domain::DeviceId& device_id,
                                   domain::TransmissionStopReason stop_reason, TimePoint ended_at,
                                   const domain::CorrelationId& correlation_id)
        -> TransmissionEndRepositoryResult = 0;
    [[nodiscard]] virtual auto interrupt(const domain::TransmissionId& transmission_id,
                                         domain::TransmissionStopReason stop_reason,
                                         TimePoint ended_at,
                                         const domain::CorrelationId& correlation_id)
        -> TransmissionEndRepositoryResult = 0;
    [[nodiscard]] virtual auto expireTimedOut(std::chrono::milliseconds maximum_duration,
                                              TimePoint now,
                                              const domain::CorrelationId& correlation_id)
        -> std::vector<EndedTransmission> = 0;
};

enum class StartTransmissionError : std::uint8_t
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
    voice_membership_stale,
    session_changed_during_start,
    membership_changed_during_start,
    sender_already_transmitting,
    transmission_id_conflict,
    rate_limited
};

struct StartTransmissionResult final
{
    [[nodiscard]] static auto started(ActiveTransmission active_transmission)
        -> StartTransmissionResult;
    [[nodiscard]] static auto rejected(StartTransmissionError start_error)
        -> StartTransmissionResult;

    [[nodiscard]] auto successful() const noexcept -> bool
    {
        return transmission.has_value();
    }

    std::optional<ActiveTransmission> transmission;
    std::optional<StartTransmissionError> error;

  private:
    StartTransmissionResult(std::optional<ActiveTransmission> active_transmission,
                            std::optional<StartTransmissionError> start_error);
};

struct EndTransmissionCommand final
{
    EndTransmissionCommand(domain::SessionId session, domain::DeviceId device,
                           domain::TransmissionId transmission, domain::CorrelationId correlation)
        : session_id(std::move(session)), device_id(std::move(device)),
          transmission_id(std::move(transmission)), correlation_id(std::move(correlation))
    {
    }

    domain::SessionId session_id;
    domain::DeviceId device_id;
    domain::TransmissionId transmission_id;
    domain::CorrelationId correlation_id;
};

enum class EndTransmissionError : std::uint8_t
{
    session_not_found,
    session_device_mismatch,
    session_expired,
    transmission_not_found,
    transmission_not_owned,
    rate_limited
};

struct EndTransmissionResult final
{
    [[nodiscard]] static auto ended(EndedTransmission ended_transmission) -> EndTransmissionResult;
    [[nodiscard]] static auto rejected(EndTransmissionError end_error) -> EndTransmissionResult;

    [[nodiscard]] auto successful() const noexcept -> bool
    {
        return transmission.has_value();
    }

    std::optional<EndedTransmission> transmission;
    std::optional<EndTransmissionError> error;

  private:
    EndTransmissionResult(std::optional<EndedTransmission> ended_transmission,
                          std::optional<EndTransmissionError> end_error);
};

class ITransmissionModerationAuthorizer
{
  public:
    virtual ~ITransmissionModerationAuthorizer() = default;

    [[nodiscard]] virtual auto canInterrupt(const domain::PlayerId& moderator_player_id,
                                            const domain::TransmissionId& transmission_id) const
        -> bool = 0;
};

struct ModerateTransmissionCommand final
{
    ModerateTransmissionCommand(domain::SessionId session, domain::DeviceId device,
                                domain::TransmissionId transmission,
                                domain::CorrelationId correlation)
        : session_id(std::move(session)), device_id(std::move(device)),
          transmission_id(std::move(transmission)), correlation_id(std::move(correlation))
    {
    }

    domain::SessionId session_id;
    domain::DeviceId device_id;
    domain::TransmissionId transmission_id;
    domain::CorrelationId correlation_id;
};

enum class ModerateTransmissionError : std::uint8_t
{
    session_not_found,
    session_device_mismatch,
    session_expired,
    not_authorized,
    transmission_not_found
};

struct ModerateTransmissionResult final
{
    [[nodiscard]] static auto interrupted(EndedTransmission ended_transmission)
        -> ModerateTransmissionResult;
    [[nodiscard]] static auto rejected(ModerateTransmissionError moderation_error)
        -> ModerateTransmissionResult;

    [[nodiscard]] auto successful() const noexcept -> bool
    {
        return transmission.has_value();
    }

    std::optional<EndedTransmission> transmission;
    std::optional<ModerateTransmissionError> error;

  private:
    ModerateTransmissionResult(std::optional<EndedTransmission> ended_transmission,
                               std::optional<ModerateTransmissionError> moderation_error);
};

struct TransmissionLifecyclePolicy final
{
    explicit TransmissionLifecyclePolicy(std::chrono::milliseconds maximum_duration);

    std::chrono::milliseconds maximum_transmission_duration;
};

class TransmissionApplicationService final
{
  public:
    TransmissionApplicationService(const ISessionRepository& sessions,
                                   const IAuthoritativeMembershipProvider& memberships,
                                   ITransmissionIdGenerator& transmission_ids,
                                   IActiveTransmissionRepository& active_transmissions,
                                   ITransmissionRateLimiter& rate_limiter,
                                   const ITransmissionModerationAuthorizer& moderation_authorizer,
                                   TransmissionLifecyclePolicy lifecycle_policy,
                                   ITransmissionAuditEventSink* audit_events = nullptr);

    [[nodiscard]] auto start(const StartTransmissionCommand& command, TimePoint now)
        -> StartTransmissionResult;
    [[nodiscard]] auto end(const EndTransmissionCommand& command, TimePoint now)
        -> EndTransmissionResult;
    [[nodiscard]] auto interruptForModeration(const ModerateTransmissionCommand& command,
                                              TimePoint now) -> ModerateTransmissionResult;
    [[nodiscard]] auto expireTimedOut(TimePoint now, const domain::CorrelationId& correlation_id)
        -> std::vector<EndedTransmission>;

  private:
    const ISessionRepository& sessions_;
    TransmissionAuthorizationService authorization_;
    IActiveTransmissionRepository& active_transmissions_;
    ITransmissionRateLimiter& rate_limiter_;
    const ITransmissionModerationAuthorizer& moderation_authorizer_;
    TransmissionLifecyclePolicy lifecycle_policy_;
    ITransmissionAuditEventSink* audit_events_;
};
} // namespace hvc::application
