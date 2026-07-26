#include <array>
#include <chrono>
#include <cstdio>
#include <exception>
#include <hvc/livekit/livekit_token.hpp>
#include <string>
#include <utility>
#include <vector>

namespace
{
namespace application = hvc::application;
namespace domain = hvc::domain;
namespace livekit = hvc::livekit;

class RecordingRoomService final : public livekit::ILiveKitRoomServiceClient
{
  public:
    [[nodiscard]] auto updateParticipant(std::string_view room_name,
                                         std::string_view participant_identity, bool can_publish,
                                         bool can_subscribe, std::string_view authorization_token)
        -> bool override
    {
        room = room_name;
        identity = participant_identity;
        publishing = can_publish;
        subscribing = can_subscribe;
        token_present = !authorization_token.empty();
        ++calls;
        return true;
    }

    std::string room;
    std::string identity;
    bool publishing{};
    bool subscribing{};
    bool token_present{};
    int calls{};
};

[[nodiscard]] auto tokenPayload(std::string_view token) -> std::string
{
    constexpr std::string_view alphabet{
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"};
    const auto first_dot = token.find('.');
    const auto second_dot = token.find('.', first_dot + 1U);
    if (first_dot == std::string_view::npos || second_dot == std::string_view::npos)
    {
        return {};
    }
    const auto encoded = token.substr(first_dot + 1U, second_dot - first_dot - 1U);
    std::string decoded;
    std::uint32_t bits{};
    int bit_count{};
    for (const char character : encoded)
    {
        const auto value = alphabet.find(character);
        if (value == std::string_view::npos)
        {
            return {};
        }
        bits = (bits << 6U) | static_cast<std::uint32_t>(value);
        bit_count += 6;
        if (bit_count >= 8)
        {
            bit_count -= 8;
            decoded.push_back(static_cast<char>((bits >> bit_count) & 0xFFU));
        }
    }
    return decoded;
}

[[nodiscard]] auto claimsFor(std::string group, std::string specialization, std::string team)
    -> application::VoiceGrantClaims
{
    const std::vector scopes{domain::VoiceScope::team, domain::VoiceScope::specialization,
                             domain::VoiceScope::group};
    return {domain::PlayerId{"player-42"},
            domain::DeviceId{"device-1"},
            7,
            domain::GroupId{std::move(group)},
            domain::SpecializationId{std::move(specialization)},
            domain::TeamId{std::move(team)},
            scopes,
            scopes,
            application::TimePoint{std::chrono::seconds{1'800'000'000}}};
}

[[nodiscard]] auto signsOnlyAuthorizedRoomsWithLeastPrivilege() -> bool
{
    const application::VoiceGrantClaims claims{
        domain::PlayerId{"player-42"},
        domain::DeviceId{"device-1"},
        7,
        domain::GroupId{"group-1"},
        domain::SpecializationId{"specialization-1"},
        domain::TeamId{"team-1"},
        {domain::VoiceScope::team},
        {domain::VoiceScope::team, domain::VoiceScope::group},
        application::TimePoint{std::chrono::seconds{1'800'000'000}}};
    const livekit::LiveKitTokenAdapter adapter{
        livekit::LiveKitCredentials{"api-key", "api-secret"}};
    const auto grants = adapter.sign(claims);

    return grants.size() == 2 && grants[0].scope == domain::VoiceScope::team &&
           grants[0].room_name == "team:team-1" &&
           tokenPayload(grants[0].access_token).find("\"canPublish\":false") != std::string::npos &&
           tokenPayload(grants[0].access_token).find("\"canPublish\":true") == std::string::npos &&
           grants[1].scope == domain::VoiceScope::group && grants[1].room_name == "group:group-1";
}

[[nodiscard]] auto separatesTeamAndGroupRoomsAcrossMemberships() -> bool
{
    const livekit::LiveKitTokenAdapter adapter{
        livekit::LiveKitCredentials{"api-key", "api-secret"}};
    const auto alpha_team_one = adapter.sign(claimsFor("group-alpha", "alpha-red", "alpha-red-1"));
    const auto alpha_team_two = adapter.sign(claimsFor("group-alpha", "alpha-red", "alpha-red-2"));
    const auto beta_team_one = adapter.sign(claimsFor("group-beta", "beta-red", "beta-red-1"));

    return alpha_team_one.size() == 3 && alpha_team_two.size() == 3 && beta_team_one.size() == 3 &&
           alpha_team_one[0].room_name == "team:alpha-red-1" &&
           alpha_team_two[0].room_name == "team:alpha-red-2" &&
           alpha_team_one[0].room_name != alpha_team_two[0].room_name &&
           alpha_team_one[2].room_name == alpha_team_two[2].room_name &&
           alpha_team_one[2].room_name == "group:group-alpha" &&
           beta_team_one[2].room_name == "group:group-beta" &&
           alpha_team_one[2].room_name != beta_team_one[2].room_name &&
           alpha_team_one[0].access_token != alpha_team_two[0].access_token &&
           alpha_team_one[2].access_token != beta_team_one[2].access_token;
}

[[nodiscard]] auto controlsPublicationForTheExactActiveRoom() -> bool
{
    RecordingRoomService room_service;
    livekit::LiveKitPublicationController controller{
        livekit::LiveKitCredentials{"api-key", "api-secret"}, room_service};
    application::AuthorizedTransmission authorization{domain::TransmissionId{"tx-1"},
                                                      domain::ClientTransmissionId{"client-1"},
                                                      domain::PlayerId{"player-42"},
                                                      domain::VoiceScope::group,
                                                      7,
                                                      {domain::PlayerId{"recipient"}},
                                                      domain::CorrelationId{"start"}};
    application::ActiveTransmission active{std::move(authorization), domain::SessionId{"session-1"},
                                           domain::DeviceId{"device-1"}, application::Clock::now()};
    active.scope_node_id = "group-1";
    active.scope_can_subscribe = true;
    if (!controller.onStarted(active) || room_service.calls != 1 ||
        room_service.room != "group:group-1" || room_service.identity != "player-42" ||
        !room_service.publishing || !room_service.subscribing || !room_service.token_present)
    {
        return false;
    }
    controller.onEnded({active, domain::TransmissionStopReason::timed_out,
                        application::Clock::now(), domain::CorrelationId{"timeout"}});
    return room_service.calls == 2 && !room_service.publishing && controller.failureCount() == 0;
}
} // namespace

auto main() noexcept -> int
{
    try
    {
        if (!signsOnlyAuthorizedRoomsWithLeastPrivilege() ||
            !separatesTeamAndGroupRoomsAcrossMemberships() ||
            !controlsPublicationForTheExactActiveRoom())
        {
            std::fputs("LiveKit token test failed\n", stderr);
            return 1;
        }
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "unexpected exception: %s\n", error.what());
        return 1;
    }
    std::puts("LiveKit token tests passed");
    return 0;
}
