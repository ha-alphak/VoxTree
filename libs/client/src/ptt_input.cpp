#include <algorithm>
#include <hvc/client/ptt_input.hpp>
#include <tuple>
#include <utility>

namespace hvc::client
{
namespace
{
constexpr auto action_count = std::size_t{3};

[[nodiscard]] auto actionIndex(PushToTalkAction action) noexcept -> std::size_t
{
    return static_cast<std::size_t>(action);
}

[[nodiscard]] auto controlLess(const InputControl& left, const InputControl& right) -> bool
{
    return std::tie(left.device_kind, left.usage_page, left.code, left.extended, left.device_id) <
           std::tie(right.device_kind, right.usage_page, right.code, right.extended,
                    right.device_id);
}

[[nodiscard]] auto normalizedChord(const InputBinding& binding) -> std::vector<InputControl>
{
    auto controls = binding.chord;
    std::ranges::sort(controls, controlLess);
    return controls;
}

[[nodiscard]] auto controlsOverlap(const InputControl& left, const InputControl& right) noexcept
    -> bool
{
    return left.device_kind == right.device_kind && left.usage_page == right.usage_page &&
           left.code == right.code && left.extended == right.extended &&
           (left.device_id.empty() || right.device_id.empty() || left.device_id == right.device_id);
}

[[nodiscard]] auto chordsOverlap(std::span<const InputControl> left,
                                 std::span<const InputControl> right) -> bool
{
    return left.size() == right.size() && std::ranges::equal(left, right, controlsOverlap);
}

[[nodiscard]] auto validControl(const InputControl& control) noexcept -> bool
{
    if (control.code == 0)
    {
        return false;
    }
    switch (control.device_kind)
    {
    case InputDeviceKind::keyboard:
        return control.usage_page == 0;
    case InputDeviceKind::mouse:
        return control.code <= static_cast<std::uint16_t>(MouseButton::button_5) &&
               control.usage_page == 0 && !control.extended;
    case InputDeviceKind::game_controller:
        return control.usage_page != 0 && !control.extended;
    }
    return false;
}

[[nodiscard]] auto heldControlMatches(const InputControl& expected,
                                      const InputControl& held) noexcept -> bool
{
    return expected.device_kind == held.device_kind && expected.usage_page == held.usage_page &&
           expected.code == held.code && expected.extended == held.extended &&
           (expected.device_id.empty() || expected.device_id == held.device_id);
}

[[nodiscard]] auto bindingPressed(const InputBinding& binding,
                                  std::span<const InputControl> pressed_controls) -> bool
{
    return std::ranges::all_of(binding.chord, [&](const InputControl& expected) {
        return std::ranges::any_of(pressed_controls, [&](const InputControl& held) {
            return heldControlMatches(expected, held);
        });
    });
}
} // namespace

auto voiceScopeFor(PushToTalkAction action) noexcept -> domain::VoiceScope
{
    switch (action)
    {
    case PushToTalkAction::team:
        return domain::VoiceScope::team;
    case PushToTalkAction::specialization:
        return domain::VoiceScope::specialization;
    case PushToTalkAction::group:
        return domain::VoiceScope::group;
    }
    return domain::VoiceScope::team;
}

InputBindingResult::operator bool() const noexcept
{
    return errors.empty();
}

void PushToTalkBindingEngine::setObserver(IPushToTalkActionObserver* observer) noexcept
{
    std::scoped_lock lock{mutex_};
    observer_ = observer;
}

auto PushToTalkBindingEngine::setBindings(std::span<const InputBinding> bindings)
    -> InputBindingResult
{
    auto result = validateBindings(bindings);
    if (!result)
    {
        return result;
    }

    std::vector<ActionChange> changes;
    IPushToTalkActionObserver* current_observer = nullptr;
    {
        std::scoped_lock lock{mutex_};
        for (std::size_t index = 0; index < pressed_actions_.size(); ++index)
        {
            if (pressed_actions_[index])
            {
                changes.push_back({static_cast<PushToTalkAction>(index), false});
            }
        }
        bindings_.assign(bindings.begin(), bindings.end());
        pressed_controls_.clear();
        pressed_actions_.fill(false);
        current_observer = observer_;
    }
    notify(changes, current_observer);
    return result;
}

auto PushToTalkBindingEngine::bindings() const -> std::vector<InputBinding>
{
    std::scoped_lock lock{mutex_};
    return bindings_;
}

auto PushToTalkBindingEngine::devices() const -> std::vector<InputDeviceProfile>
{
    std::scoped_lock lock{mutex_};
    return devices_;
}

auto PushToTalkBindingEngine::actionPressed(PushToTalkAction action) const noexcept -> bool
{
    std::scoped_lock lock{mutex_};
    const auto index = actionIndex(action);
    return index < pressed_actions_.size() && pressed_actions_[index];
}

void PushToTalkBindingEngine::releaseAll()
{
    std::vector<ActionChange> changes;
    IPushToTalkActionObserver* current_observer = nullptr;
    {
        std::scoped_lock lock{mutex_};
        pressed_controls_.clear();
        changes = recalculateLocked();
        current_observer = observer_;
    }
    notify(changes, current_observer);
}

void PushToTalkBindingEngine::onInputDeviceConnected(const InputDeviceProfile& profile)
{
    if (profile.device_id.empty())
    {
        return;
    }
    std::scoped_lock lock{mutex_};
    const auto existing =
        std::ranges::find(devices_, profile.device_id, &InputDeviceProfile::device_id);
    if (existing == devices_.end())
    {
        devices_.push_back(profile);
    }
    else
    {
        *existing = profile;
    }
}

void PushToTalkBindingEngine::onInputEvent(const InputEvent& event)
{
    if (!validControl(event.control) || event.control.device_id.empty())
    {
        return;
    }

    std::vector<ActionChange> changes;
    IPushToTalkActionObserver* current_observer = nullptr;
    {
        std::scoped_lock lock{mutex_};
        const auto existing = std::ranges::find(pressed_controls_, event.control);
        if (event.pressed && existing == pressed_controls_.end())
        {
            pressed_controls_.push_back(event.control);
        }
        else if (!event.pressed && existing != pressed_controls_.end())
        {
            pressed_controls_.erase(existing);
        }
        else
        {
            return;
        }
        changes = recalculateLocked();
        current_observer = observer_;
    }
    notify(changes, current_observer);
}

void PushToTalkBindingEngine::onInputDeviceRemoved(const std::string& device_id)
{
    if (device_id.empty())
    {
        return;
    }

    std::vector<ActionChange> changes;
    IPushToTalkActionObserver* current_observer = nullptr;
    {
        std::scoped_lock lock{mutex_};
        std::erase_if(pressed_controls_,
                      [&](const InputControl& control) { return control.device_id == device_id; });
        std::erase_if(devices_, [&](const InputDeviceProfile& profile) {
            return profile.device_id == device_id;
        });
        changes = recalculateLocked();
        current_observer = observer_;
    }
    notify(changes, current_observer);
}

auto PushToTalkBindingEngine::validateBindings(std::span<const InputBinding> bindings)
    -> InputBindingResult
{
    InputBindingResult result;
    std::vector<std::vector<InputControl>> normalized;
    normalized.reserve(bindings.size());

    for (std::size_t index = 0; index < bindings.size(); ++index)
    {
        const auto& binding = bindings[index];
        auto chord = normalizedChord(binding);
        if (chord.empty())
        {
            result.errors.push_back(
                {InputBindingErrorCode::empty_chord, index, "an input chord cannot be empty"});
        }
        else if (std::ranges::any_of(
                     chord, [](const InputControl& control) { return !validControl(control); }))
        {
            result.errors.push_back({InputBindingErrorCode::invalid_control, index,
                                     "an input chord contains an invalid control"});
        }
        else if (std::ranges::adjacent_find(chord) != chord.end())
        {
            result.errors.push_back({InputBindingErrorCode::duplicate_control, index,
                                     "an input chord contains the same control more than once"});
        }

        for (std::size_t previous = 0; previous < normalized.size(); ++previous)
        {
            const auto duplicate = chord == normalized[previous];
            const auto conflict = binding.action != bindings[previous].action &&
                                  chordsOverlap(chord, normalized[previous]);
            if (duplicate || conflict)
            {
                const auto code = binding.action == bindings[previous].action
                                      ? InputBindingErrorCode::duplicate_binding
                                      : InputBindingErrorCode::conflicting_binding;
                const auto message = code == InputBindingErrorCode::duplicate_binding
                                         ? "the input binding is duplicated"
                                         : "the input binding is assigned to multiple actions";
                result.errors.push_back({code, index, message});
                break;
            }
        }
        normalized.push_back(std::move(chord));
    }
    return result;
}

auto PushToTalkBindingEngine::recalculateLocked() -> std::vector<ActionChange>
{
    std::array<bool, action_count> next{};
    for (const auto& binding : bindings_)
    {
        const auto index = actionIndex(binding.action);
        if (index < next.size() && bindingPressed(binding, pressed_controls_))
        {
            next[index] = true;
        }
    }

    std::vector<ActionChange> changes;
    for (std::size_t index = 0; index < next.size(); ++index)
    {
        if (next[index] != pressed_actions_[index])
        {
            changes.push_back({static_cast<PushToTalkAction>(index), next[index]});
        }
    }
    pressed_actions_ = next;
    return changes;
}

void PushToTalkBindingEngine::notify(std::span<const ActionChange> changes,
                                     IPushToTalkActionObserver* observer)
{
    if (observer == nullptr)
    {
        return;
    }
    for (const auto& change : changes)
    {
        observer->onPushToTalkActionChanged(change.action, change.pressed);
    }
}

AuthorizedPushToTalkInput::AuthorizedPushToTalkInput(IPushToTalkTarget& target) : target_(target)
{
}

void AuthorizedPushToTalkInput::setObserver(IPushToTalkInputObserver* observer) noexcept
{
    std::scoped_lock lock{mutex_};
    observer_ = observer;
}

auto AuthorizedPushToTalkInput::activeAction() const noexcept -> std::optional<PushToTalkAction>
{
    std::scoped_lock lock{mutex_};
    return active_action_;
}

void AuthorizedPushToTalkInput::onPushToTalkActionChanged(PushToTalkAction action, bool pressed)
{
    VoiceSessionResult result;
    IPushToTalkInputObserver* current_observer = nullptr;
    {
        std::scoped_lock lock{mutex_};
        if (pressed)
        {
            if (active_action_.has_value())
            {
                result = VoiceSessionResult::failure(
                    {VoiceSessionErrorSource::client_state, "ptt_input_already_active",
                     "another push-to-talk input action is already active", 0});
            }
            else
            {
                result = target_.pressPushToTalk(voiceScopeFor(action));
                if (result)
                {
                    active_action_ = action;
                }
            }
        }
        else if (active_action_ == action)
        {
            result = target_.releasePushToTalk();
            active_action_.reset();
        }
        else
        {
            return;
        }
        current_observer = observer_;
    }

    if (current_observer != nullptr)
    {
        current_observer->onPushToTalkInputResult(action, pressed, result);
    }
}
} // namespace hvc::client
