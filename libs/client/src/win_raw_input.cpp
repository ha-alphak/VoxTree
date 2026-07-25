#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <bit>
#include <condition_variable>
#include <hvc/client/win_raw_input.hpp>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>

namespace hvc::client
{
namespace
{
constexpr auto window_class_name = L"HvcRawInputMessageWindow";

[[nodiscard]] auto utf8FromWide(std::wstring_view value) -> std::string
{
    if (value.empty())
    {
        return {};
    }
    const auto required =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0)
    {
        return {};
    }
    std::string converted(static_cast<std::size_t>(required), '\0');
    const auto written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                             static_cast<int>(value.size()), converted.data(),
                                             required, nullptr, nullptr);
    if (written != required)
    {
        return {};
    }
    return converted;
}

[[nodiscard]] auto errorMessage(std::string_view operation, DWORD error) -> std::string
{
    return std::string{operation} + " failed with Windows error " + std::to_string(error);
}
} // namespace

auto RawInputResult::success() -> RawInputResult
{
    return {true, 0, {}};
}

auto RawInputResult::failure(std::uint32_t system_error, std::string message) -> RawInputResult
{
    return {false, system_error, std::move(message)};
}

RawInputResult::operator bool() const noexcept
{
    return successful;
}

class WinRawInputSource::Impl final
{
  public:
    explicit Impl(IInputEventSink& sink) : sink_(sink)
    {
    }

    ~Impl()
    {
        stop();
    }

    [[nodiscard]] auto start() -> RawInputResult
    {
        std::unique_lock lock{state_mutex_};
        if (running_)
        {
            return RawInputResult::success();
        }
        lock.unlock();
        stop();
        lock.lock();

        startup_complete_ = false;
        startup_result_ =
            RawInputResult::failure(ERROR_GEN_FAILURE, "raw input startup did not complete");
        try
        {
            thread_ = std::thread{[this] { runMessageLoop(); }};
        }
        catch (const std::system_error& error)
        {
            return RawInputResult::failure(ERROR_NOT_ENOUGH_MEMORY,
                                           std::string{"could not create the raw input thread: "} +
                                               error.what());
        }

        state_changed_.wait(lock, [this] { return startup_complete_; });
        return startup_result_;
    }

    void stop() noexcept
    {
        HWND current_window = nullptr;
        DWORD current_thread_id = 0;
        {
            std::scoped_lock lock{state_mutex_};
            current_window = window_;
            current_thread_id = thread_id_;
        }

        if (current_window != nullptr)
        {
            static_cast<void>(PostMessageW(current_window, WM_CLOSE, 0, 0));
        }
        else if (current_thread_id != 0)
        {
            static_cast<void>(PostThreadMessageW(current_thread_id, WM_QUIT, 0, 0));
        }
        if (thread_.joinable())
        {
            thread_.join();
        }
    }

    [[nodiscard]] auto running() const noexcept -> bool
    {
        std::scoped_lock lock{state_mutex_};
        return running_;
    }

  private:
    static auto CALLBACK windowProcedure(HWND window, UINT message, WPARAM word_parameter,
                                         LPARAM long_parameter) -> LRESULT
    {
        Impl* implementation = nullptr;
        if (message == WM_NCCREATE)
        {
            const auto* create = std::bit_cast<const CREATESTRUCTW*>(long_parameter);
            implementation = static_cast<Impl*>(create->lpCreateParams);
            SetLastError(ERROR_SUCCESS);
            const auto previous = SetWindowLongPtrW(window, GWLP_USERDATA,
                                                    reinterpret_cast<LONG_PTR>(implementation));
            if (previous == 0 && GetLastError() != ERROR_SUCCESS)
            {
                return FALSE;
            }
        }
        else
        {
            implementation = std::bit_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        }

        if (implementation != nullptr)
        {
            return implementation->handleMessage(window, message, word_parameter, long_parameter);
        }
        return DefWindowProcW(window, message, word_parameter, long_parameter);
    }

