#include <algorithm>
#include <array>
#include <cstdio>
#include <exception>
#include <hvc/domain/routing.hpp>
#include <initializer_list>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace hvc::domain;

auto allScopes() -> std::vector<VoiceScope>
{
    return {VoiceScope::team, VoiceScope::specialization, VoiceScope::group};
}

auto makeHierarchy(bool include_group_scope = true) -> Hierarchy
{
    std::vector<ScopeDefinition> scopes{
        {VoiceScope::team, "Unit", 1, 5},
        {VoiceScope::specialization, "Division", 2, 4},
    };
    if (include_group_scope)
    {
        scopes.emplace_back(VoiceScope::group, "Command", 3, 2);
    }

    return Hierarchy{HierarchyId{"main"},
                     std::move(scopes),
                     {{GroupId{"alpha"}, "Alpha", true}, {GroupId{"beta"}, "Beta", true}},
                     {
                         {SpecializationId{"alpha-red"}, GroupId{"alpha"}, "Red"},
                         {SpecializationId{"alpha-blue"}, GroupId{"alpha"}, "Blue"},
                         {SpecializationId{"beta-red"}, GroupId{"beta"}, "Red"},
                     },
                     {
                         {TeamId{"alpha-red-1"}, SpecializationId{"alpha-red"}, "One"},
                         {TeamId{"alpha-red-2"}, SpecializationId{"alpha-red"}, "Two"},
                         {TeamId{"alpha-blue-1"}, SpecializationId{"alpha-blue"}, "One"},
                         {TeamId{"beta-red-1"}, SpecializationId{"beta-red"}, "One"},
                     }};
}

// These strings deliberately mirror the external, serialized membership representation.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto makeMember(std::string player, std::string group, std::string specialization, std::string team,
                std::string role = "listener") -> VoiceMembership
{
    return {
        PlayerId{std::move(player)},
        GroupId{std::move(group)},
        SpecializationId{std::move(specialization)},
        TeamId{std::move(team)},
        {RoleId{std::move(role)}},
    };
}

auto makePolicy() -> RolePolicy
{
    return RolePolicy{{
        {RoleId{"player"}, {VoiceScope::team}, allScopes()},
        {RoleId{"leader"}, allScopes(), allScopes()},
        {RoleId{"listener"}, {}, allScopes()},
        {RoleId{"group-listener"}, {}, {VoiceScope::group}},
    }};
}

auto samePlayers(const std::vector<PlayerId>& actual,
                 std::initializer_list<std::string_view> expected) -> bool
{
    if (actual.size() != expected.size())
    {
        return false;
    }
    return std::ranges::equal(actual, expected, {}, &PlayerId::value,
                              [](std::string_view value) { return value; });
}

template <typename Function> auto throwsInvalidArgument(Function&& function) -> bool
{
    try
    {
        std::forward<Function>(function)();
    }
    catch (const std::invalid_argument&)
    {
        return true;
    }
    return false;
}

auto testStrongIdsAndHierarchyValidation() -> bool
{
    if (PlayerId{"same"}.value() != "same" || PlayerId{"same"} == PlayerId{"different"})
    {
        return false;
    }

    const bool rejects_empty_id = throwsInvalidArgument([] { static_cast<void>(PlayerId{""}); });
    const bool rejects_orphan = throwsInvalidArgument([] {
        static_cast<void>(Hierarchy{HierarchyId{"invalid"},
                                    {{VoiceScope::team, "Team", 1, std::nullopt}},
                                    {{GroupId{"alpha"}, "Alpha", true}},
                                    {},
                                    {{TeamId{"orphan"}, SpecializationId{"missing"}, "Orphan"}}});
    });

    return rejects_empty_id && rejects_orphan;
}

auto testSnapshotValidation() -> bool
{
    const bool rejects_invalid_path = throwsInvalidArgument([] {
        static_cast<void>(MembershipSnapshot{
            7, makeHierarchy(), {makeMember("bad", "alpha", "alpha-red", "alpha-blue-1")}});
    });
    const bool rejects_duplicate_player = throwsInvalidArgument([] {
        static_cast<void>(
            MembershipSnapshot{7,
                               makeHierarchy(),
                               {
                                   makeMember("duplicate", "alpha", "alpha-red", "alpha-red-1"),
                                   makeMember("duplicate", "alpha", "alpha-red", "alpha-red-2"),
                               }});
    });

    return rejects_invalid_path && rejects_duplicate_player;
}

