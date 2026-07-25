#include <cstdio>
#include <exception>
#include <hvc/client/ptt_input.hpp>
#include <string>
#include <utility>
#include <vector>

namespace
{
using namespace hvc::client;
using hvc::domain::VoiceScope;

struct ActionChange final
{
    PushToTalkAction action;
    bool pressed;

    [[nodiscard]] auto operator==(const ActionChange&) const -> bool = default;
};

class ActionObserver final : public IPushToTalkActionObserver
{
  public:
    void onPushToTalkActionChanged(PushToTalkAction action, bool pressed) override
    {
        changes.push_back({action, pressed});
    }

    std::vector<ActionChange> changes;
};

class FakePushToTalkTarget final : public IPushToTalkTarget
{
  public:
    [[nodiscard]] auto pressPushToTalk(VoiceScope scope) -> VoiceSessionResult override
    {
        ++press_calls;
        pressed_scopes.push_back(scope);
        return next_press_result;
    }

    [[nodiscard]] auto releasePushToTalk() -> VoiceSessionResult override
    {
        ++release_calls;
        return next_release_result;
    }

    VoiceSessionResult next_press_result{VoiceSessionResult::success()};
    VoiceSessionResult next_release_result{VoiceSessionResult::success()};
    std::vector<VoiceScope> pressed_scopes;
    int press_calls{0};
    int release_calls{0};
};

class InputResultObserver final : public IPushToTalkInputObserver
{
  public:
    void onPushToTalkInputResult(PushToTalkAction action, bool pressed,
                                 const VoiceSessionResult& result) override
    {
        actions.push_back(action);
        states.push_back(pressed);
        results.push_back(static_cast<bool>(result));
    }

    std::vector<PushToTalkAction> actions;
    std::vector<bool> states;
    std::vector<bool> results;
};

[[nodiscard]] auto key(std::uint16_t code, std::string device = {}) -> InputControl
{
    return {InputDeviceKind::keyboard, code, false, std::move(device)};
}

[[nodiscard]] auto mouse(MouseButton button, std::string device = {}) -> InputControl
{
    return {InputDeviceKind::mouse, static_cast<std::uint16_t>(button), false, std::move(device)};
}

void emit(PushToTalkBindingEngine& engine, InputControl control, bool pressed)
{
    engine.onInputEvent({std::move(control), pressed});
}

auto testSeparateActionsAndChord() -> bool
{
    PushToTalkBindingEngine engine;
    ActionObserver observer;
    engine.setObserver(&observer);
    const std::vector<InputBinding> bindings{
        {PushToTalkAction::team, {key(0x54)}},
        {PushToTalkAction::specialization, {mouse(MouseButton::right)}},
        {PushToTalkAction::group, {key(0x11), key(0x47)}}};
    if (!engine.setBindings(bindings))
    {
        return false;
    }

    emit(engine, key(0x54, "keyboard-a"), true);
    emit(engine, key(0x54, "keyboard-a"), true);
    emit(engine, key(0x54, "keyboard-a"), false);
    emit(engine, key(0x11, "keyboard-a"), true);
    emit(engine, key(0x47, "keyboard-a"), true);
    emit(engine, key(0x11, "keyboard-a"), false);

    const std::vector<ActionChange> expected{{PushToTalkAction::team, true},
                                             {PushToTalkAction::team, false},
                                             {PushToTalkAction::group, true},
                                             {PushToTalkAction::group, false}};
    return observer.changes == expected && !engine.actionPressed(PushToTalkAction::team) &&
           !engine.actionPressed(PushToTalkAction::group);
}

auto testAlternativeBindingsAndDeviceRemoval() -> bool
{
    PushToTalkBindingEngine engine;
    ActionObserver observer;
    engine.setObserver(&observer);
    const std::vector<InputBinding> bindings{
        {PushToTalkAction::specialization, {key(0x53)}},
        {PushToTalkAction::specialization, {mouse(MouseButton::button_4)}}};
    if (!engine.setBindings(bindings))
    {
        return false;
    }

    emit(engine, key(0x53, "keyboard-a"), true);
    emit(engine, mouse(MouseButton::button_4, "mouse-a"), true);
    emit(engine, key(0x53, "keyboard-a"), false);
    if (!engine.actionPressed(PushToTalkAction::specialization) || observer.changes.size() != 1)
    {
        return false;
    }

    engine.onInputDeviceRemoved("mouse-a");
    const std::vector<ActionChange> expected{{PushToTalkAction::specialization, true},
                                             {PushToTalkAction::specialization, false}};
    return observer.changes == expected;
}

auto testDeviceSpecificBinding() -> bool
{
    PushToTalkBindingEngine engine;
    ActionObserver observer;
    engine.setObserver(&observer);
    const std::vector<InputBinding> bindings{
        {PushToTalkAction::group, {mouse(MouseButton::button_5, "mouse-primary")}}};
    if (!engine.setBindings(bindings))
    {
        return false;
    }

    emit(engine, mouse(MouseButton::button_5, "mouse-secondary"), true);
    emit(engine, mouse(MouseButton::button_5, "mouse-primary"), true);
    engine.releaseAll();
    const std::vector<ActionChange> expected{{PushToTalkAction::group, true},
                                             {PushToTalkAction::group, false}};
    return observer.changes == expected;
}

auto testRejectsInvalidAndConflictingBindings() -> bool
{
    PushToTalkBindingEngine engine;
    const std::vector<InputBinding> valid{{PushToTalkAction::team, {key(0x54)}}};
    if (!engine.setBindings(valid))
    {
        return false;
    }

    const std::vector<InputBinding> invalid{
        {PushToTalkAction::team, {}},
        {PushToTalkAction::team, {mouse(MouseButton::left), mouse(MouseButton::left)}},
        {PushToTalkAction::specialization, {key(0x47), key(0x11)}},
        {PushToTalkAction::group, {key(0x11), key(0x47, "keyboard-a")}}};
    const auto result = engine.setBindings(invalid);
    if (result || result.errors.size() != 3 ||
        result.errors[0].code != InputBindingErrorCode::empty_chord ||
        result.errors[1].code != InputBindingErrorCode::duplicate_control ||
        result.errors[2].code != InputBindingErrorCode::conflicting_binding)
    {
        return false;
    }
    return engine.bindings().size() == 1;
}

auto testAuthorizedInputCoordination() -> bool
{
    FakePushToTalkTarget target;
    AuthorizedPushToTalkInput input{target};
    InputResultObserver observer;
    input.setObserver(&observer);

    input.onPushToTalkActionChanged(PushToTalkAction::team, true);
    input.onPushToTalkActionChanged(PushToTalkAction::group, true);
    input.onPushToTalkActionChanged(PushToTalkAction::team, false);
    input.onPushToTalkActionChanged(PushToTalkAction::group, false);

    return target.press_calls == 1 && target.release_calls == 1 &&
           target.pressed_scopes == std::vector{VoiceScope::team} &&
           !input.activeAction().has_value() &&
           observer.actions == std::vector{PushToTalkAction::team, PushToTalkAction::group,
                                           PushToTalkAction::team} &&
           observer.states == std::vector<bool>{true, true, false} &&
           observer.results == std::vector<bool>{true, false, true};
}
} // namespace

auto main() noexcept -> int
{
    try
    {
        if (!testSeparateActionsAndChord() || !testAlternativeBindingsAndDeviceRemoval() ||
            !testDeviceSpecificBinding() || !testRejectsInvalidAndConflictingBindings() ||
            !testAuthorizedInputCoordination())
        {
            std::fputs("A push-to-talk input assertion failed.\n", stderr);
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
