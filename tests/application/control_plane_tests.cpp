#include <chrono>
#include <cstdio>
#include <exception>
#include <hvc/application/control_plane.hpp>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
using namespace hvc;

auto makeHierarchy() -> domain::Hierarchy
{
    return domain::Hierarchy{domain::HierarchyId{"main"},
                             {
                                 {domain::VoiceScope::team, "Team", 1, 5},
                                 {domain::VoiceScope::specialization, "Specialization", 2, 4},
                                 {domain::VoiceScope::group, "Group", 3, 2},
                             },
                             {{domain::GroupId{"alpha"}, "Alpha"}},
                             {{domain::SpecializationId{"red"}, domain::GroupId{"alpha"}, "Red"}},
                             {{domain::TeamId{"red-1"}, domain::SpecializationId{"red"}, "One"}}};
}

auto makeMember(std::string player, std::string role) -> domain::VoiceMembership
{
    return {domain::PlayerId{std::move(player)},
            domain::GroupId{"alpha"},
            domain::SpecializationId{"red"},
            domain::TeamId{"red-1"},
            {domain::RoleId{std::move(role)}}};
}

class SessionRepository final : public application::ISessionRepository
{
  public:
    std::optional<application::AuthenticatedSession> session;

    [[nodiscard]] auto find(const domain::SessionId& session_id) const
        -> std::optional<application::AuthenticatedSession> override
    {
        if (session && session->session_id == session_id)
        {
            return session;
        }
        return std::nullopt;
    }
};

class MembershipProvider final : public application::IAuthoritativeMembershipProvider
{
  public:
    std::optional<application::AuthoritativeMembershipContext> context;

    [[nodiscard]] auto currentFor(const domain::PlayerId& player_id) const
        -> std::optional<application::AuthoritativeMembershipContext> override
    {
        if (context && context->snapshot->find(player_id) != nullptr)
        {
            return context;
        }
        return std::nullopt;
    }
};

class TransmissionIdGenerator final : public application::ITransmissionIdGenerator
{
  public:
    [[nodiscard]] auto next() -> domain::TransmissionId override
    {
        return domain::TransmissionId{"tx-server-1"};
    }
};

struct Fixture final
{
    Fixture()
    {
        session_repository.session.emplace(domain::SessionId{"session-1"},
                                           domain::PlayerId{"sender"}, domain::DeviceId{"device-1"},
                                           now + std::chrono::minutes{5});

        auto snapshot = std::make_shared<const domain::MembershipSnapshot>(
            42, makeHierarchy(),
            std::vector<domain::VoiceMembership>{
                makeMember("sender", "leader"),
                makeMember("recipient", "listener"),
            });
        auto policy =
            std::make_shared<const domain::RolePolicy>(std::vector<domain::RolePermissions>{
                {domain::RoleId{"leader"},
                 {domain::VoiceScope::team, domain::VoiceScope::specialization,
                  domain::VoiceScope::group},
                 {domain::VoiceScope::team, domain::VoiceScope::specialization,
                  domain::VoiceScope::group}},
                {domain::RoleId{"listener"},
                 {},
                 {domain::VoiceScope::team, domain::VoiceScope::specialization,
                  domain::VoiceScope::group}},
            });
        membership_provider.context.emplace(std::move(snapshot), std::move(policy));
    }

    [[nodiscard]] static auto command(std::uint64_t membership_version = 42)
        -> application::StartTransmissionCommand
    {
        return {domain::SessionId{"session-1"},
                domain::DeviceId{"device-1"},
                domain::ClientTransmissionId{"client-tx-1"},
                domain::VoiceScope::group,
                membership_version,
                domain::CorrelationId{"correlation-1"}};
    }

    application::TimePoint now{application::Clock::time_point{std::chrono::seconds{1000}}};
    SessionRepository session_repository;
    MembershipProvider membership_provider;
    TransmissionIdGenerator id_generator;
};

