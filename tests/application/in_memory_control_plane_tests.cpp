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

class ModerationAuthorizer final : public application::ITransmissionModerationAuthorizer
{
  public:
    [[nodiscard]] auto canInterrupt(const domain::PlayerId& moderator_player_id,
                                    const domain::TransmissionId& transmission_id) const
        -> bool override
    {
        static_cast<void>(transmission_id);
        return moderator_player_id == domain::PlayerId{"moderator"};
    }
};

class RecordingAuditEventSink final : public application::ITransmissionAuditEventSink
{
  public:
    void record(const application::TransmissionAuditEvent& event) noexcept override
    {
        try
        {
            events.push_back(event);
        }
        catch (...)
        {
            recording_failed = true;
        }
    }

    std::vector<application::TransmissionAuditEvent> events;
    bool recording_failed{};
};

struct Fixture final
{
    Fixture()
    {
        store.upsertSession({domain::SessionId{"session-sender"}, domain::PlayerId{"sender"},
                             domain::DeviceId{"device-sender"}, now + std::chrono::minutes{5}});
        store.upsertSession({domain::SessionId{"session-other"}, domain::PlayerId{"other"},
                             domain::DeviceId{"device-other"}, now + std::chrono::minutes{5}});
        store.upsertSession({domain::SessionId{"session-moderator"}, domain::PlayerId{"moderator"},
                             domain::DeviceId{"device-moderator"}, now + std::chrono::minutes{5}});
        const auto update =
            store.updateMembership(domain::PlayerId{"sender"}, makeContext(42), now,
                                   domain::CorrelationId{"initial-membership"},
                                   application::AuthoritativeContextChange::membership_changed);
        if (!update.successful())
        {
            throw std::runtime_error{"Could not initialize the membership context."};
        }
    }

    [[nodiscard]] static auto startCommand(std::uint64_t version = 42)
        -> application::StartTransmissionCommand
    {
        return {domain::SessionId{"session-sender"},
                domain::DeviceId{"device-sender"},
                domain::ClientTransmissionId{"client-1"},
                domain::VoiceScope::group,
                version,
                domain::CorrelationId{"start"}};
    }

    [[nodiscard]] static auto endCommand(const domain::TransmissionId& transmission_id)
        -> application::EndTransmissionCommand
    {
        return {domain::SessionId{"session-sender"}, domain::DeviceId{"device-sender"},
                transmission_id, domain::CorrelationId{"end"}};
    }

    application::TimePoint now{application::Clock::time_point{std::chrono::seconds{1000}}};
    RecordingAuditEventSink audit_events;
    application::InMemoryControlPlaneStore store{&audit_events};
    application::InMemoryTransmissionRateLimiter rate_limiter{{100, std::chrono::seconds{10}},
                                                              {100, std::chrono::seconds{10}}};
    ModerationAuthorizer moderation_authorizer;
    application::TransmissionLifecyclePolicy lifecycle_policy{std::chrono::seconds{30}};
    TransmissionIdGenerator ids;
};

auto startsAndEndsAnActiveTransmission() -> bool
{
    Fixture fixture;
    application::TransmissionApplicationService service{
        fixture.store,           fixture.store,        fixture.ids,
        fixture.store,           fixture.rate_limiter, fixture.moderation_authorizer,
        fixture.lifecycle_policy};

    const auto started = service.start(Fixture::startCommand(), fixture.now);
    if (!started.successful() || fixture.store.activeCount() != 1)
    {
        return false;
    }

    const auto transmission_id = started.transmission->authorization.transmission_id;
    const auto duplicate = service.start(Fixture::startCommand(), fixture.now);
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

    const auto ended = service.end(Fixture::endCommand(transmission_id), fixture.now);
    return ended.successful() && ended.transmission &&
           ended.transmission->stop_reason ==
               domain::TransmissionStopReason::push_to_talk_released &&
           fixture.store.activeCount() == 0;
}

