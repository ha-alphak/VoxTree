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

/**
 * Coordinate authenticated sessions, authoritative membership, and transmission lifecycles.
 */
namespace hvc::application
{
/// Wall clock used for externally meaningful session and audit timestamps.
using Clock = std::chrono::system_clock;
/// Absolute time point produced by the application wall clock.
using TimePoint = Clock::time_point;

/// Request authentication of one device with an opaque external credential.
struct AuthenticateSessionCommand final
{
    /**
     * Construct an authentication command.
     *
     * @param credential_value Opaque credential for the identity provider.
     * @param device Device requesting the session.
     * @param correlation Correlation identifier for diagnostics and auditing.
     */
    AuthenticateSessionCommand(std::string credential_value, domain::DeviceId device,
                               domain::CorrelationId correlation)
        : credential(std::move(credential_value)), device_id(std::move(device)),
          correlation_id(std::move(correlation))
    {
    }

    /// Opaque external credential; must not be logged.
    std::string credential;
    /// Device to bind to the issued session.
    domain::DeviceId device_id;
    /// Request correlation identifier.
    domain::CorrelationId correlation_id;
};

/// Represent an authenticated, device-bound control-plane session.
struct AuthenticatedSession final
{
    /**
     * Construct an authenticated session.
     *
     * @param session Server-assigned session identifier.
     * @param player Authenticated participant.
     * @param device Device bound to the session.
     * @param expiration Absolute expiration time.
     */
    AuthenticatedSession(domain::SessionId session, domain::PlayerId player,
                         domain::DeviceId device, TimePoint expiration)
        : session_id(std::move(session)), player_id(std::move(player)),
          device_id(std::move(device)), expires_at(expiration)
    {
    }

    /**
     * Determine whether the session is active at a time.
     *
     * @param now Time to evaluate.
     * @returns `true` when `now` is strictly before expiration.
     */
    [[nodiscard]] auto activeAt(TimePoint now) const noexcept -> bool
    {
        return now < expires_at;
    }

    /// Server-assigned session identifier.
    domain::SessionId session_id;
    /// Authenticated participant identifier.
    domain::PlayerId player_id;
    /// Device exclusively bound to the session.
    domain::DeviceId device_id;
    /// Absolute session expiration time.
    TimePoint expires_at;
};

/// Classify a public session-authentication rejection.
enum class SessionAuthenticationError : std::uint8_t
{
    /// The external credential is invalid.
    invalid_credentials,
    /// Device policy does not allow the requesting device.
    device_not_allowed,
    /// The participant account is disabled.
    account_disabled,
    /// Authentication attempts exceeded the provider limit.
    rate_limited
};

/// Hold either an authenticated session or a public rejection reason.
struct SessionAuthenticationResult final
{
    /**
     * Create an accepted authentication result.
     *
     * @param authenticated_session Newly issued session.
     * @returns A result that reports authentication.
     */
    [[nodiscard]] static auto accepted(AuthenticatedSession authenticated_session)
        -> SessionAuthenticationResult;
    /**
     * Create a rejected authentication result.
     *
     * @param authentication_error Public rejection reason.
     * @returns A result that reports rejection.
     */
    [[nodiscard]] static auto rejected(SessionAuthenticationError authentication_error)
        -> SessionAuthenticationResult;

    /**
     * Return whether an authenticated session is present.
     *
     * @returns `true` when authentication succeeded.
     */
    [[nodiscard]] auto authenticated() const noexcept -> bool
    {
        return session.has_value();
    }

    /// Issued session, absent after rejection.
    std::optional<AuthenticatedSession> session;
    /// Rejection reason, absent after acceptance.
    std::optional<SessionAuthenticationError> error;

  private:
    SessionAuthenticationResult(std::optional<AuthenticatedSession> authenticated_session,
                                std::optional<SessionAuthenticationError> authentication_error);
};

/// Authenticate external credentials and issue internal sessions.
class ISessionAuthenticator
{
  public:
    /// Destroy the session-authenticator interface.
    virtual ~ISessionAuthenticator() = default;

    /**
     * Authenticate one session command at an authoritative time.
     *
     * @param command Credential, device, and correlation data.
     * @param now Authoritative authentication time.
     * @returns An issued session or a public rejection reason.
     */
    [[nodiscard]] virtual auto authenticate(const AuthenticateSessionCommand& command,
                                            TimePoint now) -> SessionAuthenticationResult = 0;
};

/// Read authenticated sessions by identifier.
class ISessionRepository
{
  public:
    /// Destroy the session-repository interface.
    virtual ~ISessionRepository() = default;

    /**
     * Find an authenticated session.
     *
     * @param session_id Session identifier to find.
     * @returns A session snapshot, or no value when absent.
     */
    [[nodiscard]] virtual auto find(const domain::SessionId& session_id) const
        -> std::optional<AuthenticatedSession> = 0;
};

/// Persist, replace, and remove authenticated sessions.
class IMutableSessionRepository : public ISessionRepository
{
  public:
    /// Destroy the mutable session-repository interface.
    ~IMutableSessionRepository() override = default;

