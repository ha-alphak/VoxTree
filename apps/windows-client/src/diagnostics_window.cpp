#include "diagnostics_window.hpp"

#include "../resources/resource.h"

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/base.h>

namespace hvc::windows_client
{
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace
{
[[nodiscard]] auto connectionText(client::VoiceTransportState state) -> hstring
{
    switch (state)
    {
    case client::VoiceTransportState::disconnected:
        return text(IDS_CONNECTION_DISCONNECTED);
    case client::VoiceTransportState::connecting:
        return text(IDS_CONNECTION_CONNECTING);
    case client::VoiceTransportState::connected:
        return text(IDS_CONNECTION_CONNECTED);
    case client::VoiceTransportState::reconnecting:
        return text(IDS_CONNECTION_RECONNECTING);
    }
    return text(IDS_UNKNOWN);
}
} // namespace

void DiagnosticsWindow::show(const presentation::DiagnosticsState& diagnostics,
                             std::uint16_t text_scale_percent)
{
    diagnostics_ = diagnostics;
    if (open_)
    {
        setTextScale(text_scale_percent);
        update(diagnostics);
        window_.Activate();
        return;
    }

    window_ = Window{};
    window_.Title(text(IDS_DIAGNOSTICS_TITLE));
    texts_.setScale(text_scale_percent);
    buildContent();
    open_ = true;
    const auto weak = weak_from_this();
    window_.Closed([weak](auto const&, WindowEventArgs const&) {
        if (const auto self = weak.lock())
        {
            self->open_ = false;
            self->window_ = nullptr;
        }
    });
    update(diagnostics_);
    window_.Activate();
}

void DiagnosticsWindow::update(const presentation::DiagnosticsState& diagnostics)
{
    diagnostics_ = diagnostics;
    if (!open_)
    {
        return;
    }
    connection_text_.Text(labeledValue(IDS_CONNECTION, connectionText(diagnostics_.voice_state)));
    error_code_text_.Text(labeledValue(IDS_ERROR, diagnostics_.last_error_code.empty()
                                                      ? text(IDS_NO_ERROR_CODE)
                                                      : to_hstring(diagnostics_.last_error_code)));
    error_count_text_.Text(labeledValue(IDS_ERROR_COUNT, to_hstring(diagnostics_.error_count)));
    diagnostic_text_.Text(diagnostics_.last_diagnostic.empty()
                              ? text(IDS_DIAGNOSTICS_EMPTY)
                              : to_hstring(diagnostics_.last_diagnostic));
}

void DiagnosticsWindow::setTextScale(std::uint16_t text_scale_percent)
{
    texts_.setScale(text_scale_percent);
}

void DiagnosticsWindow::close() noexcept
{
    if (open_ && window_ != nullptr)
    {
        window_.Close();
    }
}

auto DiagnosticsWindow::isOpen() const noexcept -> bool
{
    return open_;
}

void DiagnosticsWindow::buildContent()
{
    auto root = StackPanel{};
    root.Padding(Thickness{24.0});
    root.Spacing(12.0);
    root.Children().Append(texts_.heading(IDS_DIAGNOSTICS_TITLE, 28.0));
    connection_text_ = texts_.block({});
    error_code_text_ = texts_.block({});
    error_count_text_ = texts_.block({});
    diagnostic_text_ = texts_.block({});
    diagnostic_text_.TextWrapping(TextWrapping::WrapWholeWords);
    root.Children().Append(connection_text_);
    root.Children().Append(error_code_text_);
    root.Children().Append(error_count_text_);
    root.Children().Append(texts_.section(IDS_SHOW_DIAGNOSTICS, diagnostic_text_));

    auto close_button = Button{};
    close_button.Content(box_value(text(IDS_CLOSE)));
    close_button.HorizontalAlignment(HorizontalAlignment::Left);
    const auto weak = weak_from_this();
    close_button.Click([weak](auto const&, RoutedEventArgs const&) {
        if (const auto self = weak.lock())
        {
            self->close();
        }
    });
    root.Children().Append(close_button);
    window_.Content(root);
}
} // namespace hvc::windows_client
