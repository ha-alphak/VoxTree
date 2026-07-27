#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <exception>
#include <hvc/application/in_memory_control_plane.hpp>
#include <hvc/network/control_plane_http.hpp>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
namespace application = hvc::application;
namespace domain = hvc::domain;
namespace network = hvc::network;

class SessionRepository final : public application::IMutableSessionRepository
{
  public:
    void upsert(application::AuthenticatedSession session) override
    {
        sessions.insert_or_assign(session.session_id, std::move(session));
    }

    [[nodiscard]] auto erase(const domain::SessionId& session_id) -> bool override
    {
        return sessions.erase(session_id) != 0;
    }

    [[nodiscard]] auto find(const domain::SessionId& session_id) const
        -> std::optional<application::AuthenticatedSession> override
    {
        const auto session = sessions.find(session_id);
        return session == sessions.end()
                   ? std::nullopt
                   : std::optional<application::AuthenticatedSession>{session->second};
    }

    std::map<domain::SessionId, application::AuthenticatedSession> sessions;
};

class Authenticator final : public application::ISessionAuthenticator
{
  public:
    [[nodiscard]] auto authenticate(const application::AuthenticateSessionCommand& command,
                                    application::TimePoint now)
        -> application::SessionAuthenticationResult override
    {
        credential = command.credential;
        device_id = command.device_id;
        correlation_id = command.correlation_id;
        if (credential != "external-secret")
        {
            return application::SessionAuthenticationResult::rejected(
                application::SessionAuthenticationError::invalid_credentials);
        }
        return application::SessionAuthenticationResult::accepted(application::AuthenticatedSession{
            domain::SessionId{"session-1"}, domain::PlayerId{"sender"}, command.device_id,
            now + std::chrono::hours{1}});
    }

    std::string credential;
    std::optional<domain::DeviceId> device_id;
    std::optional<domain::CorrelationId> correlation_id;
};

[[nodiscard]] auto makeContext() -> application::AuthoritativeMembershipContext
{
    std::vector<domain::ScopeDefinition> scopes;
    scopes.emplace_back(domain::VoiceScope::team, "Team", 1, 5);
    scopes.emplace_back(domain::VoiceScope::specialization, "Specialization", 2, 4);
    scopes.emplace_back(domain::VoiceScope::group, "Group", 3, 2);

    std::vector<domain::Group> groups;
    groups.emplace_back(domain::GroupId{"group-1"}, "Group");
    std::vector<domain::Specialization> specializations;
    specializations.emplace_back(domain::SpecializationId{"specialization-1"},
                                 domain::GroupId{"group-1"}, "Specialization");
    std::vector<domain::Team> teams;
    teams.emplace_back(domain::TeamId{"team-1"}, domain::SpecializationId{"specialization-1"},
                       "Team");

    domain::Hierarchy hierarchy{domain::HierarchyId{"hierarchy-1"}, std::move(scopes),
                                std::move(groups), std::move(specializations), std::move(teams)};
    std::vector<domain::VoiceMembership> memberships;
    memberships.emplace_back(domain::PlayerId{"sender"}, domain::GroupId{"group-1"},
                             domain::SpecializationId{"specialization-1"}, domain::TeamId{"team-1"},
                             std::vector<domain::RoleId>{domain::RoleId{"speaker"}});
    memberships.emplace_back(domain::PlayerId{"private-listener"}, domain::GroupId{"group-1"},
                             domain::SpecializationId{"specialization-1"}, domain::TeamId{"team-1"},
                             std::vector<domain::RoleId>{domain::RoleId{"listener"}});

    std::vector<domain::RolePermissions> permissions;
    permissions.emplace_back(
        domain::RoleId{"speaker"},
        std::vector<domain::VoiceScope>{domain::VoiceScope::team, domain::VoiceScope::group},
        std::vector<domain::VoiceScope>{domain::VoiceScope::team, domain::VoiceScope::group});
    permissions.emplace_back(
        domain::RoleId{"listener"}, std::vector<domain::VoiceScope>{},
        std::vector<domain::VoiceScope>{domain::VoiceScope::team, domain::VoiceScope::group});

    return application::AuthoritativeMembershipContext{
        std::make_shared<const domain::MembershipSnapshot>(42, std::move(hierarchy),
                                                           std::move(memberships)),
        std::make_shared<const domain::RolePolicy>(std::move(permissions))};
}