    /**
     * Insert or replace a session atomically.
     *
     * @param session Session keyed by `session.session_id`.
     */
    virtual void upsert(AuthenticatedSession session) = 0;
    /**
     * Remove a session.
     *
     * @param session_id Session identifier to remove.
     * @returns `true` when a stored session was removed.
     */
    [[nodiscard]] virtual auto erase(const domain::SessionId& session_id) -> bool = 0;
};

/// Couple one immutable membership snapshot with its role policy.
struct AuthoritativeMembershipContext final
{
    /**
     * Construct an authoritative membership context.
     *
     * @param snapshot_value Non-null immutable membership snapshot.
     * @param role_policy_value Non-null immutable role policy.
     * @throws std::invalid_argument Thrown when either pointer is null.
     */
    AuthoritativeMembershipContext(std::shared_ptr<const domain::MembershipSnapshot> snapshot_value,
                                   std::shared_ptr<const domain::RolePolicy> role_policy_value);

    /// Immutable authoritative membership snapshot.
    std::shared_ptr<const domain::MembershipSnapshot> snapshot;
    /// Immutable role policy evaluated with the snapshot.
    std::shared_ptr<const domain::RolePolicy> role_policy;
};

/// Resolve the current authoritative context for a participant.
class IAuthoritativeMembershipProvider
{
  public:
    /// Destroy the membership-provider interface.
    virtual ~IAuthoritativeMembershipProvider() = default;

    /**
     * Resolve authoritative context for a participant.
     *
     * @param player_id Participant to resolve.
     * @returns Current context, or no value when unavailable.
     */
    [[nodiscard]] virtual auto currentFor(const domain::PlayerId& player_id) const
        -> std::optional<AuthoritativeMembershipContext> = 0;
};

/// Classify a rejected authoritative membership write.
enum class AuthoritativeMembershipWriteError : std::uint8_t
{
    /// The target participant is absent from the supplied snapshot.
    player_not_in_snapshot,
    /// The supplied snapshot version is not newer than the stored version.
    version_not_newer
};

/// Persist authoritative contexts while enforcing monotonic versions.
class IMutableAuthoritativeMembershipRepository : public IAuthoritativeMembershipProvider
{
  public:
    /// Destroy the mutable membership-repository interface.
    ~IMutableAuthoritativeMembershipRepository() override = default;

    /**
     * Atomically replace a participant context when its version is newer.
     *
     * @param player_id Participant whose context is stored.
     * @param context Candidate authoritative context.
     * @returns No value after success, otherwise the rejection reason.
     * @note Version comparison and complete-context replacement must occur in
     *     the same atomic operation.
     */
    [[nodiscard]] virtual auto upsertIfNewer(const domain::PlayerId& player_id,
                                             AuthoritativeMembershipContext context)
        -> std::optional<AuthoritativeMembershipWriteError> = 0;
    /**
     * Remove a participant's authoritative context.
     *
     * @param player_id Participant to remove.
     * @returns `true` when a stored context was removed.
     */
    [[nodiscard]] virtual auto erase(const domain::PlayerId& player_id) -> bool = 0;
};

/// Authorize administrative membership reads and mutations.
class IAdministrativeMembershipAuthorizer
{
  public:
    /// Destroy the administrative authorizer interface.
    virtual ~IAdministrativeMembershipAuthorizer() = default;

    /**
     * Determine whether an actor may read a participant's membership.
     *
     * @param actor Administrative participant.
     * @param subject Participant whose membership would be read.
     * @returns `true` when the read is authorized.
     */
    [[nodiscard]] virtual auto canRead(const domain::PlayerId& actor,
                                       const domain::PlayerId& subject) const -> bool = 0;
    /**
     * Determine whether an actor may remove a participant's membership.
     *
     * @param actor Administrative participant.
     * @param subject Participant whose membership would be removed.
     * @returns `true` when removal is authorized.
     */
    [[nodiscard]] virtual auto canRemove(const domain::PlayerId& actor,
                                         const domain::PlayerId& subject) const -> bool = 0;
    /**
     * Determine whether an actor may replace a participant's membership.
     *
     * @param actor Administrative participant.
     * @param subject Participant whose membership would be replaced.
     * @returns `true` when replacement is authorized.
     */
    [[nodiscard]] virtual auto canReplace(const domain::PlayerId& actor,
                                          const domain::PlayerId& subject) const -> bool = 0;
};

/// Configure the server-side maximum lifetime of issued voice grants.
struct VoiceGrantPolicy final
{
    /**
     * Construct a voice-grant policy.
     *
     * @param maximum_lifetime Positive upper bound for grant validity.
     * @throws std::invalid_argument Thrown when the lifetime is not positive.
     */
    explicit VoiceGrantPolicy(std::chrono::milliseconds maximum_lifetime);

