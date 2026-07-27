#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <hvc/client/linux_evdev_input.hpp>
#include <libudev.h>
#include <limits>
#include <linux/input.h>
#include <memory>
#include <mutex>
#include <poll.h>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace hvc::client
{
namespace
{
constexpr std::uint16_t hid_button_usage_page = 0x0009;
constexpr auto poll_timeout = std::chrono::milliseconds{100};
constexpr std::size_t bits_per_word = sizeof(unsigned long) * 8U;
constexpr std::size_t key_word_count =
    (static_cast<std::size_t>(KEY_MAX) + bits_per_word) / bits_per_word;

[[nodiscard]] auto bitIsSet(const std::array<unsigned long, key_word_count>& bits,
                            unsigned int code) noexcept -> bool
{
    const auto word = static_cast<std::size_t>(code) / bits_per_word;
    const auto offset = static_cast<std::size_t>(code) % bits_per_word;
    return word < bits.size() && ((bits[word] >> offset) & 1UL) != 0UL;
}

[[nodiscard]] auto textProperty(udev_device* device, const char* name) -> std::string
{
    const auto* const value = udev_device_get_property_value(device, name);
    return value == nullptr ? std::string{} : std::string{value};
}

[[nodiscard]] auto stableDeviceId(udev_device* device, std::string_view device_node) -> std::string
{
    auto identity = textProperty(device, "ID_SERIAL");
    if (identity.empty())
    {
        identity = textProperty(device, "ID_PATH");
    }
    if (identity.empty())
    {
        const auto* const system_path = udev_device_get_syspath(device);
        identity = system_path == nullptr ? std::string{device_node} : std::string{system_path};
    }
    return "linux-input:" + identity;
}

[[nodiscard]] auto isController(udev_device* device) -> bool
{
    return textProperty(device, "ID_INPUT_JOYSTICK") == "1";
}
} // namespace

LinuxEvdevInputResult::operator bool() const noexcept
{
    return error == LinuxEvdevInputError::none;
}

class LinuxEvdevInputSource::Impl final
{
  public:
    explicit Impl(IInputEventSink& sink) : sink_(sink)
    {
    }

    ~Impl()
    {
        stop();
    }

    [[nodiscard]] auto start() -> LinuxEvdevInputResult
    {
        const std::scoped_lock lock{lifecycle_mutex_};
        if (running_.load())
        {
            return {};
        }

        udev_ = udev_new();
        if (udev_ == nullptr)
        {
            return {LinuxEvdevInputError::device_monitor_unavailable,
                    "udev context initialization failed"};
        }
        monitor_ = udev_monitor_new_from_netlink(udev_, "udev");
        if (monitor_ == nullptr ||
            udev_monitor_filter_add_match_subsystem_devtype(monitor_, "input", nullptr) < 0 ||
            udev_monitor_enable_receiving(monitor_) < 0)
        {
            releaseUdev();
            return {LinuxEvdevInputError::device_monitor_unavailable,
                    "udev input hot-plug monitor initialization failed"};
        }

        stop_requested_.store(false);
        try
        {
            worker_ = std::jthread{[this] { run(); }};
        }
        catch (const std::exception& error)
        {
            releaseUdev();
            return {LinuxEvdevInputError::thread_start_failed, error.what()};
        }
        running_.store(true);
        return {};
    }

    void stop() noexcept
    {
        const std::scoped_lock lock{lifecycle_mutex_};
        if (!running_.load() && !worker_.joinable())
        {
            releaseUdev();
            return;
        }
        stop_requested_.store(true);
        worker_.request_stop();
        if (worker_.joinable())
        {
            worker_.join();
        }
        running_.store(false);
        releaseUdev();
    }

    [[nodiscard]] auto running() const noexcept -> bool
    {
        return running_.load();
    }

    [[nodiscard]] auto statistics() const noexcept -> LinuxEvdevInputStatistics
    {
        return {.connected_devices = connected_devices_.load(),
                .inaccessible_devices = inaccessible_devices_.load(),
                .delivered_events = delivered_events_.load(),
                .hot_plug_events = hot_plug_events_.load()};
    }

  private:
    struct Device final
    {
        int descriptor{-1};
        std::string node;
        InputDeviceProfile profile;
        std::vector<std::pair<std::uint16_t, std::uint16_t>> event_code_to_usage;
    };

    void run() noexcept
    {
        enumerateDevices();
        while (!stop_requested_.load())
        {
            std::vector<pollfd> descriptors;
            descriptors.reserve(devices_.size() + 1U);
            descriptors.push_back(
                pollfd{udev_monitor_get_fd(monitor_), static_cast<short>(POLLIN), 0});
            for (const auto& device : devices_)
            {
                descriptors.push_back(pollfd{device.descriptor, static_cast<short>(POLLIN), 0});
            }

            const auto poll_result =
                ::poll(descriptors.data(), static_cast<nfds_t>(descriptors.size()),
                       static_cast<int>(poll_timeout.count()));
            if (poll_result < 0)
            {
                if (errno != EINTR)
                {
                    break;
                }
                continue;
            }
            if (poll_result == 0)
            {
                continue;
            }
            if ((descriptors.front().revents & POLLIN) != 0)
            {
                handleHotPlug();
            }
            const auto device_count = std::min(devices_.size(), descriptors.size() - 1U);
            for (std::size_t index = 0; index < device_count; ++index)
            {
                if ((descriptors[index + 1U].revents & POLLIN) != 0)
                {
                    readEvents(devices_[index]);
                }
            }
        }

        for (auto& device : devices_)
        {
            sink_.onInputDeviceRemoved(device.profile.device_id);
            ::close(device.descriptor);
        }
        devices_.clear();
        connected_devices_.store(0);
        running_.store(false);
    }

    void enumerateDevices() noexcept
    {
        auto* const enumeration = udev_enumerate_new(udev_);
        if (enumeration == nullptr)
        {
            return;
        }
        udev_enumerate_add_match_subsystem(enumeration, "input");
        udev_enumerate_scan_devices(enumeration);
        auto* entries = udev_enumerate_get_list_entry(enumeration);
        udev_list_entry* entry = nullptr;
        udev_list_entry_foreach(entry, entries)
        {
            const auto* const path = udev_list_entry_get_name(entry);
            auto* const device = udev_device_new_from_syspath(udev_, path);
            if (device != nullptr)
            {
                addDevice(device);
                udev_device_unref(device);
            }
        }
        udev_enumerate_unref(enumeration);
    }

    void handleHotPlug() noexcept
    {
        auto* const device = udev_monitor_receive_device(monitor_);
        if (device == nullptr)
        {
            return;
        }
        const auto* const action = udev_device_get_action(device);
        if (action != nullptr && std::string_view{action} == "add")
        {
            addDevice(device);
            hot_plug_events_.fetch_add(1);
        }
        else if (action != nullptr && std::string_view{action} == "remove")
        {
            const auto* const node = udev_device_get_devnode(device);
            if (node != nullptr)
            {
                removeDevice(node);
                hot_plug_events_.fetch_add(1);
            }
        }
        udev_device_unref(device);
    }

    void addDevice(udev_device* device) noexcept
    {
        auto descriptor = -1;
        try
        {
            const auto* const node_value = udev_device_get_devnode(device);
            if (node_value == nullptr || !isController(device))
            {
                return;
            }
            const std::string node{node_value};
            if (std::ranges::any_of(devices_,
                                    [&](const Device& existing) { return existing.node == node; }))
            {
                return;
            }

            descriptor = ::open(node.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (descriptor < 0)
            {
                inaccessible_devices_.fetch_add(1);
                return;
            }

            Device input_device;
            input_device.descriptor = descriptor;
            input_device.node = node;
            input_device.profile = profileFor(device, descriptor, node);
            input_device.event_code_to_usage = buttonMap(descriptor);
            if (input_device.event_code_to_usage.empty())
            {
                ::close(descriptor);
                return;
            }
            input_device.profile.buttons.reserve(input_device.event_code_to_usage.size());
            for (const auto& [event_code, usage] : input_device.event_code_to_usage)
            {
                static_cast<void>(event_code);
                input_device.profile.buttons.push_back(
                    HidButtonDescriptor{hid_button_usage_page, usage});
            }

            sink_.onInputDeviceConnected(input_device.profile);
            devices_.push_back(std::move(input_device));
            descriptor = -1;
            connected_devices_.store(devices_.size());
        }
        catch (...)
        {
            if (descriptor >= 0)
            {
                ::close(descriptor);
            }
            // A malformed device or allocation failure must not stop other controllers.
        }
    }

    void removeDevice(std::string_view node) noexcept
    {
        const auto iterator = std::ranges::find(devices_, node, &Device::node);
        if (iterator == devices_.end())
        {
            return;
        }
        const auto device_id = iterator->profile.device_id;
        ::close(iterator->descriptor);
        devices_.erase(iterator);
        connected_devices_.store(devices_.size());
        sink_.onInputDeviceRemoved(device_id);
    }

    [[nodiscard]] static auto profileFor(udev_device* device, int descriptor, std::string_view node)
        -> InputDeviceProfile
    {
        input_id identifiers{};
        static_cast<void>(ioctl(descriptor, EVIOCGID, &identifiers));
        std::array<char, 256> name{};
        const auto name_length = ioctl(descriptor, EVIOCGNAME(name.size()), name.data());

        InputDeviceProfile profile;
        profile.device_id = stableDeviceId(device, node);
        profile.display_name = name_length > 0
                                   ? std::string{name.data(), static_cast<std::size_t>(name_length)}
                                   : std::string{node};
        profile.device_kind = InputDeviceKind::game_controller;
        profile.vendor_id = identifiers.vendor;
        profile.product_id = identifiers.product;
        profile.usage_page = 0x0001;
        profile.usage = 0x0004;
        return profile;
    }

    [[nodiscard]] static auto buttonMap(int descriptor)
        -> std::vector<std::pair<std::uint16_t, std::uint16_t>>
    {
        std::array<unsigned long, key_word_count> key_bits{};
        if (ioctl(descriptor, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits.data()) < 0)
        {
            return {};
        }

        std::vector<std::pair<std::uint16_t, std::uint16_t>> result;
        std::uint16_t usage = 1;
        for (auto event_code = static_cast<unsigned int>(BTN_MISC);
             event_code <= static_cast<unsigned int>(KEY_MAX) &&
             usage < std::numeric_limits<std::uint16_t>::max();
             ++event_code)
        {
            if (bitIsSet(key_bits, event_code))
            {
                result.emplace_back(static_cast<std::uint16_t>(event_code), usage);
                ++usage;
            }
        }
        return result;
    }

    void readEvents(const Device& device) noexcept
    {
        std::array<input_event, 32> events{};
        while (true)
        {
            const auto byte_count = ::read(device.descriptor, events.data(), sizeof(events));
            if (byte_count <= 0)
            {
                return;
            }
            const auto event_count = static_cast<std::size_t>(byte_count) / sizeof(input_event);
            for (std::size_t index = 0; index < event_count; ++index)
            {
                const auto& event = events[index];
                if (event.type != EV_KEY || (event.value != 0 && event.value != 1))
                {
                    continue;
                }
                const auto mapped = std::ranges::find(
                    device.event_code_to_usage, static_cast<std::uint16_t>(event.code),
                    &std::pair<std::uint16_t, std::uint16_t>::first);
                if (mapped == device.event_code_to_usage.end())
                {
                    continue;
                }
                sink_.onInputEvent(
                    InputEvent{InputControl{InputDeviceKind::game_controller, hid_button_usage_page,
                                            mapped->second, false, device.profile.device_id},
                               event.value == 1, true});
                delivered_events_.fetch_add(1);
            }
        }
    }

    void releaseUdev() noexcept
    {
        if (monitor_ != nullptr)
        {
            udev_monitor_unref(monitor_);
            monitor_ = nullptr;
        }
        if (udev_ != nullptr)
        {
            udev_unref(udev_);
            udev_ = nullptr;
        }
    }

    IInputEventSink& sink_;
    mutable std::mutex lifecycle_mutex_;
    std::jthread worker_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> running_{false};
    udev* udev_{nullptr};
    udev_monitor* monitor_{nullptr};
    std::vector<Device> devices_;
    std::atomic<std::size_t> connected_devices_{0};
    std::atomic<std::size_t> inaccessible_devices_{0};
    std::atomic<std::uint64_t> delivered_events_{0};
    std::atomic<std::uint64_t> hot_plug_events_{0};
};

LinuxEvdevInputSource::LinuxEvdevInputSource(IInputEventSink& sink)
    : impl_(std::make_unique<Impl>(sink))
{
}

LinuxEvdevInputSource::~LinuxEvdevInputSource() = default;

auto LinuxEvdevInputSource::start() -> LinuxEvdevInputResult
{
    return impl_->start();
}

void LinuxEvdevInputSource::stop() noexcept
{
    impl_->stop();
}

auto LinuxEvdevInputSource::running() const noexcept -> bool
{
    return impl_->running();
}

auto LinuxEvdevInputSource::statistics() const noexcept -> LinuxEvdevInputStatistics
{
    return impl_->statistics();
}
} // namespace hvc::client
