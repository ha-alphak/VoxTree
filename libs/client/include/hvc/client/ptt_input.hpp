#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <hvc/client/authorized_voice_client.hpp>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hvc::client
{
/// Identify one independently bindable push-to-talk action.
enum class PushToTalkAction : std::uint8_t
{
    /// Transmit to the current team.
    team,
    /// Transmit to the current specialization.
    specialization,
    /// Transmit to the current group.
    group
};

/**
 * Map an input action to its voice scope.
 *
 * @param action Push-to-talk action to map.
 * @returns Corresponding domain voice scope.
 */
[[nodiscard]] auto voiceScopeFor(PushToTalkAction action) noexcept -> domain::VoiceScope;

/// Classify a physical input device.
enum class InputDeviceKind : std::uint8_t
{
    /// Keyboard input identified by scan code.
    keyboard,
    /// Mouse input identified by button.
    mouse,
    /// Generic HID game controller, joystick, or HOTAS input.
    game_controller
};

/// Identify supported mouse buttons.
enum class MouseButton : std::uint8_t
{
    /// Primary mouse button.
    left = 1,
    /// Secondary mouse button.
    right = 2,
    /// Middle mouse button.
    middle = 3,
    /// First auxiliary mouse button.
    button_4 = 4,
    /// Second auxiliary mouse button.
    button_5 = 5
};

/// Identify one bindable control on one physical input device.
struct InputControl final
{
    /// Kind of physical device producing the control.
    InputDeviceKind device_kind{InputDeviceKind::keyboard};
    /// HID usage page, or zero for non-HID keyboard and mouse controls.
    std::uint16_t usage_page{0};
    /// Scan code, mouse-button value, or HID usage.
    std::uint16_t code{0};
    /// Whether a keyboard scan code uses the extended-key prefix.
    bool extended{false};
    /// Stable physical device identifier, empty for device-agnostic bindings.
    std::string device_id;

    /**
     * Compare all control identity fields.
     *
     * @returns `true` when both controls identify the same physical input.
     */
    [[nodiscard]] auto operator==(const InputControl&) const -> bool = default;
};

/// Describe one button advertised by a HID device.
struct HidButtonDescriptor final
{
    /// HID usage page containing the button.
    std::uint16_t usage_page{0};
    /// HID usage identifying the button.
    std::uint16_t usage{0};

    /**
     * Compare the usage page and usage.
     *
     * @returns `true` when both descriptors identify the same HID button.
     */
    [[nodiscard]] auto operator==(const HidButtonDescriptor&) const -> bool = default;
};

/// Describe a connected input device and its bindable controls.
struct InputDeviceProfile final
{
    /// Stable platform device identifier.
    std::string device_id;
    /// Human-readable device name.
    std::string display_name;
    /// Broad device classification.
    InputDeviceKind device_kind{InputDeviceKind::keyboard};
    /// USB/HID vendor identifier when available.
    std::uint16_t vendor_id{0};
    /// USB/HID product identifier when available.
    std::uint16_t product_id{0};
    /// Top-level HID usage page when available.
    std::uint16_t usage_page{0};
    /// Top-level HID usage when available.
    std::uint16_t usage{0};
    /// Bindable buttons advertised by a game-controller device.
    std::vector<HidButtonDescriptor> buttons;

    /**
     * Compare all profile fields.
     *
     * @returns `true` when both device snapshots are identical.
     */
    [[nodiscard]] auto operator==(const InputDeviceProfile&) const -> bool = default;
};

/// Bind one push-to-talk action to a simultaneous control chord.
struct InputBinding final
{
    /// Action activated by the chord.
    PushToTalkAction action{PushToTalkAction::team};
    /// Non-empty set of controls that must all be pressed.
    std::vector<InputControl> chord;
};

/// Report one normalized press or release from an input source.
struct InputEvent final
{
    /// Physical control whose state changed.
    InputControl control;
    /// `true` for press and `false` for release.
    bool pressed{false};
    /// Whether the event was received while the application lacked focus.
    bool received_in_background{false};
};

/// Classify a validation failure in a push-to-talk binding set.
enum class InputBindingErrorCode : std::uint8_t
{
    /// A binding contains no controls.
    empty_chord,
    /// A control has an invalid code or incompatible fields.
    invalid_control,
    /// The same control appears more than once in one chord.
    duplicate_control,
    /// Two bindings are identical.
    duplicate_binding,
    /// Different actions use a chord that cannot be distinguished safely.
    conflicting_binding
};

/// Describe one invalid binding and its location.
struct InputBindingError final
{
    /// Validation-failure classification.
    InputBindingErrorCode code{InputBindingErrorCode::empty_chord};
    /// Zero-based index of the invalid binding.
    std::size_t binding_index{0};
    /// Human-readable diagnostic message.
    std::string message;
};

/// Hold all validation errors found while replacing input bindings.
struct InputBindingResult final
{
    /**
     * Return whether validation succeeded without errors.
     *
     * @returns `true` when `errors` is empty.
     */
    [[nodiscard]] explicit operator bool() const noexcept;

    /// Complete set of validation errors.
    std::vector<InputBindingError> errors;
};

/// Receive device discovery, input events, and device-removal notifications.
class IInputEventSink
{
  public:
    /// Construct an input-event sink interface.
    IInputEventSink() = default;
    /// Copy construction is disabled.
    IInputEventSink(const IInputEventSink&) = delete;
    /// Copy assignment is disabled.
    auto operator=(const IInputEventSink&) -> IInputEventSink& = delete;
    /// Move construction is disabled.
    IInputEventSink(IInputEventSink&&) = delete;
    /// Move assignment is disabled.
    auto operator=(IInputEventSink&&) -> IInputEventSink& = delete;
    /// Destroy the input-event sink interface.
    virtual ~IInputEventSink() = default;

    /**
     * Handle discovery or refresh of an input device.
     *
     * @param profile Latest device profile.
     */
    virtual void onInputDeviceConnected(const InputDeviceProfile& profile) = 0;
    /**
     * Handle a normalized input event.
     *
     * @param event Control state change.
     */
    virtual void onInputEvent(const InputEvent& event) = 0;
    /**
     * Handle removal of a physical input device.
     *
     * @param device_id Stable identifier of the removed device.
     */
    virtual void onInputDeviceRemoved(const std::string& device_id) = 0;
};

/// Receive effective push-to-talk action-state changes.
class IPushToTalkActionObserver
{
  public:
    /// Construct an action observer interface.
    IPushToTalkActionObserver() = default;
    /// Copy construction is disabled.
    IPushToTalkActionObserver(const IPushToTalkActionObserver&) = delete;
    /// Copy assignment is disabled.
    auto operator=(const IPushToTalkActionObserver&) -> IPushToTalkActionObserver& = delete;
    /// Move construction is disabled.
    IPushToTalkActionObserver(IPushToTalkActionObserver&&) = delete;
    /// Move assignment is disabled.
    auto operator=(IPushToTalkActionObserver&&) -> IPushToTalkActionObserver& = delete;
    /// Destroy the action observer interface.
    virtual ~IPushToTalkActionObserver() = default;

    /**
     * Handle an effective action press or release.
     *
     * @param action Action whose state changed.
     * @param pressed New effective state.
     */
    virtual void onPushToTalkActionChanged(PushToTalkAction action, bool pressed) = 0;
};

/**
 * Validate bindings and derive action state from normalized input events.
 *
 * Binding, device, control, and action state is synchronized. Observer
 * callbacks occur without holding the internal mutex.
 */
class PushToTalkBindingEngine final : public IInputEventSink
{
  public:
    /**
     * Replace the action observer.
     *
     * @param observer Observer to notify, or `nullptr` to detach.
     */
    void setObserver(IPushToTalkActionObserver* observer) noexcept;
    /**
     * Validate and atomically replace all bindings.
     *
     * @param bindings Candidate binding set.
     * @returns All validation errors; the previous set remains active on failure.
     */
    [[nodiscard]] auto setBindings(std::span<const InputBinding> bindings) -> InputBindingResult;
    /**
     * Return a snapshot of the active bindings.
     *
     * @returns Thread-safe copy of the current binding set.
     */
    [[nodiscard]] auto bindings() const -> std::vector<InputBinding>;
    /**
     * Return a snapshot of connected input-device profiles.
     *
     * @returns Thread-safe copy of the connected device set.
     */
    [[nodiscard]] auto devices() const -> std::vector<InputDeviceProfile>;
    /**
     * Return whether an action is currently pressed.
     *
     * @param action Action to inspect.
     * @returns Current effective action state.
     */
    [[nodiscard]] auto actionPressed(PushToTalkAction action) const noexcept -> bool;
    /// Release every pressed control and notify resulting action changes.
    void releaseAll();

    /// @copydoc IInputEventSink::onInputDeviceConnected
    void onInputDeviceConnected(const InputDeviceProfile& profile) override;
    /// @copydoc IInputEventSink::onInputEvent
    void onInputEvent(const InputEvent& event) override;
    /// @copydoc IInputEventSink::onInputDeviceRemoved
    void onInputDeviceRemoved(const std::string& device_id) override;

  private:
    struct ActionChange final
    {
        PushToTalkAction action;
        bool pressed;
    };

    [[nodiscard]] static auto validateBindings(std::span<const InputBinding> bindings)
        -> InputBindingResult;
    [[nodiscard]] auto recalculateLocked() -> std::vector<ActionChange>;
    static void notify(std::span<const ActionChange> changes, IPushToTalkActionObserver* observer);

    mutable std::mutex mutex_;
    IPushToTalkActionObserver* observer_{nullptr};
    std::vector<InputBinding> bindings_;
    std::vector<InputDeviceProfile> devices_;
    std::vector<InputControl> pressed_controls_;
    std::array<bool, 3> pressed_actions_{};
};

/// Receive results after input actions invoke the authorized voice target.
class IPushToTalkInputObserver
{
  public:
    /// Construct an input-result observer interface.
    IPushToTalkInputObserver() = default;
    /// Copy construction is disabled.
    IPushToTalkInputObserver(const IPushToTalkInputObserver&) = delete;
    /// Copy assignment is disabled.
    auto operator=(const IPushToTalkInputObserver&) -> IPushToTalkInputObserver& = delete;
    /// Move construction is disabled.
    IPushToTalkInputObserver(IPushToTalkInputObserver&&) = delete;
    /// Move assignment is disabled.
    auto operator=(IPushToTalkInputObserver&&) -> IPushToTalkInputObserver& = delete;
    /// Destroy the input-result observer interface.
    virtual ~IPushToTalkInputObserver() = default;

    /**
     * Handle the result of an input-driven press or release.
     *
     * @param action Action that was invoked.
     * @param pressed Whether the invocation represented a press.
     * @param result Coordinated voice-session outcome.
     */
    virtual void onPushToTalkInputResult(PushToTalkAction action, bool pressed,
                                         const VoiceSessionResult& result) = 0;
};

/**
 * Translate exclusive input actions into authorized push-to-talk operations.
 *
 * When a new action is pressed while another is active, the old action is
 * released before the new one starts. The target and observer are not owned.
 */
class AuthorizedPushToTalkInput final : public IPushToTalkActionObserver
{
  public:
    /**
     * Construct an input coordinator.
     *
     * @param target Authorized target that must outlive this coordinator.
     */
    explicit AuthorizedPushToTalkInput(IPushToTalkTarget& target);

    /**
     * Replace the result observer.
     *
     * @param observer Observer to notify, or `nullptr` to detach.
     */
    void setObserver(IPushToTalkInputObserver* observer) noexcept;
    /**
     * Return the action currently held active.
     *
     * @returns Active action, or no value when all actions are released.
     */
    [[nodiscard]] auto activeAction() const noexcept -> std::optional<PushToTalkAction>;
    /// @copydoc IPushToTalkActionObserver::onPushToTalkActionChanged
    void onPushToTalkActionChanged(PushToTalkAction action, bool pressed) override;

  private:
    IPushToTalkTarget& target_;
    mutable std::mutex mutex_;
    IPushToTalkInputObserver* observer_{nullptr};
    std::optional<PushToTalkAction> active_action_;
};
} // namespace hvc::client