    /// Server-side upper bound for grant validity.
    std::chrono::milliseconds lifetime;
};

/// Hold the authoritative claims from which scoped voice grants are issued.
struct VoiceGrantClaims final
{
    /// Participant receiving the grants.
    domain::PlayerId player_id;
    /// Device bound to the authenticated session.
    domain::DeviceId device_id;
    /// Membership version on which grants are based.
    std::uint64_t membership_version;
    /// Participant's group.
    domain::GroupId group_id;
    /// Participant's specialization.
    domain::SpecializationId specialization_id;
    /// Participant's team.
    domain::TeamId team_id;
    /// Scopes in which the participant may publish audio.
    std::vector<domain::VoiceScope> transmit_scopes;
    /// Scopes from which the participant may subscribe to audio.
    std::vector<domain::VoiceScope> receive_scopes;
    /// Absolute grant expiration time.
    TimePoint expires_at;
};

/// Classify why voice-grant authorization failed.
enum class VoiceGrantError : std::uint8_t
{
    /// The referenced session does not exist.
    session_not_found,
    /// The requesting device differs from the session-bound device.
    session_device_mismatch,
    /// The referenced session has expired.
    session_expired,
    /// No authoritative membership context is available.
    membership_unavailable,
    /// The authenticated participant is absent from the snapshot.
    player_not_in_membership,
    /// The membership is not eligible for connected voice.
    voice_not_connected
};

/// Hold either authorized voice-grant claims or a rejection reason.
struct VoiceGrantResult final
{
    /**
     * Return whether authorized claims are present.
     *
     * @returns `true` when authorization succeeded.
     */
    [[nodiscard]] auto successful() const noexcept -> bool
    {
        return claims.has_value();
    }

    /// Authorized claims, absent after rejection.
    std::optional<VoiceGrantClaims> claims;
    /// Rejection reason, absent after success.
    std::optional<VoiceGrantError> error;
};

/// Hold one signed access grant for one isolated voice room.
struct IssuedVoiceRoomGrant final
{
    /// Hierarchy scope carried by the room.
    domain::VoiceScope scope;
    /// Server-controlled room name.
    std::string room_name;
    /// Short-lived signed access token.
    std::string access_token;
};

/// Convert authorized claims into transport-specific signed room grants.
class IVoiceGrantIssuer
{
  public:
    /// Destroy the voice-grant issuer interface.
    virtual ~IVoiceGrantIssuer() = default;

    /**
     * Issue signed room grants from authorized claims.
     *
     * @param claims Authoritative player, membership, scope, and expiry claims.
     * @returns One isolated grant per authorized voice scope.
     */
    [[nodiscard]] virtual auto issue(const VoiceGrantClaims& claims) const
        -> std::vector<IssuedVoiceRoomGrant> = 0;
};

/// Derive least-privilege voice-grant claims from session and membership state.
class VoiceGrantAuthorizationService final
{
  public:
    /**
     * Construct a voice-grant authorization service.
     *
     * @param sessions Session repository that must outlive the service.
     * @param memberships Membership provider that must outlive the service.
     * @param policy Maximum grant lifetime.
     */
    VoiceGrantAuthorizationService(const ISessionRepository& sessions,
                                   const IAuthoritativeMembershipProvider& memberships,
                                   VoiceGrantPolicy policy);

    /**
     * Authorize voice grants for a device-bound session.
     *
     * @param session_id Authenticated session.
     * @param device_id Device presenting the session.
     * @param now Authoritative authorization time.
     * @returns Derived claims or a rejection reason.
     */
    [[nodiscard]] auto derive(const domain::SessionId& session_id,
                              const domain::DeviceId& device_id, TimePoint now) const
        -> VoiceGrantResult;

  private:
    const ISessionRepository& sessions_;
    const IAuthoritativeMembershipProvider& memberships_;
    VoiceGrantPolicy policy_;
};

/// Request authorization to start one voice transmission.
struct StartTransmissionCommand final
{
    /**
     * Construct a start-transmission command.
     *
     * @param session Authenticated session requesting transmission.
     * @param device Device presenting the session.
     * @param client_transmission Client-generated idempotency identifier.
     * @param requested_scope Requested hierarchy scope.
     * @param client_membership_version Membership version held by the client.
     * @param correlation Request correlation identifier.
     */
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

    /// Authenticated session requesting transmission.
    domain::SessionId session_id;
    /// Device presenting the session.
    domain::DeviceId device_id;
    /// Client-generated transmission identifier.
    domain::ClientTransmissionId client_transmission_id;
    /// Requested hierarchy scope.
    domain::VoiceScope scope;
    /// Membership version held by the client.
    std::uint64_t membership_version;
    /// Request correlation identifier.
    domain::CorrelationId correlation_id;
};

/// Classify why a start request could not be authorized.
enum class TransmissionAuthorizationError : std::uint8_t
{
    /// The referenced session does not exist.
    session_not_found,
    /// The requesting device differs from the session-bound device.
    session_device_mismatch,
    /// The referenced session has expired.
    session_expired,
    /// No authoritative membership context is available.
    membership_unavailable,
    /// The sender is not voice-connected.
    voice_not_connected,
    /// The sender is absent from the active snapshot.
    voice_no_active_membership,
    /// The requested scope is undefined.
    voice_scope_not_found,
    /// The sender's roles do not grant the requested scope.
    voice_scope_not_authorized,
    /// The sender is muted or banned.
    voice_transmit_muted,
    /// The client's membership version is obsolete.
    voice_membership_stale
};

/// Capture an immutable authorization decision before atomic activation.
struct AuthorizedTransmission final
{
    /**
     * Construct an authorized transmission.
     *
     * @param transmission Server-assigned transmission identifier.
     * @param client_transmission Client-generated identifier.
     * @param sender Authorized participant.
     * @param authorized_scope Authorized hierarchy scope.
     * @param authorized_membership_version Membership version used for the
     *     decision.
     * @param resolved_recipients Server-resolved authorized recipients.
     * @param correlation Request correlation identifier.
     */
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

