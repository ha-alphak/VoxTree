#include <cstdio>
#include <exception>
#include <hvc/client/win_raw_input.hpp>
#include <mutex>
#include <string>
#include <vector>

namespace
{
using namespace hvc::client;

class CollectingInputSink final : public IInputEventSink
{
  public:
    void onInputDeviceConnected(const InputDeviceProfile& profile) override
    {
        std::scoped_lock lock{mutex_};
        profiles_.push_back(profile);
    }

    void onInputEvent(const InputEvent&) override
    {
    }

    void onInputDeviceRemoved(const std::string&) override
    {
    }

    [[nodiscard]] auto profiles() const -> std::vector<InputDeviceProfile>
    {
        std::scoped_lock lock{mutex_};
        return profiles_;
    }

  private:
    mutable std::mutex mutex_;
    std::vector<InputDeviceProfile> profiles_;
};

auto testSourceLifecycle() -> bool
{
    CollectingInputSink sink;
    WinRawInputSource source{sink};
    if (!source.start() || !source.running() || !source.start() || !source.running())
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
    for (const auto& profile : sink.profiles())
    {
        if (profile.device_id.empty() || profile.display_name.empty() ||
            (profile.device_kind == InputDeviceKind::game_controller &&
             (profile.usage_page == 0 || profile.usage == 0)))
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
            std::fputs("The Win32 Raw Input lifecycle assertion failed.\n", stderr);
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