auto authorizesFromServerSideIdentityAndMembership() -> bool
{
    Fixture fixture;
    application::TransmissionAuthorizationService service{
        fixture.session_repository, fixture.membership_provider, fixture.id_generator};

    const auto result = service.authorizeStart(Fixture::command(), fixture.now);

    return result.authorized() && !result.error && result.transmission &&
           result.transmission->transmission_id == domain::TransmissionId{"tx-server-1"} &&
           result.transmission->sender_player_id == domain::PlayerId{"sender"} &&
           result.transmission->membership_version == 42 &&
           result.transmission->recipients.size() == 2 &&
           result.transmission->correlation_id == domain::CorrelationId{"correlation-1"};
}

auto rejectsInvalidSessionContext() -> bool
{
    Fixture fixture;
    application::TransmissionAuthorizationService service{
        fixture.session_repository, fixture.membership_provider, fixture.id_generator};

    auto unknown_session = Fixture::command();
    unknown_session.session_id = domain::SessionId{"unknown"};
    const auto missing = service.authorizeStart(unknown_session, fixture.now);

    auto wrong_device = Fixture::command();
    wrong_device.device_id = domain::DeviceId{"other-device"};
    const auto mismatched = service.authorizeStart(wrong_device, fixture.now);

    const auto expired =
        service.authorizeStart(Fixture::command(), fixture.now + std::chrono::minutes{10});

    return missing.error == application::TransmissionAuthorizationError::session_not_found &&
           mismatched.error ==
               application::TransmissionAuthorizationError::session_device_mismatch &&
           expired.error == application::TransmissionAuthorizationError::session_expired;
}

auto rejectsStaleAndUnauthorizedRequests() -> bool
{
    Fixture fixture;
    application::TransmissionAuthorizationService service{
        fixture.session_repository, fixture.membership_provider, fixture.id_generator};

    const auto stale = service.authorizeStart(Fixture::command(41), fixture.now);

    fixture.membership_provider.context->snapshot =
        std::make_shared<const domain::MembershipSnapshot>(43, makeHierarchy(),
                                                           std::vector<domain::VoiceMembership>{
                                                               makeMember("sender", "listener"),
                                                               makeMember("recipient", "listener"),
                                                           });
    const auto unauthorized = service.authorizeStart(Fixture::command(43), fixture.now);

    return stale.error == application::TransmissionAuthorizationError::voice_membership_stale &&
           unauthorized.error ==
               application::TransmissionAuthorizationError::voice_scope_not_authorized;
}

auto derivesShortLivedVoiceGrantClaimsFromAuthoritativeState() -> bool
{
    Fixture fixture;
    application::VoiceGrantAuthorizationService grants{
        fixture.session_repository, fixture.membership_provider,
        application::VoiceGrantPolicy{std::chrono::seconds{30}}};

    const auto result =
        grants.derive(domain::SessionId{"session-1"}, domain::DeviceId{"device-1"}, fixture.now);
    return result.successful() && result.claims && result.claims->membership_version == 42 &&
           result.claims->player_id == domain::PlayerId{"sender"} &&
           result.claims->transmit_scopes.size() == 3 &&
           result.claims->receive_scopes.size() == 3 &&
           result.claims->expires_at == fixture.now + std::chrono::seconds{30};
}
} // namespace

auto main() noexcept -> int
{
    try
    {
        const bool passed = authorizesFromServerSideIdentityAndMembership() &&
                            rejectsInvalidSessionContext() &&
                            rejectsStaleAndUnauthorizedRequests() &&
                            derivesShortLivedVoiceGrantClaimsFromAuthoritativeState();
        if (!passed)
        {
            std::fputs("application control-plane tests failed\n", stderr);
            return 1;
        }
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "unexpected exception: %s\n", error.what());
        return 1;
    }

    std::puts("application control-plane tests passed");
    return 0;
}