    /// Server-assigned transmission identifier.
    domain::TransmissionId transmission_id;
    /// Client-generated transmission identifier.
    domain::ClientTransmissionId client_transmission_id;
    /// Authorized sender.
    domain::PlayerId sender_player_id;
    /// Authorized hierarchy scope.
    domain::VoiceScope scope;
    /// Membership version used for authorization.
    std::uint64_t membership_version;
    /// Server-resolved recipients; never accepted from a client.
    std::vector<domain::PlayerId> recipients;
    /// Request correlation identifier.
    domain::CorrelationId correlation_id;
};

/// Hold either an authorization decision or a rejection reason.
struct TransmissionAuthorizationResult final
{
    /**
     * Create an accepted authorization result.
     *
     * @param authorized_transmission Immutable authorization decision.
     * @returns A result that reports authorization.
     */
    [[nodiscard]] static auto accepted(AuthorizedTransmission authorized_transmission)
        -> TransmissionAuthorizationResult;
    /**
     * Create a rejected authorization result.
     *
     * @param authorization_error Rejection reason.
     * @returns A result that reports rejection.
     */
    [[nodiscard]] static auto rejected(TransmissionAuthorizationError authorization_error)
        -> TransmissionAuthorizationResult;

    /**
     * Return whether an authorization decision is present.
     *
     * @returns `true` when authorization succeeded.
     */
    [[nodiscard]] auto authorized() const noexcept -> bool
    {
        return transmission.has_value();
    }

    /// Authorized transmission, absent after rejection.
    std::optional<AuthorizedTransmission> transmission;
    /// Rejection reason, absent after acceptance.
    std::optional<TransmissionAuthorizationError> error;

  private:
    TransmissionAuthorizationResult(
        std::optional<AuthorizedTransmission> authorized_transmission,
        std::optional<TransmissionAuthorizationError> authorization_error);
};

/// Generate unique server transmission identifiers.
class ITransmissionIdGenerator
{
  public:
    /// Destroy the transmission-identifier generator interface.
    virtual ~ITransmissionIdGenerator() = default;

    /**
     * Generate the next server transmission identifier.
     *
     * @returns An identifier unique across retained and active transmissions.
     */
    [[nodiscard]] virtual auto next() -> domain::TransmissionId = 0;
};

/// Identify the operation checked by transmission rate limiting.
enum class TransmissionRateLimitAction : std::uint8_t
{
    /// Starting a transmission.
    start,
    /// Ending a transmission.
    end
};

/// Enforce per-participant transmission-operation limits.
class ITransmissionRateLimiter
{
  public:
    /// Destroy the transmission rate-limiter interface.
    virtual ~ITransmissionRateLimiter() = default;

    /**
     * Consume permission for one transmission operation.
     *
     * @param player_id Participant performing the operation.
     * @param action Operation being limited.
     * @param now Authoritative operation time.
     * @returns `true` when the operation remains within its limit.
     */
    [[nodiscard]] virtual auto allow(const domain::PlayerId& player_id,
                                     TransmissionRateLimitAction action, TimePoint now) -> bool = 0;
};

/// Authorize transmission starts from session, membership, and routing state.
class TransmissionAuthorizationService final
{
  public:
    /**
     * Construct a transmission authorization service.
     *
     * @param sessions Session repository that must outlive the service.
     * @param memberships Membership provider that must outlive the service.
     * @param transmission_ids Identifier generator that must outlive the service.
     */
    TransmissionAuthorizationService(const ISessionRepository& sessions,
                                     const IAuthoritativeMembershipProvider& memberships,
                                     ITransmissionIdGenerator& transmission_ids);

    /**
     * Authorize a start command and resolve recipients.
     *
     * @param command Device-bound start request.
     * @param now Authoritative authorization time.
     * @returns Immutable authorization or a rejection reason.
     */
    [[nodiscard]] auto authorizeStart(const StartTransmissionCommand& command, TimePoint now)
        -> TransmissionAuthorizationResult;

  private:
    const ISessionRepository& sessions_;
    const IAuthoritativeMembershipProvider& memberships_;
    ITransmissionIdGenerator& transmission_ids_;
};

/// Classify why an authorization could not be atomically activated.
enum class TransmissionActivationError : std::uint8_t
{
    /// Session state changed after authorization.
    session_changed,
    /// Membership state changed after authorization.
    membership_changed,
    /// The sender already has an active transmission.
    sender_already_transmitting,
    /// The server transmission identifier is already active.
    transmission_id_conflict
};

/// Represent one atomically activated transmission.
struct ActiveTransmission final
{
    /**
     * Construct an active transmission.
     *
     * @param authorized_transmission Immutable authorization decision.
     * @param session Owning authenticated session.
     * @param device Owning session-bound device.
     * @param start_time Authoritative activation time.
     */
    ActiveTransmission(AuthorizedTransmission authorized_transmission, domain::SessionId session,
                       domain::DeviceId device, TimePoint start_time)
        : authorization(std::move(authorized_transmission)), session_id(std::move(session)),
          device_id(std::move(device)), started_at(start_time)
    {
    }

