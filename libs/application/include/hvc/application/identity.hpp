#pragma once

#include <chrono>
#include <hvc/application/control_plane.hpp>
#include <optional>
#include <string>
#include <utility>

namespace hvc::application
{
struct VerifiedIdentity final
{
    VerifiedIdentity(domain::PlayerId player, std::chrono::milliseconds session_lifetime)
        : player_id(std::move(player)), maximum_session_lifetime(session_lifetime)
    {
    }

    domain::PlayerId player_id;
    std::chrono::milliseconds maximum_session_lifetime;
};

struct IdentityVerificationResult final
{
    [[nodiscard]] static auto verified(VerifiedIdentity identity) -> IdentityVerificationResult;
    [[nodiscard]] static auto rejected(SessionAuthenticationError error)
        -> IdentityVerificationResult;

    [[nodiscard]] auto successful() const noexcept -> bool
    {
        return identity.has_value();
    }

    std::optional<VerifiedIdentity> identity;
    std::optional<SessionAuthenticationError> error;

  private:
    IdentityVerificationResult(std::optional<VerifiedIdentity> verified_identity,
                               std::optional<SessionAuthenticationError> verification_error);
};

class IIdentityProvider
{
  public:
    virtual ~IIdentityProvider() = default;

    // The provider owns credential validation, account state, device policy and throttling.
    [[nodiscard]] virtual auto verify(std::string_view credential,
                                      const domain::DeviceId& device_id, TimePoint now)
        -> IdentityVerificationResult = 0;
};

class ISessionIdGenerator
{
  public:
    virtual ~ISessionIdGenerator() = default;

    [[nodiscard]] virtual auto nextSession() -> domain::SessionId = 0;
};

struct IdentitySessionPolicy final
{
    explicit IdentitySessionPolicy(std::chrono::milliseconds maximum_lifetime);

    std::chrono::milliseconds maximum_session_lifetime;
};

// Production boundary: external identity verification and internal session issuance stay separate.
class IdentitySessionAuthenticator final : public ISessionAuthenticator
{
  public:
    IdentitySessionAuthenticator(IIdentityProvider& identities, ISessionIdGenerator& session_ids,
                                 IdentitySessionPolicy policy) noexcept;

    [[nodiscard]] auto authenticate(const AuthenticateSessionCommand& command, TimePoint now)
        -> SessionAuthenticationResult override;

  private:
    IIdentityProvider& identities_;
    ISessionIdGenerator& session_ids_;
    IdentitySessionPolicy policy_;
};
} // namespace hvc::application
