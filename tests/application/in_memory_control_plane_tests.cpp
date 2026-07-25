#include <chrono>
#include <cstdio>
#include <exception>
#include <hvc/application/in_memory_control_plane.hpp>
#include <memory>
#include <stdexcept>
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

auto makeContext(std::uint64_t version, std::string sender_role = "leader")
    -> application::AuthoritativeMembershipContext
{
    auto snapshot = std::make_shared<const domain::MembershipSnapshot>(
        version, makeHierarchy(),
        std::vector<domain::VoiceMembership>{
            makeMember("sender", std::move(sender_role)),
            makeMember("recipient", "listener"),
            makeMember("other", "listener"),
        });
    auto policy = std::make_shared<const domain::RolePolicy>(std::vector<domain::RolePermissions>{
        {domain::RoleId{"leader"},
         {domain::VoiceScope::team, domain::VoiceScope::specialization, domain::VoiceScope::group},
         {domain::VoiceScope::team, domain::VoiceScope::specialization, domain::VoiceScope::group}},
        {domain::RoleId{"listener"},
         {},
         {domain::VoiceScope::team, domain::VoiceScope::specialization, domain::VoiceScope::group}},
    });
    return {std::move(snapshot), std::move(policy)};
}

class TransmissionIdGenerator final : public application::ITransmissionIdGenerator
{
  public:
    [[nodiscard]] auto next() -> domain::TransmissionId override
    {
        ++next_id_;
        return domain::TransmissionId{"tx-" + std::to_string(next_id_)};
    }

  private:
    int next_id_{0};
};

struct Fixture final
{
    Fixture()
    {
        store.upsertSession({domain::SessionId{"session-sender"}, domain::PlayerId{"sender"},
                             domain::DeviceId{"device-sender"}, now + std::chrono::minutes{5}});
        store.upsertSession({domain::SessionId{"session-other"}, domain::PlayerId{"other"},
                             domain::DeviceId{"device-other"}, now + std::chrono::minutes{5}});
        const auto update =
            store.updateMembership(domain::PlayerId{"sender"}, makeContext(42), now,
                                   domain::CorrelationId{"initial-membership"},
                                   application::AuthoritativeContextChange::membership_changed);
        if (!update.successful())
        {
            throw std::runtime_error{"Could not initialize the membership context."};
        }
    }

    [[nodiscard]] auto startCommand(std::uint64_t version = 42) const
        -> application::StartTransmissionCommand
    {
        return {domain::SessionId{"session-sender"},
                domain::DeviceId{"device-sender"},
                domain::ClientTransmissionId{"client-1"},
                domain::VoiceScope::group,
                version,
                domain::CorrelationId{"start"}};
    }

    [[nodiscard]] auto endCommand(const domain::TransmissionId& transmission_id) const
        -> application::EndTransmissionCommand
    {
        return {domain::SessionId{"session-sender"}, domain::DeviceId{"device-sender"},
                transmission_id, domain::CorrelationId{"end"}};
    }

    application::TimePoint now{application::Clock::time_point{std::chrono::seconds{1000}}};
    application::InMemoryControlPlaneStore store;
    TransmissionIdGenerator ids;
};

auto startsAndEndsAnActiveTransmission() -> bool
{
    Fixture fixture;
    application::TransmissionApplicationService service{fixture.store, fixture.store, fixture.ids,
                                                        fixture.store};

    const auto started = service.start(fixture.startCommand(), fixture.now);
    if (!started.successful() || fixture.store.activeCount() != 1)
    {
        return false;
    }

    const auto transmission_id = started.transmission->authorization.transmission_id;
    const auto duplicate = service.start(fixture.startCommand(), fixture.now);
    if (duplicate.error != application::StartTransmissionError::sender_already_transmitting)
    {
        return false;
    }

    application::EndTransmissionCommand foreign_end{
        domain::SessionId{"session-other"}, domain::DeviceId{"device-other"}, transmission_id,
        domain::CorrelationId{"foreign-end"}};
    const auto foreign = service.end(foreign_end, fixture.now);
    if (foreign.error != application::EndTransmissionError::transmission_not_owned ||
        fixture.store.activeCount() != 1)
    {
        return false;
    }

    const auto ended = service.end(fixture.endCommand(transmission_id), fixture.now);
    return ended.successful() && ended.transmission &&
           ended.transmission->stop_reason ==
               domain::TransmissionStopReason::push_to_talk_released &&
           fixture.store.activeCount() == 0;
}

auto membershipChangeAtomicallyInterruptsTransmission() -> bool
{
    Fixture fixture;
    application::TransmissionApplicationService service{fixture.store, fixture.store, fixture.ids,
                                                        fixture.store};
    const auto started = service.start(fixture.startCommand(), fixture.now);
    if (!started.successful())
    {
        return false;
    }

    const auto update =
        fixture.store.updateMembership(domain::PlayerId{"sender"}, makeContext(43), fixture.now,
                                       domain::CorrelationId{"membership-change"},
                                       application::AuthoritativeContextChange::membership_changed);

    return update.successful() && update.interrupted.size() == 1 &&
           update.interrupted.front().stop_reason ==
               domain::TransmissionStopReason::membership_changed &&
           fixture.store.activeCount() == 0 &&
           fixture.store.currentFor(domain::PlayerId{"sender"})->snapshot->version() == 43;
}