    /// Immutable authorization decision.
    AuthorizedTransmission authorization;
    /// Session that owns the transmission.
    domain::SessionId session_id;
    /// Device that owns the transmission.
    domain::DeviceId device_id;
    /// Authoritative activation time.
    TimePoint started_at;
};

/// Hold either an activated transmission or an activation rejection.
struct TransmissionActivationResult final
{
    /**
     * Create a successful activation result.
     *
     * @param active_transmission Newly active transmission.
     * @returns A result that reports activation.
     */
    [[nodiscard]] static auto activated(ActiveTransmission active_transmission)
        -> TransmissionActivationResult;
    /**
     * Create a rejected activation result.
     *
     * @param activation_error Atomic validation failure.
     * @returns A result that reports rejection.
     */
    [[nodiscard]] static auto rejected(TransmissionActivationError activation_error)
        -> TransmissionActivationResult;

    /**
     * Return whether an active transmission is present.
     *
     * @returns `true` when activation succeeded.
     */
    [[nodiscard]] auto active() const noexcept -> bool
    {
        return transmission.has_value();
    }

    /// Active transmission, absent after rejection.
    std::optional<ActiveTransmission> transmission;
    /// Activation rejection, absent after success.
    std::optional<TransmissionActivationError> error;

  private:
    TransmissionActivationResult(std::optional<ActiveTransmission> active_transmission,
                                 std::optional<TransmissionActivationError> activation_error);
};

/// Represent an immutable record of a completed transmission.
struct EndedTransmission final
{
    /**
     * Construct an ended transmission.
     *
     * @param active_transmission Transmission state before termination.
     * @param transmission_stop_reason Non-`none` termination reason.
     * @param end_time Authoritative termination time.
     * @param correlation Correlation identifier of the terminating operation.
     */
    EndedTransmission(ActiveTransmission active_transmission,
                      domain::TransmissionStopReason transmission_stop_reason, TimePoint end_time,
                      domain::CorrelationId correlation)
        : transmission(std::move(active_transmission)), stop_reason(transmission_stop_reason),
          ended_at(end_time), correlation_id(std::move(correlation))
    {
    }

    /// Transmission state captured before termination.
    ActiveTransmission transmission;
    /// Reason the transmission stopped.
    domain::TransmissionStopReason stop_reason;
    /// Authoritative termination time.
    TimePoint ended_at;
    /// Correlation identifier of the terminating operation.
    domain::CorrelationId correlation_id;
};

/// Membership-write rejection exposed by administrative update operations.
using MembershipUpdateError = AuthoritativeMembershipWriteError;

/// Hold the outcome and interruptions produced by a membership update.
struct MembershipUpdateResult final
{
    /**
     * Create a successful membership-update result.
     *
     * @param interrupted_transmissions Transmissions stopped by the change.
     * @returns A result that reports success.
     */
    [[nodiscard]] static auto updated(std::vector<EndedTransmission> interrupted_transmissions)
        -> MembershipUpdateResult;
    /**
     * Create a rejected membership-update result.
     *
     * @param update_error Rejection reason.
     * @returns A result that reports failure.
     */
    [[nodiscard]] static auto rejected(MembershipUpdateError update_error)
        -> MembershipUpdateResult;

    /**
     * Return whether the authoritative update succeeded.
     *
     * @returns `true` when the update was applied.
     */
    [[nodiscard]] auto successful() const noexcept -> bool
    {
        return !error.has_value();
    }

    /// Transmissions interrupted by the successful update.
    std::vector<EndedTransmission> interrupted;
    /// Rejection reason, absent after success.
    std::optional<MembershipUpdateError> error;

  private:
    MembershipUpdateResult(std::vector<EndedTransmission> interrupted_transmissions,
                           std::optional<MembershipUpdateError> update_error);
};

/// Administratively replace or remove authoritative participant membership.
class IAdministrativeMembershipService : public IAuthoritativeMembershipProvider
{
  public:
    /// Destroy the administrative membership-service interface.
    ~IAdministrativeMembershipService() override = default;

