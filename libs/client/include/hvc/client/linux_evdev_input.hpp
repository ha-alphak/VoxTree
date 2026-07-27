#pragma once

#include <cstddef>
#include <cstdint>
#include <hvc/client/ptt_input.hpp>
#include <memory>
#include <string>

namespace hvc::client
{
/// Classify failures while starting the unprivileged Linux controller source.
enum class LinuxEvdevInputError : std::uint8_t
{
    /// No failure occurred.
    none,
    /// The udev input-device monitor could not be created.
    device_monitor_unavailable,
    /// The background event thread could not be started.
    thread_start_failed
};

/// Describe the outcome of starting the Linux controller source.
struct LinuxEvdevInputResult final
{
    /**
     * Return whether startup succeeded.
     *
     * @returns `true` when `error` is `none`.
     */
    [[nodiscard]] explicit operator bool() const noexcept;

    /// Stable error classification.
    LinuxEvdevInputError error{LinuxEvdevInputError::none};
    /// Human-readable diagnostic without device input contents.
    std::string message;
};

/// Hold diagnostic counters for the Linux controller source.
struct LinuxEvdevInputStatistics final
{
    /// Controller devices that were opened successfully.
    std::size_t connected_devices{0};
    /// Controller devices detected but inaccessible to the current user.
    std::size_t inaccessible_devices{0};
    /// Normalized button state changes delivered to the sink.
    std::uint64_t delivered_events{0};
    /// Hot-plug add and remove notifications observed through udev.
    std::uint64_t hot_plug_events{0};
};

/**
 * Read game-controller, joystick, and HOTAS buttons through Linux evdev.
 *
 * The source uses the active desktop user's ACLs and never opens keyboard or
 * mouse devices. One worker owns all file descriptors and reports normalized
 * game-controller events to the referenced sink. The sink must outlive the
 * source or be retained until `stop()` returns.
 */
class LinuxEvdevInputSource final
{
  public:
    /**
     * Create an idle input source.
     *
     * @param sink Event sink that must outlive the running source.
     */
    explicit LinuxEvdevInputSource(IInputEventSink& sink);
    /// Stop the worker and release all device descriptors.
    ~LinuxEvdevInputSource();

    LinuxEvdevInputSource(const LinuxEvdevInputSource&) = delete;
    auto operator=(const LinuxEvdevInputSource&) -> LinuxEvdevInputSource& = delete;
    LinuxEvdevInputSource(LinuxEvdevInputSource&&) = delete;
    auto operator=(LinuxEvdevInputSource&&) -> LinuxEvdevInputSource& = delete;

    /**
     * Start discovery, hot-plug monitoring, and button delivery.
     *
     * Repeated calls while running succeed without creating another worker.
     *
     * @returns Startup outcome.
     */
    [[nodiscard]] auto start() -> LinuxEvdevInputResult;
    /// Stop the worker; repeated calls are safe.
    void stop() noexcept;
    /**
     * Return whether the worker is active.
     *
     * @returns `true` after successful startup and before `stop()`.
     */
    [[nodiscard]] auto running() const noexcept -> bool;
    /**
     * Return current discovery and event counters.
     *
     * @returns Thread-safe statistics snapshot.
     */
    [[nodiscard]] auto statistics() const noexcept -> LinuxEvdevInputStatistics;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace hvc::client