auto permissionChangeAtomicallyInterruptsAndRevokes() -> bool
{
    Fixture fixture;
    application::TransmissionApplicationService service{fixture.store, fixture.store, fixture.ids,
                                                        fixture.store};
    const auto started = service.start(fixture.startCommand(), fixture.now);
    if (!started.successful())
    {
        return false;
    }

    const auto update = fixture.store.updateMembership(
        domain::PlayerId{"sender"}, makeContext(43, "listener"), fixture.now,
        domain::CorrelationId{"permission-change"},
        application::AuthoritativeContextChange::permissions_changed);
    const auto rejected = service.start(fixture.startCommand(43), fixture.now);

    return update.successful() && update.interrupted.size() == 1 &&
           update.interrupted.front().stop_reason ==
               domain::TransmissionStopReason::permission_revoked &&
           rejected.error == application::StartTransmissionError::voice_scope_not_authorized &&
           fixture.store.activeCount() == 0;
}

auto staleChangesCannotReplaceStateOrLeaveAStaleTransmission() -> bool
{
    Fixture fixture;
    application::TransmissionAuthorizationService authorization{fixture.store, fixture.store,
                                                                fixture.ids};
    auto authorized = authorization.authorizeStart(fixture.startCommand(), fixture.now);
    if (!authorized.authorized())
    {
        return false;
    }

    const auto update =
        fixture.store.updateMembership(domain::PlayerId{"sender"}, makeContext(43), fixture.now,
                                       domain::CorrelationId{"new-membership"},
                                       application::AuthoritativeContextChange::membership_changed);
    const auto activation = fixture.store.activate(std::move(*authorized.transmission),
                                                   domain::SessionId{"session-sender"},
                                                   domain::DeviceId{"device-sender"}, fixture.now);
    const auto stale_update =
        fixture.store.updateMembership(domain::PlayerId{"sender"}, makeContext(42), fixture.now,
                                       domain::CorrelationId{"stale-membership"},
                                       application::AuthoritativeContextChange::membership_changed);

    return update.successful() &&
           activation.error == application::TransmissionActivationError::membership_changed &&
           stale_update.error == application::MembershipUpdateError::version_not_newer &&
           fixture.store.currentFor(domain::PlayerId{"sender"})->snapshot->version() == 43 &&
           fixture.store.activeCount() == 0;
}

auto disconnectAtomicallyInterruptsTransmission() -> bool
{
    Fixture fixture;
    application::TransmissionApplicationService service{fixture.store, fixture.store, fixture.ids,
                                                        fixture.store};
    const auto started = service.start(fixture.startCommand(), fixture.now);
    if (!started.successful())
    {
        return false;
    }

    const auto interrupted = fixture.store.removeSession(
        domain::SessionId{"session-sender"}, fixture.now, domain::CorrelationId{"disconnect"});
    return interrupted.size() == 1 &&
           interrupted.front().stop_reason == domain::TransmissionStopReason::disconnected &&
           !fixture.store.find(domain::SessionId{"session-sender"}) &&
           fixture.store.activeCount() == 0;
}

auto sessionChangeDuringStartCannotActivateTransmission() -> bool
{
    Fixture fixture;
    application::TransmissionAuthorizationService authorization{fixture.store, fixture.store,
                                                                fixture.ids};
    auto authorized = authorization.authorizeStart(fixture.startCommand(), fixture.now);
    if (!authorized.authorized())
    {
        return false;
    }

    static_cast<void>(
        fixture.store.removeSession(domain::SessionId{"session-sender"}, fixture.now,
                                    domain::CorrelationId{"disconnect-before-activation"}));
    const auto activation = fixture.store.activate(std::move(*authorized.transmission),
                                                   domain::SessionId{"session-sender"},
                                                   domain::DeviceId{"device-sender"}, fixture.now);

    return activation.error == application::TransmissionActivationError::session_changed &&
           fixture.store.activeCount() == 0;
}
} // namespace

auto main() noexcept -> int
{
    try
    {
        const bool passed = startsAndEndsAnActiveTransmission() &&
                            membershipChangeAtomicallyInterruptsTransmission() &&
                            permissionChangeAtomicallyInterruptsAndRevokes() &&
                            staleChangesCannotReplaceStateOrLeaveAStaleTransmission() &&
                            disconnectAtomicallyInterruptsTransmission() &&
                            sessionChangeDuringStartCannotActivateTransmission();
        if (!passed)
        {
            std::fputs("in-memory control-plane tests failed\n", stderr);
            return 1;
        }
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "unexpected exception: %s\n", error.what());
        return 1;
    }

    std::puts("in-memory control-plane tests passed");
    return 0;
}