    /**
     * Atomically replace membership and interrupt invalidated transmissions.
     *
     * @param player_id Participant whose context is replaced.
     * @param context New authoritative membership and role policy.
     * @param now Authoritative update time.
     * @param correlation_id Correlation identifier for generated audit events.
     * @returns Update outcome and interrupted transmissions.
     */
    [[nodiscard]] virtual auto replaceMembership(const domain::PlayerId& player_id,
                                                 AuthoritativeMembershipContext context,
                                                 TimePoint now,
                                                 const domain::CorrelationId& correlation_id)
        -> MembershipUpdateResult = 0;
    /**
     * Remove membership and interrupt the participant's transmissions.
     *
     * @param player_id Participant whose context is removed.
     * @param now Authoritative removal time.
     * @param correlation_id Correlation identifier for generated audit events.
     * @returns Transmissions interrupted by the removal.
     */
    [[nodiscard]] virtual auto removeMembership(const domain::PlayerId& player_id, TimePoint now,
                                                const domain::CorrelationId& correlation_id)
        -> std::vector<EndedTransmission> = 0;
};

/// Classify a rejected repository termination request.
enum class TransmissionEndRepositoryError : std::uint8_t
{
    /// The referenced active transmission does not exist.
    transmission_not_found,
    /// The supplied session or device does not own the transmission.
    transmission_not_owned
};

/// Hold either an ended transmission or a repository rejection.
struct TransmissionEndRepositoryResult final
{
    /**
     * Create a successful repository-end result.
     *
     * @param ended_transmission Immutable completion record.
     * @returns A result that reports success.
     */
    [[nodiscard]] static auto ended(EndedTransmission ended_transmission)
        -> TransmissionEndRepositoryResult;
    /**
     * Create a rejected repository-end result.
     *
     * @param repository_error Rejection reason.
     * @returns A result that reports failure.
     */
    [[nodiscard]] static auto rejected(TransmissionEndRepositoryError repository_error)
        -> TransmissionEndRepositoryResult;

    /**
     * Return whether a completion record is present.
     *
     * @returns `true` when the repository operation succeeded.
     */
    [[nodiscard]] auto successful() const noexcept -> bool
    {
        return transmission.has_value();
    }

    /// Ended transmission, absent after rejection.
    std::optional<EndedTransmission> transmission;
    /// Rejection reason, absent after success.
    std::optional<TransmissionEndRepositoryError> error;

  private:
    TransmissionEndRepositoryResult(std::optional<EndedTransmission> ended_transmission,
                                    std::optional<TransmissionEndRepositoryError> repository_error);
};

/// Atomically activate, end, interrupt, and expire transmissions.
class IActiveTransmissionRepository
{
  public:
    /// Destroy the active-transmission repository interface.
    virtual ~IActiveTransmissionRepository() = default;

    /**
     * Atomically validate and activate an authorized transmission.
     *
     * @param transmission Immutable authorization decision.
     * @param session_id Session claiming the transmission.
     * @param device_id Device claiming the transmission.
     * @param started_at Authoritative activation time.
     * @returns Activated transmission or an atomic validation rejection.
     * @note Session, device, and membership version must be verified in the same
     *     atomic operation that makes the transmission visible.
     */
    [[nodiscard]] virtual auto activate(AuthorizedTransmission transmission,
                                        const domain::SessionId& session_id,
                                        const domain::DeviceId& device_id, TimePoint started_at)
        -> TransmissionActivationResult = 0;
    /**
     * End a transmission owned by a session and device.
     *
     * @param transmission_id Active transmission to end.
     * @param session_id Claiming session.
     * @param device_id Claiming device.
     * @param stop_reason Non-`none` termination reason.
     * @param ended_at Authoritative termination time.
     * @param correlation_id Correlation identifier of the request.
     * @returns Completion record or ownership/not-found rejection.
     */
    [[nodiscard]] virtual auto end(const domain::TransmissionId& transmission_id,
                                   const domain::SessionId& session_id,
                                   const domain::DeviceId& device_id,
                                   domain::TransmissionStopReason stop_reason, TimePoint ended_at,
                                   const domain::CorrelationId& correlation_id)
        -> TransmissionEndRepositoryResult = 0;
    /**
     * Forcibly interrupt a transmission without ownership matching.
     *
     * @param transmission_id Active transmission to interrupt.
     * @param stop_reason Non-`none` server-enforced reason.
     * @param ended_at Authoritative interruption time.
     * @param correlation_id Correlation identifier of the operation.
     * @returns Completion record or a not-found rejection.
     */
    [[nodiscard]] virtual auto interrupt(const domain::TransmissionId& transmission_id,
                                         domain::TransmissionStopReason stop_reason,
                                         TimePoint ended_at,
                                         const domain::CorrelationId& correlation_id)
        -> TransmissionEndRepositoryResult = 0;
    /**
     * Expire all transmissions exceeding a duration.
     *
     * @param maximum_duration Positive maximum active duration.
     * @param now Authoritative sweep time.
     * @param correlation_id Correlation identifier for generated audit events.
     * @returns Completion records for all transmissions expired by the sweep.
     */
    [[nodiscard]] virtual auto expireTimedOut(std::chrono::milliseconds maximum_duration,
                                              TimePoint now,
                                              const domain::CorrelationId& correlation_id)
        -> std::vector<EndedTransmission> = 0;
};

/// Classify any rejection from the complete start-transmission workflow.
enum class StartTransmissionError : std::uint8_t
{
    /// The referenced session does not exist.
    session_not_found,
    /// The requesting device differs from the session-bound device.
    session_device_mismatch,
    /// The referenced session has expired.
    session_expired,
    /// No authoritative membership context is available.
    membership_unavailable,
    /// The sender is not voice-connected.
    voice_not_connected,
    /// The sender is absent from the active membership snapshot.
    voice_no_active_membership,
    /// The requested scope is undefined.
    voice_scope_not_found,
    /// The sender lacks permission for the requested scope.
    voice_scope_not_authorized,
    /// The sender is muted or banned.
    voice_transmit_muted,
    /// The client used an obsolete membership version.
    voice_membership_stale,
    /// Session state changed before atomic activation.
    session_changed_during_start,
    /// Membership changed before atomic activation.
    membership_changed_during_start,
    /// The sender already has an active transmission.
    sender_already_transmitting,
    /// The generated server transmission identifier already exists.
    transmission_id_conflict,
    /// The participant exceeded the start-operation rate limit.
    rate_limited
};

/// Hold either a newly active transmission or a workflow rejection.
struct StartTransmissionResult final
{
    /**
     * Create a successful start result.
     *
     * @param active_transmission Newly active transmission.
     * @returns A result that reports success.
     */
    [[nodiscard]] static auto started(ActiveTransmission active_transmission)
        -> StartTransmissionResult;
    /**
     * Create a rejected start result.
     *
     * @param start_error Workflow rejection reason.
     * @returns A result that reports failure.
     */
    [[nodiscard]] static auto rejected(StartTransmissionError start_error)
        -> StartTransmissionResult;

