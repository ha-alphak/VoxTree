#include "view_helpers.hpp"

#include "../resources/resource.h"
#include "localized_text.hpp"

#include <winrt/Windows.UI.Text.h>

namespace hvc::windows_client
{
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

auto text(std::uint32_t resource_id) -> hstring
{
    return localizedText(resource_id);
}

auto labeledValue(std::uint32_t label_id, const hstring& value) -> hstring
{
    return text(label_id) + L": " + value;
}

auto scopeText(domain::VoiceScope scope) -> hstring
{
    switch (scope)
    {
    case domain::VoiceScope::team:
        return text(IDS_SCOPE_TEAM);
    case domain::VoiceScope::specialization:
        return text(IDS_SCOPE_SPECIALIZATION);
    case domain::VoiceScope::group:
        return text(IDS_SCOPE_GROUP);
    }
    return text(IDS_UNKNOWN);
}

auto rolesText(const client::MembershipView& membership) -> hstring
{
    hstring value;
    for (std::size_t index = 0; index < membership.role_ids.size(); ++index)
    {
        if (index != 0)
        {
            value = value + text(IDS_ROLE_SEPARATOR);
        }
        value = value + to_hstring(membership.role_ids[index].value());
    }
    return value.empty() ? text(IDS_UNKNOWN) : value;
}

auto ViewTextRegistry::block(const hstring& value, double font_size) -> TextBlock
{
    auto result = TextBlock{};
    result.Text(value);
    result.FontSize(font_size * (static_cast<double>(scale_percent_) / 100.0));
    result.TextWrapping(TextWrapping::Wrap);
    blocks_.emplace_back(result, font_size);
    return result;
}

auto ViewTextRegistry::heading(std::uint32_t resource_id, double font_size) -> TextBlock
{
    auto result = block(text(resource_id), font_size);
    result.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    return result;
}

auto ViewTextRegistry::section(std::uint32_t title_id, const UIElement& content) -> StackPanel
{
    auto panel = StackPanel{};
    panel.Spacing(8.0);
    panel.Children().Append(heading(title_id, 20.0));
    panel.Children().Append(content);
    return panel;
}

void ViewTextRegistry::setScale(std::uint16_t scale_percent)
{
    scale_percent_ = scale_percent;
    const auto factor = static_cast<double>(scale_percent_) / 100.0;
    for (const auto& [block, base_size] : blocks_)
    {
        block.FontSize(base_size * factor);
    }
}

auto labeledSlider(ViewTextRegistry& texts, std::uint32_t label_id, double minimum, double maximum,
                   double value, double step) -> std::pair<StackPanel, Slider>
{
    auto panel = StackPanel{};
    panel.Spacing(4.0);
    panel.Children().Append(texts.block(text(label_id)));
    auto slider = Slider{};
    slider.Minimum(minimum);
    slider.Maximum(maximum);
    slider.Value(value);
    slider.StepFrequency(step);
    slider.SmallChange(step);
    slider.LargeChange(step);
    panel.Children().Append(slider);
    return {panel, slider};
}
} // namespace hvc::windows_client
