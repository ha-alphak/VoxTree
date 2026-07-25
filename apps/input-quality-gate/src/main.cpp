#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <chrono>
#include <cstdio>
#include <exception>
#include <hvc/client/win_raw_input.hpp>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace
{
using namespace hvc::client;

struct Arguments final
{
    std::chrono::seconds observe_for{10};
    bool require_background_event{false};
    bool require_background_controller_event{false};
    bool require_controller{false};
};

void printUsage()
{
    std::fputs("Usage: hvc-input-quality-gate [--observe <seconds>] "
               "[--require-background-event] [--require-background-controller-event] "
               "[--require-controller]\n",
               stderr);
}

[[nodiscard]] auto parseArguments(int argument_count, char** arguments) -> Arguments
{
    Arguments parsed;
    for (int index = 1; index < argument_count; ++index)
    {
        const std::string_view option{arguments[index]};
        if (option == "--observe" && index + 1 < argument_count)
        {
            parsed.observe_for = std::chrono::seconds{std::stoll(arguments[++index])};
        }
        else if (option == "--require-background-event")
        {
            parsed.require_background_event = true;
        }
        else if (option == "--require-background-controller-event")
        {
            parsed.require_background_controller_event = true;
        }
        else if (option == "--require-controller")
        {
            parsed.require_controller = true;
        }
        else
        {
            throw std::invalid_argument{"unknown or incomplete command-line option"};
        }
    }
    if (parsed.observe_for.count() < 1 || parsed.observe_for.count() > 300)
    {
        throw std::invalid_argument{"observation time must be between 1 and 300 seconds"};
    }
    return parsed;
}

[[nodiscard]] auto kindName(InputDeviceKind kind) -> std::string_view
{
    switch (kind)
    {
    case InputDeviceKind::keyboard:
        return "keyboard";
    case InputDeviceKind::mouse:
        return "mouse";
    case InputDeviceKind::game_controller:
        return "game-controller";
    }
    return "unknown";
}

class ProbeSink final : public IInputEventSink
{
  public:
    void onInputDeviceConnected(const InputDeviceProfile& profile) override
    {
        std::scoped_lock lock{mutex_};
        if (profile.device_kind == InputDeviceKind::game_controller)
        {
            controller_seen_ = true;
        }
        std::printf(
            "DEVICE connected kind=%.*s vid=%u pid=%u usage=%u:%u buttons=%zu id=%s\n",
            static_cast<int>(kindName(profile.device_kind).size()),
            kindName(profile.device_kind).data(), static_cast<unsigned int>(profile.vendor_id),
            static_cast<unsigned int>(profile.product_id),
            static_cast<unsigned int>(profile.usage_page), static_cast<unsigned int>(profile.usage),
            profile.buttons.size(), profile.device_id.c_str());
    }

    void onInputEvent(const InputEvent& event) override
    {
        std::scoped_lock lock{mutex_};
        if (event.pressed)
        {
            pressed_event_seen_ = true;
            background_event_seen_ = background_event_seen_ || event.received_in_background;
            background_controller_event_seen_ =
                background_controller_event_seen_ ||
                (event.received_in_background &&
                 event.control.device_kind == InputDeviceKind::game_controller);
        }
        std::printf("INPUT kind=%.*s page=%u code=%u state=%s background=%s device=%s\n",
                    static_cast<int>(kindName(event.control.device_kind).size()),
                    kindName(event.control.device_kind).data(),
                    static_cast<unsigned int>(event.control.usage_page),
                    static_cast<unsigned int>(event.control.code),
                    event.pressed ? "pressed" : "released",
                    event.received_in_background ? "yes" : "no", event.control.device_id.c_str());
    }

    void onInputDeviceRemoved(const std::string& device_id) override
    {
        std::scoped_lock lock{mutex_};
        std::printf("DEVICE removed id=%s\n", device_id.c_str());
    }

    [[nodiscard]] auto controllerSeen() const -> bool
    {
        std::scoped_lock lock{mutex_};
        return controller_seen_;
    }

    [[nodiscard]] auto pressedEventSeen() const -> bool
    {
        std::scoped_lock lock{mutex_};
        return pressed_event_seen_;
    }

    [[nodiscard]] auto backgroundEventSeen() const -> bool
    {
        std::scoped_lock lock{mutex_};
        return background_event_seen_;
    }

    [[nodiscard]] auto backgroundControllerEventSeen() const -> bool
    {
        std::scoped_lock lock{mutex_};
        return background_controller_event_seen_;
    }

  private:
    mutable std::mutex mutex_;
    bool controller_seen_{false};
    bool pressed_event_seen_{false};
    bool background_event_seen_{false};
    bool background_controller_event_seen_{false};
};
} // namespace

auto main(int argument_count, char** arguments) noexcept -> int
{
    try
    {
        static_cast<void>(std::setvbuf(stdout, nullptr, _IONBF, 0));
        const auto parsed = parseArguments(argument_count, arguments);
        ProbeSink sink;
        WinRawInputSource source{sink};
        const auto started = source.start();
        if (!started)
        {
            std::fprintf(stderr, "Raw Input startup failed (%u): %s\n",
                         static_cast<unsigned int>(started.system_error), started.message.c_str());
            return 1;
        }

        std::printf("Observing physical Raw Input for %lld seconds. Keep another application in "
                    "the foreground and press the desired controls.\n",
                    static_cast<long long>(parsed.observe_for.count()));
        std::this_thread::sleep_for(parsed.observe_for);
        source.stop();
        const auto statistics = source.statistics();
        std::printf("STATS wm_input=%llu delivered=%llu\n",
                    static_cast<unsigned long long>(statistics.input_messages),
                    static_cast<unsigned long long>(statistics.delivered_events));

        if (parsed.require_controller && !sink.controllerSeen())
        {
            std::fputs("FAIL: no supported joystick, gamepad, or multi-axis controller found.\n",
                       stderr);
            return 2;
        }
        if (parsed.require_background_event && !sink.backgroundEventSeen())
        {
            std::fputs("FAIL: no physical input was received while another process owned the "
                       "foreground window.\n",
                       stderr);
            return 3;
        }
        if (parsed.require_background_controller_event && !sink.backgroundControllerEventSeen())
        {
            std::fputs("FAIL: no physical controller button was received through the background "
                       "Raw Input path.\n",
                       stderr);
            return 4;
        }
        if (!sink.pressedEventSeen())
        {
            std::fputs("No pressed input event was observed.\n", stderr);
        }
        std::puts("PASS: Raw Input observation completed.");
        return 0;
    }
    catch (const std::exception& error)
    {
        printUsage();
        std::fprintf(stderr, "Error: %s\n", error.what());
        return 1;
    }
}
