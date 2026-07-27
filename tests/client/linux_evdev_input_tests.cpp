#include <chrono>
#include <cstdio>
#include <exception>
#include <hvc/client/linux_evdev_input.hpp>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
using namespace hvc::client;

class CollectingInputSink final : public IInputEventSink
{
  public:
    void onInputDeviceConnected(const InputDeviceProfile& profile) override
    {
        const std::scoped_lock lock{mutex_};
        profiles_.push_back(profile);
    }

    void onInputEvent(const InputEvent& event) override
    {
        const std::scoped_lock lock{mutex_};
        events_.push_back(event);
    }

    void onInputDeviceRemoved(const std::string&) override
    {
    }

    [[nodiscard]] auto validProfiles() const -> bool
    {
        const std::scoped_lock lock{mutex_};
        for (const auto& profile : profiles_)
        {
            if (profile.device_kind != InputDeviceKind::game_controller ||
                profile.device_id.empty() || profile.display_name.empty() ||
                profile.usage_page == 0 || profile.usage == 0 || profile.buttons.empty())
            {
                return false;
            }
            for (const auto& button : profile.buttons)
            {
                if (button.usage_page == 0 || button.usage == 0)
                {
                    return false;
                }
            }
        }
        return true;
    }

  private:
    mutable std::mutex mutex_;
    std::vector<InputDeviceProfile> profiles_;
    std::vector<InputEvent> events_;
};

[[nodiscard]] auto testSourceLifecycle() -> bool
{
    CollectingInputSink sink;
    LinuxEvdevInputSource source{sink};
    if (!source.start() || !source.running() || !source.start())
    {
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{150});
    if (!sink.validProfiles())
    {
        return false;
    }
    source.stop();
    if (source.running())
    {
        return false;
    }
    if (!source.start() || !source.running())
    {
        return false;
    }
    source.stop();
    return !source.running();
}
} // namespace

auto main() noexcept -> int
{
    try
    {
        if (!testSourceLifecycle())
        {
            std::fputs("The Linux evdev input lifecycle assertion failed.\n", stderr);
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
