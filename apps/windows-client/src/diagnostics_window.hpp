#pragma once

#include "view_helpers.hpp"

#include <cstdint>
#include <hvc/presentation/desktop_model.hpp>
#include <memory>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace hvc::windows_client
{
/**
 * Present session diagnostics in an independent non-modal window.
 *
 * The window owns no session or transport object and receives only the
 * privacy-bounded presentation snapshot.
 */
class DiagnosticsWindow final : public std::enable_shared_from_this<DiagnosticsWindow>
{
  public:
    /// Construct a closed diagnostics window.
    DiagnosticsWindow() = default;

    /**
     * Create or activate the window with the latest diagnostic state.
     *
     * @param diagnostics Privacy-bounded diagnostics snapshot.
     * @param text_scale_percent Current application text scale.
     */
    void show(const presentation::DiagnosticsState& diagnostics, std::uint16_t text_scale_percent);
    /**
     * Refresh visible diagnostics.
     *
     * @param diagnostics Latest privacy-bounded snapshot.
     */
    void update(const presentation::DiagnosticsState& diagnostics);
    /**
     * Apply application text scaling.
     *
     * @param text_scale_percent Inclusive percentage from 100 through 200.
     */
    void setTextScale(std::uint16_t text_scale_percent);
    /// Close the window if it is open.
    void close() noexcept;
    /**
     * Return whether a native window is currently open.
     *
     * @returns `true` after activation and before the close event.
     */
    [[nodiscard]] auto isOpen() const noexcept -> bool;

  private:
    void buildContent();

    winrt::Microsoft::UI::Xaml::Window window_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock connection_text_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock error_code_text_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock error_count_text_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock diagnostic_text_{nullptr};
    ViewTextRegistry texts_;
    presentation::DiagnosticsState diagnostics_;
    bool open_{false};
};
} // namespace hvc::windows_client
