#pragma once

#include <cstdint>
#include <hvc/client/ptt_input.hpp>
#include <memory>
#include <string>

namespace hvc::client
{
struct RawInputResult final
{
    [[nodiscard]] static auto success() -> RawInputResult;
    [[nodiscard]] static auto failure(std::uint32_t system_error, std::string message)
        -> RawInputResult;
    [[nodiscard]] explicit operator bool() const noexcept;

    bool successful{false};
    std::uint32_t system_error{0};
    std::string message;
};

class WinRawInputSource final
{
  public:
    explicit WinRawInputSource(IInputEventSink& sink);
    ~WinRawInputSource();

    WinRawInputSource(const WinRawInputSource&) = delete;
    auto operator=(const WinRawInputSource&) -> WinRawInputSource& = delete;
    WinRawInputSource(WinRawInputSource&&) = delete;
    auto operator=(WinRawInputSource&&) -> WinRawInputSource& = delete;

    [[nodiscard]] auto start() -> RawInputResult;
    void stop() noexcept;
    [[nodiscard]] auto running() const noexcept -> bool;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace hvc::client