[[nodiscard]] auto makeLargeContext() -> application::AuthoritativeMembershipContext
{
    const auto base = makeContext();
    const auto& hierarchy = base.snapshot->hierarchy();
    std::vector<domain::VoiceMembership> memberships;
    memberships.reserve(201);
    memberships.emplace_back(domain::PlayerId{"sender"}, domain::GroupId{"group-1"},
                             domain::SpecializationId{"specialization-1"}, domain::TeamId{"team-1"},
                             std::vector<domain::RoleId>{});
    for (std::size_t index = 0; index < 200; ++index)
    {
        memberships.emplace_back(domain::PlayerId{"listener-" + std::to_string(index)},
                                 domain::GroupId{"group-1"},
                                 domain::SpecializationId{"specialization-1"},
                                 domain::TeamId{"team-1"}, std::vector<domain::RoleId>{});
    }
    return {
        std::make_shared<const domain::MembershipSnapshot>(
            43,
            domain::Hierarchy{
                hierarchy.id(),
                std::vector<domain::ScopeDefinition>{hierarchy.scopes().begin(),
                                                     hierarchy.scopes().end()},
                std::vector<domain::Group>{hierarchy.groups().begin(), hierarchy.groups().end()},
                std::vector<domain::Specialization>{hierarchy.specializations().begin(),
                                                    hierarchy.specializations().end()},
                std::vector<domain::Team>{hierarchy.teams().begin(), hierarchy.teams().end()}},
            std::move(memberships)),
        base.role_policy};
}

class MembershipProvider final : public application::IMutableAuthoritativeMembershipRepository
{
  public:
    MembershipProvider() : context(makeContext())
    {
    }

    [[nodiscard]] auto currentFor(const domain::PlayerId& player_id) const
        -> std::optional<application::AuthoritativeMembershipContext> override
    {
        if (player_id == domain::PlayerId{"sender"})
        {
            return context;
        }
        return std::nullopt;
    }

    [[nodiscard]] auto upsertIfNewer(const domain::PlayerId& player_id,
                                     application::AuthoritativeMembershipContext new_context)
        -> std::optional<application::AuthoritativeMembershipWriteError> override
    {
        if (new_context.snapshot->find(player_id) == nullptr)
        {
            return application::AuthoritativeMembershipWriteError::player_not_in_snapshot;
        }
        if (new_context.snapshot->version() <= context.snapshot->version())
        {
            return application::AuthoritativeMembershipWriteError::version_not_newer;
        }
        context = std::move(new_context);
        return std::nullopt;
    }

    [[nodiscard]] auto erase(const domain::PlayerId& player_id) -> bool override
    {
        return player_id == domain::PlayerId{"sender"};
    }

    application::AuthoritativeMembershipContext context;
};

class TransmissionIds final : public application::ITransmissionIdGenerator
{
  public:
    [[nodiscard]] auto next() -> domain::TransmissionId override
    {
        return domain::TransmissionId{"transmission-1"};
    }
};

class RateLimiter final : public application::ITransmissionRateLimiter
{
  public:
    [[nodiscard]] auto allow(const domain::PlayerId&, application::TransmissionRateLimitAction,
                             application::TimePoint) -> bool override
    {
        return true;
    }
};

class ModerationAuthorizer final : public application::ITransmissionModerationAuthorizer
{
  public:
    [[nodiscard]] auto canInterrupt(const domain::PlayerId&, const domain::TransmissionId&) const
        -> bool override
    {
        return false;
    }
};

class MembershipAdministrator final : public application::IAdministrativeMembershipAuthorizer
{
  public:
    [[nodiscard]] auto canRead(const domain::PlayerId& actor, const domain::PlayerId&) const
        -> bool override
    {
        return actor == domain::PlayerId{"sender"};
    }

    [[nodiscard]] auto canRemove(const domain::PlayerId& actor, const domain::PlayerId&) const
        -> bool override
    {
        return actor == domain::PlayerId{"sender"};
    }

    [[nodiscard]] auto canReplace(const domain::PlayerId& actor, const domain::PlayerId&) const
        -> bool override
    {
        return actor == domain::PlayerId{"sender"};
    }
};

