#include <algorithm>
#include <hvc/application/identity.hpp>
#include <stdexcept>

namespace hvc::application
{
IdentityVerificationResult::IdentityVerificationResult(
    std::optional<VerifiedIdentity> verified_identity,
    std::optional<SessionAuthenticationError> verification_error)
    : identity(std::move(verified_identity)), error(verification_error)
{
    if (identity.has_value() == error.has_value())
    {
        throw std::invalid_argument{
            "An identity verification result must contain either an identity or an error."};
    }
    if (identity && identity->maximum_session_lifetime <= std::chrono::milliseconds::zero())
    {
        throw std::invalid_argument{"A verified identity requires a positive session lifetime."};
    }
}

auto IdentityVerificationResult::verified(VerifiedIdentity identity) -> IdentityVerificationResult
{
    return IdentityVerificationResult{std::move(identity), std::nullopt};
}

auto IdentityVerificationResult::rejected(SessionAuthenticationError error)
    -> IdentityVerificationResult
{
    return IdentityVerificationResult{std::nullopt, error};
}

IdentitySessionPolicy::IdentitySessionPolicy(std::chrono::milliseconds maximum_lifetime)
    : maximum_session_lifetime(maximum_lifetime)
{
    if (maximum_session_lifetime <= std::chrono::milliseconds::zero())
    {
        throw std::invalid_argument{"The maximum session lifetime must be positive."};
    }
}

IdentitySessionAuthenticator::IdentitySessionAuthenticator(IIdentityProvider& identities,
                                                           ISessionIdGenerator& session_ids,
                                                           IdentitySessionPolicy policy) noexcept
    : identities_(identities), session_ids_(session_ids), policy_(policy)
{
}

auto IdentitySessionAuthenticator::authenticate(const AuthenticateSessionCommand& command,
                                                TimePoint now) -> SessionAuthenticationResult
{
    auto verification = identities_.verify(command.credential, command.device_id, now);
    if (!verification.successful())
    {
        return SessionAuthenticationResult::rejected(*verification.error);
    }

    const auto lifetime =
        std::min(verification.identity->maximum_session_lifetime, policy_.maximum_session_lifetime);
    return SessionAuthenticationResult::accepted(
        AuthenticatedSession{session_ids_.nextSession(), verification.identity->player_id,
                             command.device_id, now + lifetime});
}
} // namespace hvc::application
