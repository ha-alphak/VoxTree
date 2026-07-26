#pragma once

#include <chrono>
#include <hvc/application/control_plane.hpp>
#include <optional>
#include <string>
#include <utility>

namespace hvc::application
{
/// Represent a successfully verified external identity.
struct VerifiedIdentity final
{
    /**
     * Construct a verified identity.
     *
     * @param player Verified participant.
     * @param session_lifetime Maximum session lifetime allowed by the provider.
     */
    VerifiedIdentity(domain::PlayerId player, std::chrono::milliseconds session_lifetime)
        : player_id(std::move(player)), maximum_session_lifetime(session_lifetime)
    {
    }

    /// Verified participant identifier.
    domain::PlayerId player_id;
    /// Provider-imposed upper bound for the issued session.
    std::chrono::milliseconds maximum_session_lifetime;
};

/// Hold either a verified identity or an authentication rejection.
struct IdentityVerificationResult final
{
    /**
     * Create a successful verification result.
     *
     * @param identity Verified identity and provider policy.
     * @returns A result that reports success.
     */
    [[nodiscard]] static auto verified(VerifiedIdentity identity) -> IdentityVerificationResult;
    /**
     * Create a rejected verification result.
     *
     * @param error Public authentication rejection reason.
     * @returns A result that reports failure.
     */
    [[nodiscard]] static auto rejected(SessionAuthenticationError error)
        -> IdentityVerificationResult;

    /**
     * Return whether a verified identity is present.
     *
     * @returns `true` when verification succeeded.
     */
    [[nodiscard]] auto successful() const noexcept -> bool
    {
        return identity.has_value();
    }

    /// Verified identity, absent after rejection.
    std::optional<VerifiedIdentity> identity;
    /// Rejection reason, absent after success.
    std::optional<SessionAuthenticationError> error;

  private:
    IdentityVerificationResult(std::optional<VerifiedIdentity> verified_identity,
                               std::optional<SessionAuthenticationError> verification_error);
};

/**
 * Verify external credentials, account state, device policy, and throttling.
 *
 * Providers own all interpretation of the opaque credential and must not expose
 * credential material through diagnostics.
 */
class IIdentityProvider
{
  public:
    /// Destroy the identity-provider interface.
    virtual ~IIdentityProvider() = default;

    /**
     * Verify an external credential for a device.
     *
     * @param credential Opaque credential supplied by the client.
     * @param device_id Device requesting authentication.
     * @param now Authoritative verification time.
     * @returns A verified identity or a public rejection reason.
     */
    [[nodiscard]] virtual auto verify(std::string_view credential,
                                      const domain::DeviceId& device_id, TimePoint now)
        -> IdentityVerificationResult = 0;
};

/// Generate unique server session identifiers.
class ISessionIdGenerator
{
  public:
    /// Destroy the session-identifier generator interface.
    virtual ~ISessionIdGenerator() = default;

    /**
     * Generate the next session identifier.
     *
     * @returns An identifier unique across retained and active sessions.
     */
    [[nodiscard]] virtual auto nextSession() -> domain::SessionId = 0;
};

/// Configure the server-side upper bound for issued session lifetimes.
struct IdentitySessionPolicy final
{
    /**
     * Construct a session policy.
     *
     * @param maximum_lifetime Positive server-side lifetime limit.
     * @throws std::invalid_argument Thrown when the lifetime is not positive.
     */
    explicit IdentitySessionPolicy(std::chrono::milliseconds maximum_lifetime);

    /// Server-side upper bound for an issued session.
    std::chrono::milliseconds maximum_session_lifetime;
};

/**
 * Issue internal sessions only after external identity verification.
 *
 * External credential verification and internal session issuance remain
 * separate trust boundaries. The referenced collaborators must outlive the
 * authenticator.
 */
class IdentitySessionAuthenticator final : public ISessionAuthenticator
{
  public:
    /**
     * Construct a session authenticator.
     *
     * @param identities External identity provider.
     * @param session_ids Internal session-identifier generator.
     * @param policy Server-side session lifetime policy.
     */
    IdentitySessionAuthenticator(IIdentityProvider& identities, ISessionIdGenerator& session_ids,
                                 IdentitySessionPolicy policy) noexcept;

    /// @copydoc ISessionAuthenticator::authenticate
    [[nodiscard]] auto authenticate(const AuthenticateSessionCommand& command, TimePoint now)
        -> SessionAuthenticationResult override;

  private:
    IIdentityProvider& identities_;
    ISessionIdGenerator& session_ids_;
    IdentitySessionPolicy policy_;
};
} // namespace hvc::application