    [[nodiscard]] static auto registerWindowClass() -> RawInputResult
    {
        const auto instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = windowProcedure;
        window_class.hInstance = instance;
        window_class.lpszClassName = window_class_name;
        if (RegisterClassExW(&window_class) != 0)
        {
            return RawInputResult::success();
        }
        const auto error = GetLastError();
        if (error == ERROR_CLASS_ALREADY_EXISTS)
        {
            return RawInputResult::success();
        }
        return RawInputResult::failure(error, errorMessage("RegisterClassExW", error));
    }

    [[nodiscard]] static auto registerDevices(HWND window) -> RawInputResult
    {
        std::array<RAWINPUTDEVICE, 2> devices{};
        devices[0].usUsagePage = 0x01;
        devices[0].usUsage = 0x06;
        devices[0].dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
        devices[0].hwndTarget = window;
        devices[1].usUsagePage = 0x01;
        devices[1].usUsage = 0x02;
        devices[1].dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
        devices[1].hwndTarget = window;
        if (RegisterRawInputDevices(devices.data(), static_cast<UINT>(devices.size()),
                                    sizeof(RAWINPUTDEVICE)) != FALSE)
        {
            return RawInputResult::success();
        }
        const auto error = GetLastError();
        return RawInputResult::failure(error, errorMessage("RegisterRawInputDevices", error));
    }

    void completeStartup(RawInputResult result, HWND window, bool running)
    {
        {
            std::scoped_lock lock{state_mutex_};
            startup_result_ = std::move(result);
            startup_complete_ = true;
            window_ = window;
            running_ = running;
        }
        state_changed_.notify_all();
    }

    void runMessageLoop()
    {
        {
            std::scoped_lock lock{state_mutex_};
            thread_id_ = GetCurrentThreadId();
        }

        auto class_result = registerWindowClass();
        if (!class_result)
        {
            completeStartup(std::move(class_result), nullptr, false);
            finishThread();
            return;
        }

        const auto instance = GetModuleHandleW(nullptr);
        const auto window = CreateWindowExW(0, window_class_name, L"HVC Raw Input", 0, 0, 0, 0, 0,
                                            HWND_MESSAGE, nullptr, instance, this);
        if (window == nullptr)
        {
            const auto error = GetLastError();
            completeStartup(RawInputResult::failure(error, errorMessage("CreateWindowExW", error)),
                            nullptr, false);
            finishThread();
            return;
        }

        auto registration = registerDevices(window);
        if (!registration)
        {
            completeStartup(std::move(registration), window, false);
            DestroyWindow(window);
            finishThread();
            return;
        }
        completeStartup(RawInputResult::success(), window, true);

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        finishThread();
    }

    void finishThread()
    {
        {
            std::scoped_lock lock{state_mutex_};
            window_ = nullptr;
            thread_id_ = 0;
            running_ = false;
        }
        state_changed_.notify_all();
    }

    [[nodiscard]] auto handleMessage(HWND window, UINT message, WPARAM word_parameter,
                                     LPARAM long_parameter) -> LRESULT
    {
        switch (message)
        {
        case WM_INPUT:
            processRawInput(std::bit_cast<HRAWINPUT>(long_parameter));
            return DefWindowProcW(window, message, word_parameter, long_parameter);
        case WM_INPUT_DEVICE_CHANGE:
            if (word_parameter == GIDC_REMOVAL)
            {
                removeDevice(std::bit_cast<HANDLE>(long_parameter));
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, word_parameter, long_parameter);
        }
    }

    void processRawInput(HRAWINPUT input)
    {
        RAWINPUT raw_input{};
        UINT input_size = sizeof(raw_input);
        if (GetRawInputData(input, RID_INPUT, &raw_input, &input_size, sizeof(RAWINPUTHEADER)) ==
            static_cast<UINT>(-1))
        {
            return;
        }

        const auto device_id = deviceId(raw_input.header.hDevice);
        if (device_id.empty())
        {
            return;
        }
        if (raw_input.header.dwType == RIM_TYPEKEYBOARD)
        {
            // RAWINPUT is a tagged Win32 union selected by dwType.
            processKeyboard(
                raw_input.data.keyboard, // NOLINT(cppcoreguidelines-pro-type-union-access)
                device_id);
        }
        else if (raw_input.header.dwType == RIM_TYPEMOUSE)
        {
            // RAWINPUT is a tagged Win32 union selected by dwType.
            processMouse(raw_input.data.mouse, // NOLINT(cppcoreguidelines-pro-type-union-access)
                         device_id);
        }
    }