auto testRoutingAndIsolation() -> bool
{
    auto sender = makeMember("sender", "alpha", "alpha-red", "alpha-red-1", "leader");
    auto disconnected = makeMember("disconnected", "alpha", "alpha-red", "alpha-red-1");
    disconnected.connected = false;
    auto disabled = makeMember("disabled", "alpha", "alpha-red", "alpha-red-1");
    disabled.can_receive_voice = false;

    const MembershipSnapshot snapshot{
        42,
        makeHierarchy(),
        {
            makeMember("z-team", "alpha", "alpha-red", "alpha-red-1"),
            makeMember("other-team", "alpha", "alpha-red", "alpha-red-2"),
            makeMember("other-specialization", "alpha", "alpha-blue", "alpha-blue-1"),
            makeMember("other-group", "beta", "beta-red", "beta-red-1"),
            std::move(disconnected),
            std::move(disabled),
            std::move(sender),
        }};
    const RolePolicy policy = makePolicy();

    const auto team =
        RecipientResolver::resolve(snapshot, policy, {PlayerId{"sender"}, VoiceScope::team, 42});
    const auto specialization = RecipientResolver::resolve(
        snapshot, policy, {PlayerId{"sender"}, VoiceScope::specialization, 42});
    const std::array restrictions{
        RecipientRestriction{PlayerId{"other-specialization"}, PlayerId{"sender"}}};
    const auto group = RecipientResolver::resolve(
        snapshot, policy, {PlayerId{"sender"}, VoiceScope::group, 42}, restrictions);

    return team.accepted() && samePlayers(team.recipients, {"sender", "z-team"}) &&
           specialization.accepted() &&
           samePlayers(specialization.recipients, {"other-team", "sender", "z-team"}) &&
           group.accepted() && samePlayers(group.recipients, {"other-team", "sender", "z-team"});
}

auto testIndependentPermissionsAndRejections() -> bool
{
    const MembershipSnapshot snapshot{
        8,
        makeHierarchy(false),
        {
            makeMember("player", "alpha", "alpha-red", "alpha-red-1", "player"),
            makeMember("group-listener", "alpha", "alpha-red", "alpha-red-1", "group-listener"),
        }};
    const RolePolicy policy = makePolicy();

    const auto stale =
        RecipientResolver::resolve(snapshot, policy, {PlayerId{"player"}, VoiceScope::team, 7});
    const auto unauthorized = RecipientResolver::resolve(
        snapshot, policy, {PlayerId{"player"}, VoiceScope::specialization, 8});
    const auto missing_scope =
        RecipientResolver::resolve(snapshot, policy, {PlayerId{"player"}, VoiceScope::group, 8});
    const auto allowed =
        RecipientResolver::resolve(snapshot, policy, {PlayerId{"player"}, VoiceScope::team, 8});

    return stale.rejection == RoutingError::voice_membership_stale &&
           unauthorized.rejection == RoutingError::voice_scope_not_authorized &&
           missing_scope.rejection == RoutingError::voice_scope_not_found && allowed.accepted() &&
           samePlayers(allowed.recipients, {"player"});
}

auto testDeterminismForLargeGroup() -> bool
{
    std::vector<VoiceMembership> members;
    members.reserve(200);
    for (int index = 199; index >= 0; --index)
    {
        auto member = makeMember(
            "player-" + std::to_string(index), "alpha", index % 2 == 0 ? "alpha-red" : "alpha-blue",
            index % 2 == 0 ? "alpha-red-1" : "alpha-blue-1", index == 0 ? "leader" : "listener");
        members.push_back(std::move(member));
    }

    const RolePolicy policy = makePolicy();
    const MembershipSnapshot first{100, makeHierarchy(), members};
    const auto first_result =
        RecipientResolver::resolve(first, policy, {PlayerId{"player-0"}, VoiceScope::group, 100});
    if (!first_result.accepted() || first_result.recipients.size() != 200 ||
        !std::ranges::is_sorted(first_result.recipients))
    {
        return false;
    }

    std::mt19937 generator{0x485643U};
    std::ranges::shuffle(members, generator);
    const MembershipSnapshot second{101, makeHierarchy(), std::move(members)};
    const auto second_result =
        RecipientResolver::resolve(second, policy, {PlayerId{"player-0"}, VoiceScope::group, 101});

    return second_result.accepted() && first_result.recipients == second_result.recipients;
}
} // namespace

auto main() noexcept -> int
{
    try
    {
        const bool passed = testStrongIdsAndHierarchyValidation() && testSnapshotValidation() &&
                            testRoutingAndIsolation() &&
                            testIndependentPermissionsAndRejections() &&
                            testDeterminismForLargeGroup();
        if (!passed)
        {
            std::fputs("A domain-core assertion failed.\n", stderr);
            return 1;
        }
    }
    catch (const std::exception& error)
    {
        std::fputs("Unexpected exception: ", stderr);
        std::fputs(error.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }

    return 0;
}
