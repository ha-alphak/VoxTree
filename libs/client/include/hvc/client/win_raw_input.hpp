#pragma once

#include <cstdint>
#include <hvc/client/ptt_input.hpp>
#include <memory>
#include <string>

namespace hvc::client
{
/// Hold the outcome of starting or stopping the Windows Raw Input source.
struct RawInputResult final
{
    /**
     * Create a successful Raw Input result.
     *
     * @returns A result that evaluates to `true`.
     */
    [[nodiscard]] static auto success() -> RawInputResult;
    /**
     * Create a failed Raw Input result.
     *
     * @param system_error Win32 error code, or zero for a logical failure.
     * @param message Human-readable diagnostic message.
     * @returns A result that evaluates to `false`.
     */
    [[nodiscard]] static auto failure(std::uint32_t system_error, std::string message)
        -> RawInputResult;
    /**
     * Return whether the operation succeeded.
     *
     * @returns Value of `successful`.
     */
    [[nodiscard]] explicit operator bool() const noexcept;

    /// Whether the operation succeeded.
    bool successful{false};
    /// Win32 error code, or zero when not applicable.
    std::uint32_t system_error{0};
    /// Human-readable diagnostic message.
    std::string message;
};

/// Report cumulative event-processing counters for a Raw Input source.
struct RawInputStatistics final
{
    /// Number of `WM_INPUT` messages processed.
    std::uint64_t input_messages{0};
    /// Number of normalized input events delivered to the sink.
    std::uint64_t delivered_events{0};
};

/**
 * Capture keyboard, mouse, and generic HID buttons through Windows Raw Input.
 *
 * The source owns a background message thread and registers for input even when
 * the application lacks focus. The referenced sink must outlive the source.
 */
class WinRawInputSource final
{
  public:
    /**
     * Construct a stopped input source.
     *
     * @param sink Event sink that must outlive this source.
     */
    explicit WinRawInputSource(IInputEventSink& sink);
    /// Stop capture and release all Windows resources.
    ~WinRawInputSource();

    /// Copy construction is disabled.
    WinRawInputSource(const WinRawInputSource&) = delete;
    /// Copy assignment is disabled.
    auto operator=(const WinRawInputSource&) -> WinRawInputSource& = delete;
    /// Move construction is disabled.
    WinRawInputSource(WinRawInputSource&&) = delete;
    /// Move assignment is disabled.
    auto operator=(WinRawInputSource&&) -> WinRawInputSource& = delete;

    /**
     * Start the Raw Input message thread and device registration.
     *
     * @returns Success, or a Win32 diagnostic when startup fails.
     */
    [[nodiscard]] auto start() -> RawInputResult;
    /// Stop capture and wait for the message thread to exit.
    void stop() noexcept;
    /**
     * Return whether the input message thread is running.
     *
     * @returns `true` while capture is active.
     */
    [[nodiscard]] auto running() const noexcept -> bool;
    /**
     * Return cumulative processing counters.
     *
     * @returns Thread-safe statistics snapshot.
     */
    [[nodiscard]] auto statistics() const noexcept -> RawInputStatistics;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace hvc::client