class VoiceGrantIssuer final : public application::IVoiceGrantIssuer
{
  public:
    [[nodiscard]] auto issue(const application::VoiceGrantClaims& claims) const
        -> std::vector<application::IssuedVoiceRoomGrant> override
    {
        return {{domain::VoiceScope::team, "team:" + std::string{claims.team_id.value()},
                 "team-access-token"},
                {domain::VoiceScope::group, "group:" + std::string{claims.group_id.value()},
                 "group-access-token"}};
    }
};

class TransportPresenceProvider final : public application::ITransportPresenceProvider
{
  public:
    [[nodiscard]] auto connectedScopeCount(const domain::PlayerId& player_id) const
        -> std::size_t override
    {
        const auto count = connected_scopes.find(player_id);
        return count == connected_scopes.end() ? 0 : count->second;
    }

    std::map<domain::PlayerId, std::size_t> connected_scopes{
        {domain::PlayerId{"sender"}, 3}, {domain::PlayerId{"private-listener"}, 0}};
};

struct Fixture final
{
    Fixture()
        : runtime{sessions, memberships}, directory{runtime, transport_presence},
          service{runtime,
                  runtime,
                  ids,
                  runtime,
                  rate_limiter,
                  moderation,
                  application::TransmissionLifecyclePolicy{std::chrono::seconds{30}}},
          grant_authorization{sessions, runtime,
                              application::VoiceGrantPolicy{std::chrono::seconds{30}}},
          adapter{authenticator,
                  sessions,
                  runtime,
                  service,
                  &runtime,
                  &membership_administrator,
                  &grant_authorization,
                  &grant_issuer,
                  "ws://voice.example",
                  &directory}
    {
        static_cast<void>(directory.upsertProfile({domain::PlayerId{"sender"}, "Alex", 42}));
        static_cast<void>(
            directory.upsertProfile({domain::PlayerId{"private-listener"}, "Pat", 42}));
        static_cast<void>(
            directory.replacePublicRoles(42, {{domain::RoleId{"speaker"}, "Speaker", 42}}));
    }

    [[nodiscard]] static auto request(std::string method, std::string target,
                                      std::string authorization, std::string body = {})
        -> network::HttpRequest
    {
        return network::HttpRequest{std::move(method),
                                    std::move(target),
                                    {{"authorization", std::move(authorization)},
                                     {"x-correlation-id", "correlation-1"},
                                     {"x-hvc-device-id", "device-1"}},
                                    std::move(body)};
    }

