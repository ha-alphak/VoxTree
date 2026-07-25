#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <condition_variable>
#include <cstddef>
#include <hidsdi.h>
#include <hvc/client/win_raw_input.hpp>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace hvc::client
{
namespace
{
constexpr auto window_class_name = L"HvcRawInputMessageWindow";
constexpr auto hid_button_usage_page = std::uint16_t{0x09};

struct HidReportGroup final
{
    USAGE usage_page{0};
    USHORT link_collection{0};
    UCHAR report_id{0};

    [[nodiscard]] auto operator==(const HidReportGroup&) const -> bool = default;
};

// The implicit moves contain only standard containers and are required by the device map.
// NOLINTNEXTLINE(bugprone-exception-escape)
struct HidDeviceState final
{
    std::vector<std::max_align_t> preparsed_data;
    std::vector<HidReportGroup> report_groups;
    std::unordered_map<UCHAR, std::unordered_set<std::uint32_t>> pressed_by_report;
};

// The implicit move delegates to HidDeviceState and InputDeviceProfile.
// NOLINTNEXTLINE(bugprone-exception-escape)
struct RawDeviceState final
{
    InputDeviceProfile profile;
    std::optional<HidDeviceState> hid;
};

[[nodiscard]] auto hidButtonKey(USAGE usage_page, USAGE usage) noexcept -> std::uint32_t
{
    return (static_cast<std::uint32_t>(usage_page) << 16U) | static_cast<std::uint32_t>(usage);
}

[[nodiscard]] auto hidUsagePage(std::uint32_t key) noexcept -> std::uint16_t
{
    return static_cast<std::uint16_t>(key >> 16U);
}

[[nodiscard]] auto hidUsage(std::uint32_t key) noexcept -> std::uint16_t
{
    return static_cast<std::uint16_t>(key & 0xFFFFU);
}

[[nodiscard]] auto isSupportedControllerUsage(USHORT usage_page, USHORT usage) noexcept -> bool
{
    return usage_page == HID_USAGE_PAGE_GENERIC &&
           (usage == HID_USAGE_GENERIC_JOYSTICK || usage == HID_USAGE_GENERIC_GAMEPAD ||
            usage == HID_USAGE_GENERIC_MULTI_AXIS_CONTROLLER);
}

[[nodiscard]] auto controllerName(USHORT usage) -> std::string
{
    switch (usage)
    {
    case HID_USAGE_GENERIC_GAMEPAD:
        return "Gamepad";
    case HID_USAGE_GENERIC_MULTI_AXIS_CONTROLLER:
        return "Multi-axis controller";
    case HID_USAGE_GENERIC_JOYSTICK:
        return "Joystick/HOTAS";
    default:
        return "HID controller";
    }
}

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

    [[nodiscard]] auto statistics() const noexcept -> RawInputStatistics
    {
        return {input_messages_.load(std::memory_order_relaxed),
                delivered_events_.load(std::memory_order_relaxed)};
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
        std::array<RAWINPUTDEVICE, 5> devices{};
        devices[0].usUsagePage = 0x01;
        devices[0].usUsage = 0x06;
        devices[1].usUsagePage = 0x01;
        devices[1].usUsage = 0x02;
        devices[2].usUsagePage = HID_USAGE_PAGE_GENERIC;
        devices[2].usUsage = HID_USAGE_GENERIC_JOYSTICK;
        devices[3].usUsagePage = HID_USAGE_PAGE_GENERIC;
        devices[3].usUsage = HID_USAGE_GENERIC_GAMEPAD;
        devices[4].usUsagePage = HID_USAGE_PAGE_GENERIC;
        devices[4].usUsage = HID_USAGE_GENERIC_MULTI_AXIS_CONTROLLER;
        for (auto& device : devices)
        {
            device.dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
            device.hwndTarget = window;
        }
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
        const auto window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, window_class_name,
                                            L"HVC Raw Input", WS_POPUP, 0, 0, 0, 0, nullptr,
                                            nullptr, instance, this);
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
        enumerateDevices();
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
            input_messages_.fetch_add(1, std::memory_order_relaxed);
            processRawInput(std::bit_cast<HRAWINPUT>(long_parameter),
                            GET_RAWINPUT_CODE_WPARAM(word_parameter) == RIM_INPUTSINK);
            return DefWindowProcW(window, message, word_parameter, long_parameter);
        case WM_INPUT_DEVICE_CHANGE:
            if (word_parameter == GIDC_ARRIVAL)
            {
                static_cast<void>(ensureDevice(std::bit_cast<HANDLE>(long_parameter)));
            }
            else if (word_parameter == GIDC_REMOVAL)
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

    void processRawInput(HRAWINPUT input, bool received_in_background)
    {
        UINT input_size = 0;
        if (GetRawInputData(input, RID_INPUT, nullptr, &input_size, sizeof(RAWINPUTHEADER)) != 0 ||
            input_size < sizeof(RAWINPUTHEADER))
        {
            return;
        }

        const auto storage_size =
            (static_cast<std::size_t>(input_size) + sizeof(std::max_align_t) - 1U) /
            sizeof(std::max_align_t);
        std::vector<std::max_align_t> storage(storage_size);
        if (GetRawInputData(input, RID_INPUT, storage.data(), &input_size,
                            sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1))
        {
            return;
        }

        const auto* raw_input = reinterpret_cast<const RAWINPUT*>(storage.data());
        auto* device = ensureDevice(raw_input->header.hDevice);
        if (device == nullptr)
        {
            return;
        }
        if (raw_input->header.dwType == RIM_TYPEKEYBOARD)
        {
            // RAWINPUT is a tagged Win32 union selected by dwType.
            processKeyboard(
                raw_input->data.keyboard, // NOLINT(cppcoreguidelines-pro-type-union-access)
                device->profile.device_id, received_in_background);
        }
        else if (raw_input->header.dwType == RIM_TYPEMOUSE)
        {
            // RAWINPUT is a tagged Win32 union selected by dwType.
            processMouse(raw_input->data.mouse, // NOLINT(cppcoreguidelines-pro-type-union-access)
                         device->profile.device_id, received_in_background);
        }
        else if (raw_input->header.dwType == RIM_TYPEHID && device->hid.has_value())
        {
            // RAWINPUT is a tagged Win32 union selected by dwType.
            processHid(raw_input->data.hid, // NOLINT(cppcoreguidelines-pro-type-union-access)
                       input_size, *device, received_in_background);
        }
    }

    void processKeyboard(const RAWKEYBOARD& keyboard, const std::string& device_id,
                         bool received_in_background)
    {
        if (keyboard.VKey == 0 || keyboard.VKey == 0xFF)
        {
            return;
        }
        const auto code = static_cast<std::uint16_t>(keyboard.VKey);
        const auto extended = (keyboard.Flags & (RI_KEY_E0 | RI_KEY_E1)) != 0;
        const auto pressed = (keyboard.Flags & RI_KEY_BREAK) == 0;
        sink_.onInputEvent({{InputDeviceKind::keyboard, 0, code, extended, device_id},
                            pressed,
                            received_in_background});
        delivered_events_.fetch_add(1, std::memory_order_relaxed);
    }

    void processMouse(const RAWMOUSE& mouse, const std::string& device_id,
                      bool received_in_background)
    {
        // RAWMOUSE exposes button flags through its documented anonymous union.
        const auto flags = mouse.usButtonFlags; // NOLINT(cppcoreguidelines-pro-type-union-access)
        emitMouseButton(flags, RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP,
                        MouseButton::left, device_id, received_in_background);
        emitMouseButton(flags, RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP,
                        MouseButton::right, device_id, received_in_background);
        emitMouseButton(flags, RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP,
                        MouseButton::middle, device_id, received_in_background);
        emitMouseButton(flags, RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP, MouseButton::button_4,
                        device_id, received_in_background);
        emitMouseButton(flags, RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP, MouseButton::button_5,
                        device_id, received_in_background);
    }

    void emitMouseButton(USHORT flags, USHORT down_flag, USHORT up_flag, MouseButton button,
                         const std::string& device_id, bool received_in_background)
    {
        if ((flags & down_flag) != 0)
        {
            sink_.onInputEvent(
                {{InputDeviceKind::mouse, 0, static_cast<std::uint16_t>(button), false, device_id},
                 true,
                 received_in_background});
            delivered_events_.fetch_add(1, std::memory_order_relaxed);
        }
        if ((flags & up_flag) != 0)
        {
            sink_.onInputEvent(
                {{InputDeviceKind::mouse, 0, static_cast<std::uint16_t>(button), false, device_id},
                 false,
                 received_in_background});
            delivered_events_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    [[nodiscard]] static auto preparsedData(HidDeviceState& state) noexcept -> PHIDP_PREPARSED_DATA
    {
        return reinterpret_cast<PHIDP_PREPARSED_DATA>(state.preparsed_data.data());
    }

    [[nodiscard]] static auto allPressedButtons(const HidDeviceState& state)
        -> std::unordered_set<std::uint32_t>
    {
        std::unordered_set<std::uint32_t> pressed;
        for (const auto& [report_id, report_buttons] : state.pressed_by_report)
        {
            static_cast<void>(report_id);
            pressed.insert(report_buttons.begin(), report_buttons.end());
        }
        return pressed;
    }

    void processHid(const RAWHID& raw_hid, UINT input_size, RawDeviceState& device,
                    bool received_in_background)
    {
        const auto report_size = static_cast<std::size_t>(raw_hid.dwSizeHid);
        const auto report_count = static_cast<std::size_t>(raw_hid.dwCount);
        const auto hid_header_size = offsetof(RAWHID, bRawData);
        if (report_size == 0 || report_count == 0 ||
            report_count > (std::numeric_limits<std::size_t>::max() / report_size))
        {
            return;
        }
        const auto report_bytes = report_size * report_count;
        const auto available_bytes = static_cast<std::size_t>(input_size) - sizeof(RAWINPUTHEADER);
        if (available_bytes < hid_header_size || report_bytes > (available_bytes - hid_header_size))
        {
            return;
        }

        const auto* report_data = reinterpret_cast<const std::byte*>(raw_hid.bRawData);
        const std::span reports{report_data, report_bytes};
        for (std::size_t report_index = 0; report_index < report_count; ++report_index)
        {
            processHidReport(*device.hid, device.profile.device_id,
                             reports.subspan(report_index * report_size, report_size),
                             received_in_background);
        }
    }

    void processHidReport(HidDeviceState& state, const std::string& device_id,
                          std::span<const std::byte> report, bool received_in_background)
    {
        const auto old_pressed = allPressedButtons(state);
        const auto has_report_ids = std::ranges::any_of(
            state.report_groups, [](const HidReportGroup& group) { return group.report_id != 0; });
        if (has_report_ids && report.empty())
        {
            return;
        }
        const auto report_id =
            has_report_ids ? std::to_integer<UCHAR>(report.front()) : static_cast<UCHAR>(0);
        std::unordered_set<std::uint32_t> report_pressed;
        auto* preparsed = preparsedData(state);
        bool matching_group_seen = false;
        bool parsed_group_seen = false;

        for (const auto& group : state.report_groups)
        {
            if (group.report_id != report_id)
            {
                continue;
            }
            matching_group_seen = true;
            const auto maximum = HidP_MaxUsageListLength(HidP_Input, group.usage_page, preparsed);
            if (maximum == 0)
            {
                continue;
            }
            std::vector<USAGE> usages(maximum);
            ULONG usage_count = maximum;
            const auto status = HidP_GetUsages(
                HidP_Input, group.usage_page, group.link_collection, usages.data(), &usage_count,
                preparsed,
                const_cast<PCHAR>(reinterpret_cast<const CHAR*>(report.data())), // NOLINT
                static_cast<ULONG>(report.size()));
            if (status != HIDP_STATUS_SUCCESS)
            {
                continue;
            }
            parsed_group_seen = true;
            for (ULONG usage_index = 0; usage_index < usage_count; ++usage_index)
            {
                report_pressed.insert(hidButtonKey(group.usage_page, usages[usage_index]));
            }
        }

        if (!matching_group_seen || !parsed_group_seen)
        {
            return;
        }
        state.pressed_by_report[report_id] = std::move(report_pressed);
        const auto new_pressed = allPressedButtons(state);
        for (const auto key : old_pressed)
        {
            if (!new_pressed.contains(key))
            {
                emitHidButton(device_id, key, false, received_in_background);
            }
        }
        for (const auto key : new_pressed)
        {
            if (!old_pressed.contains(key))
            {
                emitHidButton(device_id, key, true, received_in_background);
            }
        }
    }

    void emitHidButton(const std::string& device_id, std::uint32_t key, bool pressed,
                       bool received_in_background)
    {
        sink_.onInputEvent(
            {{InputDeviceKind::game_controller, hidUsagePage(key), hidUsage(key), false, device_id},
             pressed,
             received_in_background});
        delivered_events_.fetch_add(1, std::memory_order_relaxed);
    }

    [[nodiscard]] static auto deviceName(HANDLE device) -> std::string
    {
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
        return utf8FromWide(name);
    }

    [[nodiscard]] static auto deviceInfo(HANDLE device) -> std::optional<RID_DEVICE_INFO>
    {
        RID_DEVICE_INFO info{};
        info.cbSize = sizeof(info);
        UINT info_size = sizeof(info);
        if (GetRawInputDeviceInfoW(device, RIDI_DEVICEINFO, &info, &info_size) ==
            static_cast<UINT>(-1))
        {
            return std::nullopt;
        }
        return info;
    }

    [[nodiscard]] static auto createHidState(HANDLE device, InputDeviceProfile& profile)
        -> std::optional<HidDeviceState>
    {
        UINT preparsed_size = 0;
        if (GetRawInputDeviceInfoW(device, RIDI_PREPARSEDDATA, nullptr, &preparsed_size) != 0 ||
            preparsed_size == 0)
        {
            return std::nullopt;
        }

        HidDeviceState state;
        const auto storage_size =
            (static_cast<std::size_t>(preparsed_size) + sizeof(std::max_align_t) - 1U) /
            sizeof(std::max_align_t);
        state.preparsed_data.resize(storage_size);
        if (GetRawInputDeviceInfoW(device, RIDI_PREPARSEDDATA, state.preparsed_data.data(),
                                   &preparsed_size) == static_cast<UINT>(-1))
        {
            return std::nullopt;
        }

        HIDP_CAPS caps{};
        auto* preparsed = preparsedData(state);
        if (HidP_GetCaps(preparsed, &caps) != HIDP_STATUS_SUCCESS)
        {
            return std::nullopt;
        }
        std::vector<HIDP_BUTTON_CAPS> button_caps(caps.NumberInputButtonCaps);
        USHORT button_cap_count = caps.NumberInputButtonCaps;
        if (button_cap_count != 0 &&
            HidP_GetButtonCaps(HidP_Input, button_caps.data(), &button_cap_count, preparsed) !=
                HIDP_STATUS_SUCCESS)
        {
            return std::nullopt;
        }
        button_caps.resize(button_cap_count);

        for (const auto& button_cap : button_caps)
        {
            const HidReportGroup group{button_cap.UsagePage, button_cap.LinkCollection,
                                       button_cap.ReportID};
            if (std::ranges::find(state.report_groups, group) == state.report_groups.end())
            {
                state.report_groups.push_back(group);
            }

            if (button_cap.IsRange != FALSE)
            {
                // HIDP_BUTTON_CAPS is a tagged Windows union selected by IsRange.
                const auto first =
                    button_cap.Range.UsageMin; // NOLINT(cppcoreguidelines-pro-type-union-access)
                const auto last =
                    button_cap.Range.UsageMax; // NOLINT(cppcoreguidelines-pro-type-union-access)
                for (std::uint32_t usage = first; usage <= last; ++usage)
                {
                    profile.buttons.push_back(
                        {button_cap.UsagePage, static_cast<std::uint16_t>(usage)});
                }
            }
            else
            {
                // HIDP_BUTTON_CAPS is a tagged Windows union selected by IsRange.
                profile.buttons.push_back(
                    {button_cap.UsagePage,
                     button_cap.NotRange.Usage}); // NOLINT(cppcoreguidelines-pro-type-union-access)
            }
        }
        std::ranges::sort(profile.buttons, {}, [](const HidButtonDescriptor& button) {
            return std::pair{button.usage_page, button.usage};
        });
        profile.buttons.erase(std::ranges::unique(profile.buttons).begin(), profile.buttons.end());
        return state;
    }

    [[nodiscard]] auto ensureDevice(HANDLE device) -> RawDeviceState*
    {
        if (const auto existing = devices_.find(device); existing != devices_.end())
        {
            return &existing->second;
        }

        const auto identifier = deviceName(device);
        const auto info = deviceInfo(device);
        if (identifier.empty() || !info.has_value())
        {
            return nullptr;
        }

        RawDeviceState state;
        state.profile.device_id = identifier;
        if (info->dwType == RIM_TYPEKEYBOARD)
        {
            state.profile.display_name = "Keyboard";
            state.profile.device_kind = InputDeviceKind::keyboard;
            state.profile.usage_page = HID_USAGE_PAGE_GENERIC;
            state.profile.usage = HID_USAGE_GENERIC_KEYBOARD;
        }
        else if (info->dwType == RIM_TYPEMOUSE)
        {
            state.profile.display_name = "Mouse";
            state.profile.device_kind = InputDeviceKind::mouse;
            state.profile.usage_page = HID_USAGE_PAGE_GENERIC;
            state.profile.usage = HID_USAGE_GENERIC_MOUSE;
        }
        else if (info->dwType == RIM_TYPEHID)
        {
            // RID_DEVICE_INFO is a tagged Win32 union selected by dwType.
            const auto& hid_info = info->hid; // NOLINT(cppcoreguidelines-pro-type-union-access)
            if (!isSupportedControllerUsage(hid_info.usUsagePage, hid_info.usUsage))
            {
                return nullptr;
            }
            state.profile.display_name = controllerName(hid_info.usUsage);
            state.profile.device_kind = InputDeviceKind::game_controller;
            state.profile.vendor_id = static_cast<std::uint16_t>(hid_info.dwVendorId);
            state.profile.product_id = static_cast<std::uint16_t>(hid_info.dwProductId);
            state.profile.usage_page = hid_info.usUsagePage;
            state.profile.usage = hid_info.usUsage;
            state.hid = createHidState(device, state.profile);
            if (!state.hid.has_value())
            {
                return nullptr;
            }
        }
        else
        {
            return nullptr;
        }

        auto [inserted, created] = devices_.emplace(device, std::move(state));
        static_cast<void>(created);
        sink_.onInputDeviceConnected(inserted->second.profile);
        return &inserted->second;
    }

    void enumerateDevices()
    {
        UINT device_count = 0;
        if (GetRawInputDeviceList(nullptr, &device_count, sizeof(RAWINPUTDEVICELIST)) != 0 ||
            device_count == 0)
        {
            return;
        }
        std::vector<RAWINPUTDEVICELIST> device_list(device_count);
        const auto listed =
            GetRawInputDeviceList(device_list.data(), &device_count, sizeof(RAWINPUTDEVICELIST));
        if (listed == static_cast<UINT>(-1))
        {
            return;
        }
        for (UINT index = 0; index < listed; ++index)
        {
            static_cast<void>(ensureDevice(device_list[index].hDevice));
        }
    }

    void removeDevice(HANDLE device)
    {
        const auto existing = devices_.find(device);
        if (existing == devices_.end())
        {
            return;
        }
        auto identifier = std::move(existing->second.profile.device_id);
        devices_.erase(existing);
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
    std::atomic_uint64_t input_messages_{0};
    std::atomic_uint64_t delivered_events_{0};
    std::unordered_map<HANDLE, RawDeviceState> devices_;
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

auto WinRawInputSource::statistics() const noexcept -> RawInputStatistics
{
    return impl_->statistics();
}
} // namespace hvc::client
