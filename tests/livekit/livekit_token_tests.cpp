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
           grants[0].access_token ==
               "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
               "eyJpc3MiOiJhcGkta2V5Iiwic3ViIjoicGxheWVyLTQyIiwiZXhwIjoxODAwMDAwMDAwLCJt"
               "ZXRhZGF0YSI6IntcImRldmljZV9pZFwiOlwiZGV2aWNlLTFcIixcIm1lbWJlcnNoaXBfdmVy"
               "c2lvblwiOjd9IiwidmlkZW8iOnsicm9vbUpvaW4iOnRydWUsInJvb20iOiJ0ZWFtOnRlYW0t"
               "MSIsImNhblB1Ymxpc2giOnRydWUsImNhblN1YnNjcmliZSI6dHJ1ZX19."
               "8Wds09PEqbprWxbrlmlmDd6xhXS-z_uhUSzsGjK8JAo" &&
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
} // namespace

auto main() noexcept -> int
{
    try
    {
        if (!signsOnlyAuthorizedRoomsWithLeastPrivilege() ||
            !separatesTeamAndGroupRoomsAcrossMemberships())
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