    void processKeyboard(const RAWKEYBOARD& keyboard, const std::string& device_id)
    {
        if (keyboard.VKey == 0 || keyboard.VKey == 0xFF)
        {
            return;
        }
        const auto code = static_cast<std::uint16_t>(keyboard.VKey);
        const auto extended = (keyboard.Flags & (RI_KEY_E0 | RI_KEY_E1)) != 0;
        const auto pressed = (keyboard.Flags & RI_KEY_BREAK) == 0;
        sink_.onInputEvent({{InputDeviceKind::keyboard, code, extended, device_id}, pressed});
    }

    void processMouse(const RAWMOUSE& mouse, const std::string& device_id)
    {
        // RAWMOUSE exposes button flags through its documented anonymous union.
        const auto flags = mouse.usButtonFlags; // NOLINT(cppcoreguidelines-pro-type-union-access)
        emitMouseButton(flags, RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP,
                        MouseButton::left, device_id);
        emitMouseButton(flags, RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP,
                        MouseButton::right, device_id);
        emitMouseButton(flags, RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP,
                        MouseButton::middle, device_id);
        emitMouseButton(flags, RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP, MouseButton::button_4,
                        device_id);
        emitMouseButton(flags, RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP, MouseButton::button_5,
                        device_id);
    }

    void emitMouseButton(USHORT flags, USHORT down_flag, USHORT up_flag, MouseButton button,
                         const std::string& device_id)
    {
        if ((flags & down_flag) != 0)
        {
            sink_.onInputEvent(
                {{InputDeviceKind::mouse, static_cast<std::uint16_t>(button), false, device_id},
                 true});
        }
        if ((flags & up_flag) != 0)
        {
            sink_.onInputEvent(
                {{InputDeviceKind::mouse, static_cast<std::uint16_t>(button), false, device_id},
                 false});
        }
    }

    [[nodiscard]] auto deviceId(HANDLE device) -> std::string
    {
        if (const auto existing = device_ids_.find(device); existing != device_ids_.end())
        {
            return existing->second;
        }

        UINT character_count = 0;
        if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, nullptr, &character_count) ==
                static_cast<UINT>(-1) ||
            character_count == 0)
        {
            return {};
        }
        std::wstring name(character_count, L'\0');
        if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, name.data(), &character_count) ==
            static_cast<UINT>(-1))
        {
            return {};
        }
        if (!name.empty() && name.back() == L'\0')
        {
            name.pop_back();
        }
        auto identifier = utf8FromWide(name);
        if (!identifier.empty())
        {
            device_ids_.emplace(device, identifier);
        }
        return identifier;
    }

    void removeDevice(HANDLE device)
    {
        const auto existing = device_ids_.find(device);
        if (existing == device_ids_.end())
        {
            return;
        }
        auto identifier = std::move(existing->second);
        device_ids_.erase(existing);
        sink_.onInputDeviceRemoved(identifier);
    }

    IInputEventSink& sink_;
    mutable std::mutex state_mutex_;
    std::condition_variable state_changed_;
    std::thread thread_;
    HWND window_{nullptr};
    DWORD thread_id_{0};
    bool running_{false};
    bool startup_complete_{false};
    RawInputResult startup_result_;
    std::unordered_map<HANDLE, std::string> device_ids_;
};

WinRawInputSource::WinRawInputSource(IInputEventSink& sink) : impl_(std::make_unique<Impl>(sink))
{
}

WinRawInputSource::~WinRawInputSource() = default;

auto WinRawInputSource::start() -> RawInputResult
{
    return impl_->start();
}

void WinRawInputSource::stop() noexcept
{
    impl_->stop();
}

auto WinRawInputSource::running() const noexcept -> bool
{
    return impl_->running();
}
} // namespace hvc::client
