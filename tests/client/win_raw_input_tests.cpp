#include <cstdio>
#include <exception>
#include <hvc/client/win_raw_input.hpp>
#include <string>

namespace
{
using namespace hvc::client;

class NullInputSink final : public IInputEventSink
{
  public:
    void onInputEvent(const InputEvent&) override
    {
    }

    void onInputDeviceRemoved(const std::string&) override
    {
    }
};

auto testSourceLifecycle() -> bool
{
    NullInputSink sink;
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
