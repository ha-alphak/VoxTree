#include <chrono>
#include <cstdio>
#include <exception>
#include <hvc/application/identity.hpp>
#include <string_view>

namespace
{
using namespace hvc;

class IdentityProvider final : public application::IIdentityProvider
{
  public:
    application::IdentityVerificationResult result =
        application::IdentityVerificationResult::verified(
            {domain::PlayerId{"player-42"}, std::chrono::minutes{30}});
    std::string observed_credential;
    domain::DeviceId observed_device{"unset"};

    [[nodiscard]] auto verify(std::string_view credential, const domain::DeviceId& device_id,
                              application::TimePoint)
        -> application::IdentityVerificationResult override
    {
        observed_credential = credential;
        observed_device = device_id;
        return result;
    }
};

class SessionIds final : public application::ISessionIdGenerator
{
  public:
    [[nodiscard]] auto nextSession() -> domain::SessionId override
    {
        ++calls;
        return domain::SessionId{"session-issued"};
    }

    int calls{};
};

auto issuesBoundedDeviceSession() -> bool
{
    IdentityProvider identities;
    SessionIds session_ids;
    application::IdentitySessionAuthenticator authenticator{
        identities, session_ids, application::IdentitySessionPolicy{std::chrono::minutes{15}}};
    const auto now = application::TimePoint{std::chrono::seconds{100}};

    const auto result =
        authenticator.authenticate({"opaque-external-token", domain::DeviceId{"desktop-1"},
                                    domain::CorrelationId{"correlation-1"}},
                                   now);

    return result.authenticated() && result.session &&
           result.session->session_id == domain::SessionId{"session-issued"} &&
           result.session->player_id == domain::PlayerId{"player-42"} &&
           result.session->device_id == domain::DeviceId{"desktop-1"} &&
           result.session->expires_at == now + std::chrono::minutes{15} &&
           identities.observed_credential == "opaque-external-token" &&
           identities.observed_device == domain::DeviceId{"desktop-1"} && session_ids.calls == 1;
}

auto preservesProviderRejection() -> bool
{
    IdentityProvider identities;
    identities.result = application::IdentityVerificationResult::rejected(
        application::SessionAuthenticationError::account_disabled);
    SessionIds session_ids;
    application::IdentitySessionAuthenticator authenticator{
        identities, session_ids, application::IdentitySessionPolicy{std::chrono::minutes{15}}};

    const auto result = authenticator.authenticate(
        {"credential", domain::DeviceId{"device"}, domain::CorrelationId{"correlation"}},
        application::TimePoint{});

    return !result.authenticated() &&
           result.error == application::SessionAuthenticationError::account_disabled &&
           session_ids.calls == 0;
}

auto rejectsInvalidLifetimes() -> bool
{
    bool policy_rejected{};
    try
    {
        static_cast<void>(application::IdentitySessionPolicy{std::chrono::milliseconds::zero()});
    }
    catch (const std::invalid_argument&)
    {
        policy_rejected = true;
    }
    if (!policy_rejected)
    {
        return false;
    }

    try
    {
        static_cast<void>(application::IdentityVerificationResult::verified(
            {domain::PlayerId{"player"}, std::chrono::milliseconds::zero()}));
        return false;
    }
    catch (const std::invalid_argument&)
    {
        return true;
    }
}
} // namespace

auto main() -> int
{
    try
    {
        const bool passed = issuesBoundedDeviceSession() && preservesProviderRejection() &&
                            rejectsInvalidLifetimes();
        std::puts(passed ? "identity tests passed" : "identity tests failed");
        return passed ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "identity tests threw: %s\n", error.what());
        return 1;
    }
}
