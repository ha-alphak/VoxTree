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
enum class PushToTalkAction : std::uint8_t
{
    team,
    specialization,
    group
};

[[nodiscard]] auto voiceScopeFor(PushToTalkAction action) noexcept -> domain::VoiceScope;

enum class InputDeviceKind : std::uint8_t
{
    keyboard,
    mouse
};

enum class MouseButton : std::uint8_t
{
    left = 1,
    right = 2,
    middle = 3,
    button_4 = 4,
    button_5 = 5
};

struct InputControl final
{
    InputDeviceKind device_kind{InputDeviceKind::keyboard};
    std::uint16_t code{0};
    bool extended{false};
    std::string device_id;

    [[nodiscard]] auto operator==(const InputControl&) const -> bool = default;
};

struct InputBinding final
{
    PushToTalkAction action{PushToTalkAction::team};
    std::vector<InputControl> chord;
};

struct InputEvent final
{
    InputControl control;
    bool pressed{false};
};

enum class InputBindingErrorCode : std::uint8_t
{
    empty_chord,
    invalid_control,
    duplicate_control,
    duplicate_binding,
    conflicting_binding
};

struct InputBindingError final
{
    InputBindingErrorCode code{InputBindingErrorCode::empty_chord};
    std::size_t binding_index{0};
    std::string message;
};

struct InputBindingResult final
{
    [[nodiscard]] explicit operator bool() const noexcept;

    std::vector<InputBindingError> errors;
};

class IInputEventSink
{
  public:
    IInputEventSink() = default;
    IInputEventSink(const IInputEventSink&) = delete;
    auto operator=(const IInputEventSink&) -> IInputEventSink& = delete;
    IInputEventSink(IInputEventSink&&) = delete;
    auto operator=(IInputEventSink&&) -> IInputEventSink& = delete;
    virtual ~IInputEventSink() = default;

    virtual void onInputEvent(const InputEvent& event) = 0;
    virtual void onInputDeviceRemoved(const std::string& device_id) = 0;
};

class IPushToTalkActionObserver
{
  public:
    IPushToTalkActionObserver() = default;
    IPushToTalkActionObserver(const IPushToTalkActionObserver&) = delete;
    auto operator=(const IPushToTalkActionObserver&) -> IPushToTalkActionObserver& = delete;
    IPushToTalkActionObserver(IPushToTalkActionObserver&&) = delete;
    auto operator=(IPushToTalkActionObserver&&) -> IPushToTalkActionObserver& = delete;
    virtual ~IPushToTalkActionObserver() = default;

    virtual void onPushToTalkActionChanged(PushToTalkAction action, bool pressed) = 0;
};

class PushToTalkBindingEngine final : public IInputEventSink
{
  public:
    void setObserver(IPushToTalkActionObserver* observer) noexcept;
    [[nodiscard]] auto setBindings(std::span<const InputBinding> bindings) -> InputBindingResult;
    [[nodiscard]] auto bindings() const -> std::vector<InputBinding>;
    [[nodiscard]] auto actionPressed(PushToTalkAction action) const noexcept -> bool;
    void releaseAll();

    void onInputEvent(const InputEvent& event) override;
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
    std::vector<InputControl> pressed_controls_;
    std::array<bool, 3> pressed_actions_{};
};

class IPushToTalkInputObserver
{
  public:
    IPushToTalkInputObserver() = default;
    IPushToTalkInputObserver(const IPushToTalkInputObserver&) = delete;
    auto operator=(const IPushToTalkInputObserver&) -> IPushToTalkInputObserver& = delete;
    IPushToTalkInputObserver(IPushToTalkInputObserver&&) = delete;
    auto operator=(IPushToTalkInputObserver&&) -> IPushToTalkInputObserver& = delete;
    virtual ~IPushToTalkInputObserver() = default;

    virtual void onPushToTalkInputResult(PushToTalkAction action, bool pressed,
                                         const VoiceSessionResult& result) = 0;
};

class AuthorizedPushToTalkInput final : public IPushToTalkActionObserver
{
  public:
    explicit AuthorizedPushToTalkInput(IPushToTalkTarget& target);

    void setObserver(IPushToTalkInputObserver* observer) noexcept;
    [[nodiscard]] auto activeAction() const noexcept -> std::optional<PushToTalkAction>;
    void onPushToTalkActionChanged(PushToTalkAction action, bool pressed) override;

  private:
    IPushToTalkTarget& target_;
    mutable std::mutex mutex_;
    IPushToTalkInputObserver* observer_{nullptr};
    std::optional<PushToTalkAction> active_action_;
};
} // namespace hvc::client