auto membershipChangeAtomicallyInterruptsTransmission() -> bool
{
    Fixture fixture;
    application::TransmissionApplicationService service{
        fixture.store,           fixture.store,        fixture.ids,
        fixture.store,           fixture.rate_limiter, fixture.moderation_authorizer,
        fixture.lifecycle_policy};
    const auto started = service.start(Fixture::startCommand(), fixture.now);
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
    application::TransmissionApplicationService service{
        fixture.store,           fixture.store,        fixture.ids,
        fixture.store,           fixture.rate_limiter, fixture.moderation_authorizer,
        fixture.lifecycle_policy};
    const auto started = service.start(Fixture::startCommand(), fixture.now);
    if (!started.successful())
    {
        return false;
    }

    const auto update = fixture.store.updateMembership(
        domain::PlayerId{"sender"}, makeContext(43, "listener"), fixture.now,
        domain::CorrelationId{"permission-change"},
        application::AuthoritativeContextChange::permissions_changed);
    const auto rejected = service.start(Fixture::startCommand(43), fixture.now);

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
    auto authorized = authorization.authorizeStart(Fixture::startCommand(), fixture.now);
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
    application::TransmissionApplicationService service{
        fixture.store,           fixture.store,        fixture.ids,
        fixture.store,           fixture.rate_limiter, fixture.moderation_authorizer,
        fixture.lifecycle_policy};
    const auto started = service.start(Fixture::startCommand(), fixture.now);
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
    auto authorized = authorization.authorizeStart(Fixture::startCommand(), fixture.now);
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

auto rateLimitsStartAndEndRequestsPerPlayer() -> bool
{
    application::InMemoryTransmissionRateLimiter limiter{{2, std::chrono::seconds{10}},
                                                         {1, std::chrono::seconds{5}}};
    const auto now = application::TimePoint{std::chrono::seconds{1000}};
    const domain::PlayerId player{"sender"};

    return limiter.allow(player, application::TransmissionRateLimitAction::start, now) &&
           limiter.allow(player, application::TransmissionRateLimitAction::start,
                         now + std::chrono::seconds{1}) &&
           !limiter.allow(player, application::TransmissionRateLimitAction::start,
                          now + std::chrono::seconds{2}) &&
           limiter.allow(player, application::TransmissionRateLimitAction::end, now) &&
           !limiter.allow(player, application::TransmissionRateLimitAction::end,
                          now + std::chrono::seconds{1}) &&
           limiter.allow(player, application::TransmissionRateLimitAction::start,
                         now + std::chrono::seconds{10}) &&
           limiter.allow(player, application::TransmissionRateLimitAction::end,
                         now + std::chrono::seconds{5});
}

auto applicationServiceReportsRateLimitedRequests() -> bool
{
    Fixture start_fixture;
    application::InMemoryTransmissionRateLimiter start_limiter{{1, std::chrono::seconds{10}},
                                                               {10, std::chrono::seconds{10}}};
    application::TransmissionApplicationService start_service{
        start_fixture.store,           start_fixture.store, start_fixture.ids,
        start_fixture.store,           start_limiter,       start_fixture.moderation_authorizer,
        start_fixture.lifecycle_policy};
    if (start_service.start(Fixture::startCommand(41), start_fixture.now).error !=
            application::StartTransmissionError::voice_membership_stale ||
        start_service.start(Fixture::startCommand(), start_fixture.now).error !=
            application::StartTransmissionError::rate_limited ||
        start_fixture.store.activeCount() != 0)
    {
        return false;
    }

    Fixture end_fixture;
    application::InMemoryTransmissionRateLimiter end_limiter{{10, std::chrono::seconds{10}},
                                                             {1, std::chrono::seconds{10}}};
    application::TransmissionApplicationService end_service{
        end_fixture.store,           end_fixture.store, end_fixture.ids,
        end_fixture.store,           end_limiter,       end_fixture.moderation_authorizer,
        end_fixture.lifecycle_policy};
    const auto started = end_service.start(Fixture::startCommand(), end_fixture.now);
    if (!started.successful())
    {
        return false;
    }

    const application::EndTransmissionCommand missing{
        domain::SessionId{"session-sender"}, domain::DeviceId{"device-sender"},
        domain::TransmissionId{"missing"}, domain::CorrelationId{"missing-end"}};
    const auto missing_result = end_service.end(missing, end_fixture.now);
    const auto limited_result = end_service.end(
        Fixture::endCommand(started.transmission->authorization.transmission_id), end_fixture.now);

    return missing_result.error == application::EndTransmissionError::transmission_not_found &&
           limited_result.error == application::EndTransmissionError::rate_limited &&
           end_fixture.store.activeCount() == 1;
}

auto timeoutExpiresOnlyOverdueTransmissions() -> bool
{
    Fixture fixture;
    application::TransmissionApplicationService service{
        fixture.store,           fixture.store,        fixture.ids,
        fixture.store,           fixture.rate_limiter, fixture.moderation_authorizer,
        fixture.lifecycle_policy};
    const auto started = service.start(Fixture::startCommand(), fixture.now);
    if (!started.successful())
    {
        return false;
    }

    const auto early = service.expireTimedOut(fixture.now + std::chrono::seconds{29},
                                              domain::CorrelationId{"timeout-scan-early"});
    const auto expired = service.expireTimedOut(fixture.now + std::chrono::seconds{30},
                                                domain::CorrelationId{"timeout-scan"});

    return early.empty() && expired.size() == 1 &&
           expired.front().stop_reason == domain::TransmissionStopReason::timed_out &&
           expired.front().correlation_id == domain::CorrelationId{"timeout-scan"} &&
           fixture.store.activeCount() == 0;
}

auto authorizedModerationInterruptsTransmission() -> bool
{
    Fixture fixture;
    application::TransmissionApplicationService service{
        fixture.store,           fixture.store,        fixture.ids,
        fixture.store,           fixture.rate_limiter, fixture.moderation_authorizer,
        fixture.lifecycle_policy};
    const auto started = service.start(Fixture::startCommand(), fixture.now);
    if (!started.successful())
    {
        return false;
    }
    const auto transmission_id = started.transmission->authorization.transmission_id;

    const application::ModerateTransmissionCommand unauthorized{
        domain::SessionId{"session-other"}, domain::DeviceId{"device-other"}, transmission_id,
        domain::CorrelationId{"unauthorized-moderation"}};
    if (service.interruptForModeration(unauthorized, fixture.now).error !=
            application::ModerateTransmissionError::not_authorized ||
        fixture.store.activeCount() != 1)
    {
        return false;
    }

    const application::ModerateTransmissionCommand authorized{
        domain::SessionId{"session-moderator"}, domain::DeviceId{"device-moderator"},
        transmission_id, domain::CorrelationId{"moderation"}};
    const auto interrupted = service.interruptForModeration(authorized, fixture.now);

    return interrupted.successful() &&
           interrupted.transmission->stop_reason ==
               domain::TransmissionStopReason::moderation_interrupted &&
           interrupted.transmission->correlation_id == domain::CorrelationId{"moderation"} &&
           fixture.store.activeCount() == 0;
}

auto emitsStructuredAuditEventsForTheTransmissionLifecycle() -> bool
{
    Fixture fixture;
    application::TransmissionApplicationService service{fixture.store,
                                                        fixture.store,
                                                        fixture.ids,
                                                        fixture.store,
                                                        fixture.rate_limiter,
                                                        fixture.moderation_authorizer,
                                                        fixture.lifecycle_policy,
                                                        &fixture.audit_events};

    const auto started = service.start(Fixture::startCommand(), fixture.now);
    if (!started.successful() || fixture.audit_events.events.size() != 1)
    {
        return false;
    }

    const auto transmission_id = started.transmission->authorization.transmission_id;
    const auto& start_event = fixture.audit_events.events[0];
    if (start_event.type != application::TransmissionAuditEventType::started ||
        start_event.operation != application::TransmissionAuditOperation::start ||
        start_event.transmission_id != transmission_id ||
        start_event.sender_player_id != domain::PlayerId{"sender"} ||
        start_event.actor_player_id != domain::PlayerId{"sender"} ||
        start_event.scope != domain::VoiceScope::group || start_event.membership_version != 42 ||
        start_event.recipient_count != 3 || start_event.rejection_reason || start_event.stop_reason)
    {
        return false;
    }

    const auto duplicate = service.start(Fixture::startCommand(), fixture.now);
    if (duplicate.error != application::StartTransmissionError::sender_already_transmitting ||
        fixture.audit_events.events.size() != 2 ||
        fixture.audit_events.events[1].type != application::TransmissionAuditEventType::rejected ||
        fixture.audit_events.events[1].rejection_reason !=
            application::TransmissionAuditRejectionReason::sender_already_transmitting)
    {
        return false;
    }

    application::EndTransmissionCommand foreign_end{
        domain::SessionId{"session-other"}, domain::DeviceId{"device-other"}, transmission_id,
        domain::CorrelationId{"foreign-end"}};
    const auto foreign = service.end(foreign_end, fixture.now);
    if (foreign.error != application::EndTransmissionError::transmission_not_owned ||
        fixture.audit_events.events.size() != 3 ||
        fixture.audit_events.events[2].operation != application::TransmissionAuditOperation::end ||
        fixture.audit_events.events[2].rejection_reason !=
            application::TransmissionAuditRejectionReason::transmission_not_owned)
    {
        return false;
    }

    const auto ended = service.end(Fixture::endCommand(transmission_id), fixture.now);
    if (!ended.successful() || fixture.audit_events.events.size() != 4 ||
        fixture.audit_events.events[3].type != application::TransmissionAuditEventType::ended ||
        fixture.audit_events.events[3].stop_reason !=
            domain::TransmissionStopReason::push_to_talk_released)
    {
        return false;
    }

    const auto restarted = service.start(Fixture::startCommand(), fixture.now);
    if (!restarted.successful() || fixture.audit_events.events.size() != 5)
    {
        return false;
    }
    const auto update =
        fixture.store.updateMembership(domain::PlayerId{"sender"}, makeContext(43), fixture.now,
                                       domain::CorrelationId{"membership-change"},
                                       application::AuthoritativeContextChange::membership_changed);
    if (!update.successful() || update.interrupted.size() != 1 ||
        fixture.audit_events.events.size() != 6 ||
        fixture.audit_events.events[5].type !=
            application::TransmissionAuditEventType::forcibly_interrupted ||
        fixture.audit_events.events[5].operation !=
            application::TransmissionAuditOperation::membership_change ||
        fixture.audit_events.events[5].stop_reason !=
            domain::TransmissionStopReason::membership_changed ||
        fixture.audit_events.events[5].correlation_id != domain::CorrelationId{"membership-change"})
    {
        return false;
    }

    const auto moderated_start = service.start(Fixture::startCommand(43), fixture.now);
    if (!moderated_start.successful())
    {
        return false;
    }
    const auto moderated_id = moderated_start.transmission->authorization.transmission_id;
    const application::ModerateTransmissionCommand unauthorized{
        domain::SessionId{"session-other"}, domain::DeviceId{"device-other"}, moderated_id,
        domain::CorrelationId{"moderation-rejected"}};
    const application::ModerateTransmissionCommand authorized{
        domain::SessionId{"session-moderator"}, domain::DeviceId{"device-moderator"}, moderated_id,
        domain::CorrelationId{"moderation-accepted"}};
    if (service.interruptForModeration(unauthorized, fixture.now).error !=
            application::ModerateTransmissionError::not_authorized ||
        !service.interruptForModeration(authorized, fixture.now).successful() ||
        fixture.audit_events.events.size() != 9 ||
        fixture.audit_events.events[7].rejection_reason !=
            application::TransmissionAuditRejectionReason::not_authorized ||
        fixture.audit_events.events[7].actor_player_id != domain::PlayerId{"other"} ||
        fixture.audit_events.events[8].type !=
            application::TransmissionAuditEventType::forcibly_interrupted ||
        fixture.audit_events.events[8].operation !=
            application::TransmissionAuditOperation::moderation ||
        fixture.audit_events.events[8].actor_player_id != domain::PlayerId{"moderator"} ||
        fixture.audit_events.events[8].sender_player_id != domain::PlayerId{"sender"})
    {
        return false;
    }

    const auto timeout_start = service.start(Fixture::startCommand(43), fixture.now);
    const auto expired = service.expireTimedOut(fixture.now + std::chrono::seconds{30},
                                                domain::CorrelationId{"timeout"});
    if (!timeout_start.successful() || expired.size() != 1 ||
        fixture.audit_events.events.size() != 11 ||
        fixture.audit_events.events[10].type !=
            application::TransmissionAuditEventType::forcibly_interrupted ||
        fixture.audit_events.events[10].operation !=
            application::TransmissionAuditOperation::timeout ||
        fixture.audit_events.events[10].stop_reason != domain::TransmissionStopReason::timed_out)
    {
        return false;
    }

    const auto disconnect_start = service.start(Fixture::startCommand(43), fixture.now);
    const auto disconnected = fixture.store.removeSession(
        domain::SessionId{"session-sender"}, fixture.now, domain::CorrelationId{"disconnect"});
    return !fixture.audit_events.recording_failed && disconnect_start.successful() &&
           disconnected.size() == 1 && fixture.audit_events.events.size() == 13 &&
           fixture.audit_events.events[12].type ==
               application::TransmissionAuditEventType::forcibly_interrupted &&
           fixture.audit_events.events[12].operation ==
               application::TransmissionAuditOperation::session_lifecycle &&
           fixture.audit_events.events[12].stop_reason ==
               domain::TransmissionStopReason::disconnected;
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
                            sessionChangeDuringStartCannotActivateTransmission() &&
                            rateLimitsStartAndEndRequestsPerPlayer() &&
                            applicationServiceReportsRateLimitedRequests() &&
                            timeoutExpiresOnlyOverdueTransmissions() &&
                            authorizedModerationInterruptsTransmission() &&
                            emitsStructuredAuditEventsForTheTransmissionLifecycle();
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