    /**
     * Return whether an active transmission is present.
     *
     * @returns `true` when the start workflow succeeded.
     */
    [[nodiscard]] auto successful() const noexcept -> bool
    {
        return transmission.has_value();
    }

    /// Active transmission, absent after rejection.
    std::optional<ActiveTransmission> transmission;
    /// Workflow rejection, absent after success.
    std::optional<StartTransmissionError> error;

  private:
    StartTransmissionResult(std::optional<ActiveTransmission> active_transmission,
                            std::optional<StartTransmissionError> start_error);
};

/// Request owner-authorized termination of an active transmission.
struct EndTransmissionCommand final
{
    /**
     * Construct an end-transmission command.
     *
     * @param session Authenticated session requesting termination.
     * @param device Device presenting the session.
     * @param transmission Active server transmission identifier.
     * @param correlation Request correlation identifier.
     */
    EndTransmissionCommand(domain::SessionId session, domain::DeviceId device,
                           domain::TransmissionId transmission, domain::CorrelationId correlation)
        : session_id(std::move(session)), device_id(std::move(device)),
          transmission_id(std::move(transmission)), correlation_id(std::move(correlation))
    {
    }

    /// Authenticated session requesting termination.
    domain::SessionId session_id;
    /// Device presenting the session.
    domain::DeviceId device_id;
    /// Active server transmission identifier.
    domain::TransmissionId transmission_id;
    /// Request correlation identifier.
    domain::CorrelationId correlation_id;
};

/// Classify a rejection from the complete end-transmission workflow.
enum class EndTransmissionError : std::uint8_t
{
    /// The referenced session does not exist.
    session_not_found,
    /// The requesting device differs from the session-bound device.
    session_device_mismatch,
    /// The referenced session has expired.
    session_expired,
    /// The referenced active transmission does not exist.
    transmission_not_found,
    /// The session or device does not own the transmission.
    transmission_not_owned,
    /// The participant exceeded the end-operation rate limit.
    rate_limited
};

/// Hold either an ended transmission or an end-workflow rejection.
struct EndTransmissionResult final
{
    /**
     * Create a successful end result.
     *
     * @param ended_transmission Immutable completion record.
     * @returns A result that reports success.
     */
    [[nodiscard]] static auto ended(EndedTransmission ended_transmission) -> EndTransmissionResult;
    /**
     * Create a rejected end result.
     *
     * @param end_error Workflow rejection reason.
     * @returns A result that reports failure.
     */
    [[nodiscard]] static auto rejected(EndTransmissionError end_error) -> EndTransmissionResult;

    /**
     * Return whether a completion record is present.
     *
     * @returns `true` when the end workflow succeeded.
     */
    [[nodiscard]] auto successful() const noexcept -> bool
    {
        return transmission.has_value();
    }

    /// Ended transmission, absent after rejection.
    std::optional<EndedTransmission> transmission;
    /// Workflow rejection, absent after success.
    std::optional<EndTransmissionError> error;

  private:
    EndTransmissionResult(std::optional<EndedTransmission> ended_transmission,
                          std::optional<EndTransmissionError> end_error);
};

/// Authorize moderators to interrupt individual transmissions.
class ITransmissionModerationAuthorizer
{
  public:
    /// Destroy the moderation-authorizer interface.
    virtual ~ITransmissionModerationAuthorizer() = default;

    /**
     * Determine whether a participant may interrupt a transmission.
     *
     * @param moderator_player_id Participant requesting moderation.
     * @param transmission_id Transmission targeted for interruption.
     * @returns `true` when interruption is authorized.
     */
    [[nodiscard]] virtual auto canInterrupt(const domain::PlayerId& moderator_player_id,
                                            const domain::TransmissionId& transmission_id) const
        -> bool = 0;
};

/// Request moderation interruption through an authenticated session.
struct ModerateTransmissionCommand final
{
    /**
     * Construct a moderation command.
     *
     * @param session Moderator's authenticated session.
     * @param device Device presenting the session.
     * @param transmission Active transmission to interrupt.
     * @param correlation Request correlation identifier.
     */
    ModerateTransmissionCommand(domain::SessionId session, domain::DeviceId device,
                                domain::TransmissionId transmission,
                                domain::CorrelationId correlation)
        : session_id(std::move(session)), device_id(std::move(device)),
          transmission_id(std::move(transmission)), correlation_id(std::move(correlation))
    {
    }

