#include <array>
#include <chrono>
#include <cstdio>
#include <hvc/livekit/livekit_token.hpp>

namespace
{
namespace application = hvc::application;
namespace domain = hvc::domain;
namespace livekit = hvc::livekit;

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
           grants[0].token ==
               "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
               "eyJpc3MiOiJhcGkta2V5Iiwic3ViIjoicGxheWVyLTQyIiwiZXhwIjoxODAwMDAwMDAwLCJt"
               "ZXRhZGF0YSI6IntcImRldmljZV9pZFwiOlwiZGV2aWNlLTFcIixcIm1lbWJlcnNoaXBfdmVy"
               "c2lvblwiOjd9IiwidmlkZW8iOnsicm9vbUpvaW4iOnRydWUsInJvb20iOiJ0ZWFtOnRlYW0t"
               "MSIsImNhblB1Ymxpc2giOnRydWUsImNhblN1YnNjcmliZSI6dHJ1ZX19."
               "8Wds09PEqbprWxbrlmlmDd6xhXS-z_uhUSzsGjK8JAo" &&
           grants[1].scope == domain::VoiceScope::group && grants[1].room_name == "group:group-1";
}
} // namespace

auto main() -> int
{
    if (!signsOnlyAuthorizedRoomsWithLeastPrivilege())
    {
        std::fputs("LiveKit token test failed\n", stderr);
        return 1;
    }
    std::puts("LiveKit token tests passed");
    return 0;
}