    application::TimePoint now{std::chrono::seconds{1'000}};
    SessionRepository sessions;
    Authenticator authenticator;
    MembershipProvider memberships;
    application::InMemoryControlPlaneStore runtime;
    TransportPresenceProvider transport_presence;
    application::DirectoryApplicationService directory;
    TransmissionIds ids;
    RateLimiter rate_limiter;
    ModerationAuthorizer moderation;
    MembershipAdministrator membership_administrator;
    application::TransmissionApplicationService service;
    application::VoiceGrantAuthorizationService grant_authorization;
    VoiceGrantIssuer grant_issuer;
    network::ControlPlaneHttpAdapter adapter;
};

[[nodiscard]] auto createsSessionOnlyAcrossTheCredentialBoundary() -> bool
{
    Fixture fixture;
    const auto response = fixture.adapter.handle(
        Fixture::request("POST", "/api/v1/sessions", "Bearer external-secret"), fixture.now);
    const auto stored = fixture.sessions.find(domain::SessionId{"session-1"});

    return response.status_code == 201 && stored &&
           fixture.authenticator.credential == "external-secret" &&
           fixture.authenticator.device_id == domain::DeviceId{"device-1"} &&
           fixture.authenticator.correlation_id == domain::CorrelationId{"correlation-1"} &&
           response.body.find("external-secret") == std::string::npos &&
           response.headers.at("x-hvc-api-version") == "v1";
}

[[nodiscard]] auto rejectsCredentialReuseAtSessionProtectedRoutes() -> bool
{
    Fixture fixture;
    static_cast<void>(fixture.adapter.handle(
        Fixture::request("POST", "/api/v1/sessions", "Bearer external-secret"), fixture.now));
    const auto response = fixture.adapter.handle(
        Fixture::request("GET", "/api/v1/membership", "Bearer external-secret"), fixture.now);

    return response.status_code == 401 &&
           response.body.find("session_required") != std::string::npos;
}

[[nodiscard]] auto rejectsAValidSessionFromAnotherDevice() -> bool
{
    Fixture fixture;
    static_cast<void>(fixture.adapter.handle(
        Fixture::request("POST", "/api/v1/sessions", "Bearer external-secret"), fixture.now));
    auto request = Fixture::request("GET", "/api/v1/membership", "Session session-1");
    request.headers.insert_or_assign("x-hvc-device-id", "device-2");
    const auto response = fixture.adapter.handle(request, fixture.now);

    return response.status_code == 403 &&
           response.body.find("session_device_mismatch") != std::string::npos;
}

[[nodiscard]] auto reportsReadinessWithoutAuthentication() -> bool
{
    Fixture fixture;
    const network::HttpRequest request{"GET", "/api/v1/health", {}, {}};
    const auto response = fixture.adapter.handle(request, fixture.now);
    return response.status_code == 200 &&
           response.body.find("\"status\":\"ready\"") != std::string::npos;
}

[[nodiscard]] auto exposesOnlyTheAuthenticatedPlayersMembership() -> bool
{
    Fixture fixture;
    static_cast<void>(fixture.adapter.handle(
        Fixture::request("POST", "/api/v1/sessions", "Bearer external-secret"), fixture.now));
    const auto response = fixture.adapter.handle(
        Fixture::request("GET", "/api/v1/membership", "Session session-1"), fixture.now);

    return response.status_code == 200 &&
           response.body.find("\"membership_version\":42") != std::string::npos &&
           response.body.find("\"player_id\":\"sender\"") != std::string::npos &&
           response.body.find("private-listener") == std::string::npos;
}

[[nodiscard]] auto exposesPrivacyLimitedDirectoryWithConditionalRequests() -> bool
{
    Fixture fixture;
    static_cast<void>(fixture.adapter.handle(
        Fixture::request("POST", "/api/v1/sessions", "Bearer external-secret"), fixture.now));
    const auto response = fixture.adapter.handle(
        Fixture::request("GET", "/api/v1/directory", "Session session-1"), fixture.now);
    if (response.status_code != 200 || !response.headers.contains("etag"))
    {
        return false;
    }

    auto conditional = Fixture::request("GET", "/api/v1/directory", "Session session-1");
    conditional.headers.emplace("if-none-match", response.headers.at("etag"));
    const auto not_modified = fixture.adapter.handle(conditional, fixture.now);

    constexpr std::array forbidden_fields{"account_id", "login_name", "credential", "device_id",
                                          "session_id", "ip_address", "audit_event"};
    return response.body.find("\"group_id\":\"group-1\"") != std::string::npos &&
           response.body.find("\"display_name\":\"Alex\"") != std::string::npos &&
           response.body.find("\"primary_team_id\":\"team-1\"") != std::string::npos &&
           response.body.find("\"role_id\":\"speaker\"") != std::string::npos &&
           response.body.find("\"public_role_ids\":[\"speaker\"]") != std::string::npos &&
           response.body.find("\"listener\"") == std::string::npos &&
           std::ranges::none_of(forbidden_fields,
                                [&response](std::string_view field) {
                                    return response.body.find(field) != std::string::npos;
                                }) &&
           not_modified.status_code == 304 && not_modified.body.empty() &&
           not_modified.headers.at("etag") == response.headers.at("etag") &&
           !not_modified.headers.contains("content-type");
}

[[nodiscard]] auto returnsAggregatedPresenceSnapshotsAndDeltas() -> bool
{
    Fixture fixture;
    static_cast<void>(fixture.adapter.handle(
        Fixture::request("POST", "/api/v1/sessions", "Bearer external-secret"), fixture.now));
    const auto snapshot = fixture.adapter.handle(
        Fixture::request("GET", "/api/v1/directory/presence", "Session session-1"), fixture.now);
    fixture.transport_presence.connected_scopes.insert_or_assign(
        domain::PlayerId{"private-listener"}, 2);
    const auto delta = fixture.adapter.handle(
        Fixture::request("GET", "/api/v1/directory/presence?after_version=1", "Session session-1"),
        fixture.now);
    const auto invalid = fixture.adapter.handle(
        Fixture::request("GET", "/api/v1/directory/presence?after_version=0", "Session session-1"),
        fixture.now);
    const auto unexpected_query = fixture.adapter.handle(
        Fixture::request("GET", "/api/v1/directory/presence?group_id=other", "Session session-1"),
        fixture.now);
    const auto foreign_group = fixture.adapter.handle(
        Fixture::request("GET", "/api/v1/directory?group_id=other", "Session session-1"),
        fixture.now);

    return snapshot.status_code == 200 &&
           snapshot.body.find("\"mode\":\"snapshot\"") != std::string::npos &&
           snapshot.body.find("\"player_id\":\"sender\",\"state\":\"online\"") !=
               std::string::npos &&
           snapshot.body.find("\"player_id\":\"private-listener\",\"state\":\"offline\"") !=
               std::string::npos &&
           snapshot.body.find("last_seen") == std::string::npos &&
           snapshot.body.find("device") == std::string::npos &&
           snapshot.body.find("speaking") == std::string::npos &&
           snapshot.headers.at("retry-after") == "1" && delta.status_code == 200 &&
           delta.body.find("\"mode\":\"delta\"") != std::string::npos &&
           delta.body.find("\"player_id\":\"private-listener\",\"state\":\"online\"") !=
               std::string::npos &&
           invalid.status_code == 409 &&
           invalid.body.find("presence_snapshot_required") != std::string::npos &&
           unexpected_query.status_code == 400 &&
           unexpected_query.body.find("invalid_query") != std::string::npos &&
           foreign_group.status_code == 404 &&
           foreign_group.body.find("route_not_found") != std::string::npos;
}

[[nodiscard]] auto rejectsOversizedDirectoriesWithoutPartialData() -> bool
{
    Fixture fixture;
    static_cast<void>(fixture.adapter.handle(
        Fixture::request("POST", "/api/v1/sessions", "Bearer external-secret"), fixture.now));
    fixture.memberships.context = makeLargeContext();
    const auto response = fixture.adapter.handle(
        Fixture::request("GET", "/api/v1/directory", "Session session-1"), fixture.now);

    return response.status_code == 413 &&
           response.body.find("directory_limit_exceeded") != std::string::npos &&
           response.body.find("listener-") == std::string::npos &&
           !response.headers.contains("etag");
}

[[nodiscard]] auto startsAndEndsWithoutExposingRecipientIds() -> bool
{
    Fixture fixture;
    static_cast<void>(fixture.adapter.handle(
        Fixture::request("POST", "/api/v1/sessions", "Bearer external-secret"), fixture.now));
    const auto started = fixture.adapter.handle(
        Fixture::request(
            "POST", "/api/v1/transmissions", "Session session-1",
            R"({"client_transmission_id":"client-1","scope":"group","membership_version":42})"),
        fixture.now);
    if (started.status_code != 201 ||
        started.body.find("\"recipient_count\":2") == std::string::npos ||
        started.body.find("private-listener") != std::string::npos ||
        !fixture.runtime.active(domain::TransmissionId{"transmission-1"}))
    {
        std::fprintf(
            stderr, "start status=%d body=%s active=%d\n", started.status_code,
            started.body.c_str(),
            fixture.runtime.active(domain::TransmissionId{"transmission-1"}).has_value() ? 1 : 0);
        return false;
    }

    const auto ended = fixture.adapter.handle(
        Fixture::request("DELETE", "/api/v1/transmissions/transmission-1", "Session session-1"),
        fixture.now);
    if (ended.status_code != 200 ||
        fixture.runtime.active(domain::TransmissionId{"transmission-1"}))
    {
        std::fprintf(
            stderr, "end status=%d body=%s active=%d\n", ended.status_code, ended.body.c_str(),
            fixture.runtime.active(domain::TransmissionId{"transmission-1"}).has_value() ? 1 : 0);
    }
    return ended.status_code == 200 &&
           ended.body.find("push_to_talk_released") != std::string::npos &&
           !fixture.runtime.active(domain::TransmissionId{"transmission-1"});
}

[[nodiscard]] auto issuesOnlyServerAuthorizedVoiceGrants() -> bool
{
    Fixture fixture;
    static_cast<void>(fixture.adapter.handle(
        Fixture::request("POST", "/api/v1/sessions", "Bearer external-secret"), fixture.now));
    const auto response = fixture.adapter.handle(
        Fixture::request("POST", "/api/v1/voice-grants", "Session session-1"), fixture.now);

    return response.status_code == 201 &&
           response.body.find("\"server_url\":\"ws://voice.example\"") != std::string::npos &&
           response.body.find("\"membership_version\":42") != std::string::npos &&
           response.body.find("\"scope\":\"team\"") != std::string::npos &&
           response.body.find("\"access_token\":\"team-access-token\"") != std::string::npos &&
           response.body.find("private-listener") == std::string::npos;
}

[[nodiscard]] auto rejectsUnknownAndMalformedInputDeterministically() -> bool
{
    Fixture fixture;
    static_cast<void>(fixture.adapter.handle(
        Fixture::request("POST", "/api/v1/sessions", "Bearer external-secret"), fixture.now));
    const auto malformed = fixture.adapter.handle(
        Fixture::request("POST", "/api/v1/transmissions", "Session session-1", "{"), fixture.now);
    const auto unknown = fixture.adapter.handle(
        Fixture::request("GET", "/api/v2/membership", "Session session-1"), fixture.now);

    return malformed.status_code == 400 &&
           malformed.body.find("invalid_json") != std::string::npos && unknown.status_code == 404 &&
           unknown.body.find("route_not_found") != std::string::npos;
}

[[nodiscard]] auto separatelyAuthorizesAdministrativeMembershipRoutes() -> bool
{
    Fixture fixture;
    static_cast<void>(fixture.adapter.handle(
        Fixture::request("POST", "/api/v1/sessions", "Bearer external-secret"), fixture.now));
    const auto read = fixture.adapter.handle(
        Fixture::request("GET", "/api/v1/admin/memberships/sender", "Session session-1"),
        fixture.now);
    const auto replaced = fixture.adapter.handle(
        Fixture::request(
            "PUT", "/api/v1/admin/memberships/sender", "Session session-1",
            R"({"membership_version":43,"group_id":"group-1","specialization_id":"specialization-1","team_id":"team-1","role_ids":"speaker","connected":true,"can_receive_voice":true,"transmit_muted":false})"),
        fixture.now);
    const auto stale = fixture.adapter.handle(
        Fixture::request(
            "PUT", "/api/v1/admin/memberships/sender", "Session session-1",
            R"({"membership_version":43,"group_id":"group-1","specialization_id":"specialization-1","team_id":"team-1","role_ids":"speaker","connected":true,"can_receive_voice":true,"transmit_muted":false})"),
        fixture.now);
    const auto removed = fixture.adapter.handle(
        Fixture::request("DELETE", "/api/v1/admin/memberships/sender", "Session session-1"),
        fixture.now);

    return read.status_code == 200 &&
           read.body.find("\"player_id\":\"sender\"") != std::string::npos &&
           replaced.status_code == 200 &&
           replaced.body.find("\"membership_version\":43") != std::string::npos &&
           stale.status_code == 409 &&
           stale.body.find("membership_version_not_newer") != std::string::npos &&
           removed.status_code == 200 &&
           removed.body.find("\"status\":\"removed\"") != std::string::npos;
}
} // namespace

auto main() noexcept -> int
{
    try
    {
        using Check = std::pair<const char*, bool (*)()>;
        const std::array checks{
            Check{"session credential boundary", &createsSessionOnlyAcrossTheCredentialBoundary},
            Check{"credential reuse", &rejectsCredentialReuseAtSessionProtectedRoutes},
            Check{"session device binding", &rejectsAValidSessionFromAnotherDevice},
            Check{"readiness", &reportsReadinessWithoutAuthentication},
            Check{"membership privacy", &exposesOnlyTheAuthenticatedPlayersMembership},
            Check{"directory privacy and caching",
                  &exposesPrivacyLimitedDirectoryWithConditionalRequests},
            Check{"presence snapshot and delta", &returnsAggregatedPresenceSnapshotsAndDeltas},
            Check{"directory participant limit", &rejectsOversizedDirectoriesWithoutPartialData},
            Check{"transmission lifecycle", &startsAndEndsWithoutExposingRecipientIds},
            Check{"voice grant issuance", &issuesOnlyServerAuthorizedVoiceGrants},
            Check{"administrative membership authorization",
                  &separatelyAuthorizesAdministrativeMembershipRoutes},
            Check{"invalid input", &rejectsUnknownAndMalformedInputDeterministically}};
        for (const auto& [name, check] : checks)
        {
            if (!check())
            {
                std::fprintf(stderr, "control-plane HTTP adapter test failed: %s\n", name);
                return 1;
            }
        }
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "unexpected exception: %s\n", error.what());
        return 1;
    }

    std::puts("control-plane HTTP adapter tests passed");
    return 0;
}