    /// Moderator's authenticated session.
    domain::SessionId session_id;
    /// Device presenting the session.
    domain::DeviceId device_id;
    /// Active transmission targeted for interruption.
    domain::TransmissionId transmission_id;
    /// Request correlation identifier.
    domain::CorrelationId correlation_id;
};

/// Classify a rejection from the moderation workflow.
enum class ModerateTransmissionError : std::uint8_t
{
    /// The referenced session does not exist.
    session_not_found,
    /// The requesting device differs from the session-bound device.
    session_device_mismatch,
    /// The referenced session has expired.
    session_expired,
    /// The participant is not authorized to moderate the transmission.
    not_authorized,
    /// The referenced active transmission does not exist.
    transmission_not_found
};

/// Hold either an interrupted transmission or a moderation rejection.
struct ModerateTransmissionResult final
{
    /**
     * Create a successful moderation result.
     *
     * @param ended_transmission Interrupted transmission record.
     * @returns A result that reports success.
     */
    [[nodiscard]] static auto interrupted(EndedTransmission ended_transmission)
        -> ModerateTransmissionResult;
    /**
     * Create a rejected moderation result.
     *
     * @param moderation_error Workflow rejection reason.
     * @returns A result that reports failure.
     */
    [[nodiscard]] static auto rejected(ModerateTransmissionError moderation_error)
        -> ModerateTransmissionResult;

    /**
     * Return whether an interrupted transmission is present.
     *
     * @returns `true` when moderation succeeded.
     */
    [[nodiscard]] auto successful() const noexcept -> bool
    {
        return transmission.has_value();
    }

    /// Interrupted transmission, absent after rejection.
    std::optional<EndedTransmission> transmission;
    /// Moderation rejection, absent after success.
    std::optional<ModerateTransmissionError> error;

  private:
    ModerateTransmissionResult(std::optional<EndedTransmission> ended_transmission,
                               std::optional<ModerateTransmissionError> moderation_error);
};

/// Configure the maximum lifetime of an active transmission.
struct TransmissionLifecyclePolicy final
{
    /**
     * Construct a transmission lifecycle policy.
     *
     * @param maximum_duration Positive maximum active duration.
     * @throws std::invalid_argument Thrown when the duration is not positive.
     */
    explicit TransmissionLifecyclePolicy(std::chrono::milliseconds maximum_duration);

    /// Maximum duration before a transmission is forcibly expired.
    std::chrono::milliseconds maximum_transmission_duration;
};

/**
 * Coordinate authorization, activation, termination, moderation, and auditing.
 *
 * The service owns no collaborators. Start authorization is revalidated
 * atomically during repository activation, preventing stale session or
 * membership decisions from becoming visible.
 */
class TransmissionApplicationService final
{
  public:
    /**
     * Construct a transmission application service.
     *
     * @param sessions Session repository.
     * @param memberships Authoritative membership provider.
     * @param transmission_ids Server identifier generator.
     * @param active_transmissions Atomic lifecycle repository.
     * @param rate_limiter Per-participant operation rate limiter.
     * @param moderation_authorizer Moderation authorization policy.
     * @param lifecycle_policy Maximum active duration.
     * @param audit_events Optional non-owning audit sink.
     */
    TransmissionApplicationService(const ISessionRepository& sessions,
                                   const IAuthoritativeMembershipProvider& memberships,
                                   ITransmissionIdGenerator& transmission_ids,
                                   IActiveTransmissionRepository& active_transmissions,
                                   ITransmissionRateLimiter& rate_limiter,
                                   const ITransmissionModerationAuthorizer& moderation_authorizer,
                                   TransmissionLifecyclePolicy lifecycle_policy,
                                   ITransmissionAuditEventSink* audit_events = nullptr);

    /**
     * Authorize and atomically activate a transmission.
     *
     * @param command Device-bound start request.
     * @param now Authoritative operation time.
     * @returns Active transmission or a workflow rejection.
     */
    [[nodiscard]] auto start(const StartTransmissionCommand& command, TimePoint now)
        -> StartTransmissionResult;
    /**
     * End a transmission owned by the authenticated session and device.
     *
     * @param command Device-bound end request.
     * @param now Authoritative operation time.
     * @returns Completion record or a workflow rejection.
     */
    [[nodiscard]] auto end(const EndTransmissionCommand& command, TimePoint now)
        -> EndTransmissionResult;
    /**
     * Interrupt a transmission after moderator authorization.
     *
     * @param command Device-bound moderation request.
     * @param now Authoritative operation time.
     * @returns Interrupted transmission or a workflow rejection.
     */
    [[nodiscard]] auto interruptForModeration(const ModerateTransmissionCommand& command,
                                              TimePoint now) -> ModerateTransmissionResult;
    /**
     * Expire transmissions exceeding the lifecycle duration.
     *
     * @param now Authoritative sweep time.
     * @param correlation_id Correlation identifier for generated audit events.
     * @returns Completion records for all expired transmissions.
     */
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
