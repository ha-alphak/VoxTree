#pragma once

#include <cstdint>
#include <hvc/client/control_plane_client.hpp>
#include <hvc/domain/model.hpp>
#include <utility>
#include <vector>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/base.h>

namespace hvc::windows_client
{
/**
 * Load one localized application resource.
 *
 * @param resource_id Identifier declared by the client resource table.
 * @returns Localized text.
 */
[[nodiscard]] auto text(std::uint32_t resource_id) -> winrt::hstring;

/**
 * Format a localized label and runtime value.
 *
 * @param label_id String-table identifier of the label.
 * @param value Runtime value.
 * @returns `label: value` in the active UI language.
 */
[[nodiscard]] auto labeledValue(std::uint32_t label_id, const winrt::hstring& value)
    -> winrt::hstring;

/**
 * Return the localized display name of a voice scope.
 *
 * @param scope Hierarchy scope.
 * @returns Localized scope name.
 */
[[nodiscard]] auto scopeText(domain::VoiceScope scope) -> winrt::hstring;

/**
 * Format public role identifiers from a membership.
 *
 * @param membership Authoritative public membership.
 * @returns Comma-separated roles or the localized unknown value.
 */
[[nodiscard]] auto rolesText(const client::MembershipView& membership) -> winrt::hstring;

/**
 * Create text controls whose scale can be updated as one view.
 *
 * The registry owns WinRT references to every tracked block. It is confined to
 * the UI thread that created the controls.
 */
class ViewTextRegistry final
{
  public:
    /**
     * Create and track a wrapping text block.
     *
     * @param value Initial text.
     * @param font_size Base font size before scaling.
     * @returns Configured text block.
     */
    [[nodiscard]] auto block(const winrt::hstring& value, double font_size = 14.0)
        -> winrt::Microsoft::UI::Xaml::Controls::TextBlock;
    /**
     * Create and track a semibold heading.
     *
     * @param resource_id Localized heading resource.
     * @param font_size Base font size before scaling.
     * @returns Configured heading.
     */
    [[nodiscard]] auto heading(std::uint32_t resource_id, double font_size = 22.0)
        -> winrt::Microsoft::UI::Xaml::Controls::TextBlock;
    /**
     * Create a titled vertical section.
     *
     * @param title_id Localized heading resource.
     * @param content Section content.
     * @returns Configured section panel.
     */
    [[nodiscard]] auto section(std::uint32_t title_id,
                               const winrt::Microsoft::UI::Xaml::UIElement& content)
        -> winrt::Microsoft::UI::Xaml::Controls::StackPanel;
    /**
     * Apply a text scale to every tracked block.
     *
     * @param scale_percent Inclusive percentage from 100 through 200.
     */
    void setScale(std::uint16_t scale_percent);

  private:
    std::uint16_t scale_percent_{100};
    std::vector<std::pair<winrt::Microsoft::UI::Xaml::Controls::TextBlock, double>> blocks_;
};

/**
 * Create a localized label and slider pair.
 *
 * @param texts Registry used for the label.
 * @param label_id Localized label resource.
 * @param minimum Minimum slider value.
 * @param maximum Maximum slider value.
 * @param value Initial slider value.
 * @param step Keyboard and pointer step.
 * @returns Panel and slider control.
 */
[[nodiscard]] auto labeledSlider(ViewTextRegistry& texts, std::uint32_t label_id, double minimum,
                                 double maximum, double value, double step)
    -> std::pair<winrt::Microsoft::UI::Xaml::Controls::StackPanel,
                 winrt::Microsoft::UI::Xaml::Controls::Slider>;
} // namespace hvc::windows_client
