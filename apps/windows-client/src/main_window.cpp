#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "main_window.hpp"

#include "../resources/resource.h"
#include "login_dialog.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cwctype>
#include <map>
#include <microsoft.ui.xaml.window.h>
#include <ranges>
#include <string_view>
#include <utility>
#include <vector>
#include <windows.h>
#undef GetCurrentTime
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.h>
#include <winrt/base.h>

namespace hvc::windows_client
{
using namespace winrt;
using namespace Microsoft::UI;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;

namespace
{
constexpr double sidebar_width = 292.0;

[[nodiscard]] auto colorBrush(std::uint8_t red, std::uint8_t green, std::uint8_t blue,
                              std::uint8_t alpha = 255) -> SolidColorBrush
{
    return SolidColorBrush{Windows::UI::Color{alpha, red, green, blue}};
}

[[nodiscard]] auto backgroundBrush() -> SolidColorBrush
{
    return colorBrush(8, 17, 31);
}

[[nodiscard]] auto surfaceBrush() -> SolidColorBrush
{
    return colorBrush(14, 26, 43);
}

[[nodiscard]] auto softSurfaceBrush() -> SolidColorBrush
{
    return colorBrush(22, 38, 59);
}

[[nodiscard]] auto borderBrush() -> SolidColorBrush
{
    return colorBrush(42, 58, 80);
}

[[nodiscard]] auto accentBrush() -> SolidColorBrush
{
    return colorBrush(47, 128, 237);
}

[[nodiscard]] auto accentSoftBrush() -> SolidColorBrush
{
    return colorBrush(28, 62, 103);
}

[[nodiscard]] auto foregroundBrush() -> SolidColorBrush
{
    return colorBrush(234, 241, 251);
}

[[nodiscard]] auto mutedBrush() -> SolidColorBrush
{
    return colorBrush(156, 172, 194);
}

[[nodiscard]] auto successBrush() -> SolidColorBrush
{
    return colorBrush(62, 207, 142);
}

[[nodiscard]] auto successSoftBrush() -> SolidColorBrush
{
    return colorBrush(18, 62, 62);
}

[[nodiscard]] auto warningSoftBrush() -> SolidColorBrush
{
    return colorBrush(68, 54, 25);
}

[[nodiscard]] auto dangerSoftBrush() -> SolidColorBrush
{
    return colorBrush(70, 33, 45);
}

[[nodiscard]] auto symbolIcon(Symbol symbol) -> SymbolIcon
{
    auto icon = SymbolIcon{};
    icon.Symbol(symbol);
    icon.Foreground(foregroundBrush());
    return icon;
}

[[nodiscard]] auto iconButton(Symbol symbol, const hstring& accessible_name) -> Button
{
    auto button = Button{};
    button.Content(symbolIcon(symbol));
    button.Width(40.0);
    button.Height(40.0);
    button.Padding(Thickness{0.0});
    button.HorizontalContentAlignment(HorizontalAlignment::Center);
    button.VerticalContentAlignment(VerticalAlignment::Center);
    button.Background(colorBrush(0, 0, 0, 0));
    button.BorderBrush(colorBrush(0, 0, 0, 0));
    button.CornerRadius(CornerRadius{8.0});
    Automation::AutomationProperties::SetName(button, accessible_name);
    ToolTipService::SetToolTip(button, box_value(accessible_name));
    return button;
}

[[nodiscard]] auto separator() -> Border
{
    auto line = Border{};
    line.Height(1.0);
    line.Background(borderBrush());
    return line;
}

[[nodiscard]] auto initials(const hstring& value) -> hstring
{
    std::wstring result;
    for (const auto character : std::wstring_view{value})
    {
        if (std::iswalnum(character) != 0)
        {
            result.push_back(static_cast<wchar_t>(std::towupper(character)));
            if (result.size() == 2)
            {
                break;
            }
        }
    }
    return result.empty() ? L"?" : hstring{result};
}

[[nodiscard]] auto connectionText(presentation::ConnectionPhase phase) -> hstring
{
    switch (phase)
    {
    case presentation::ConnectionPhase::signed_out:
        return text(IDS_CONNECTION_DISCONNECTED);
    case presentation::ConnectionPhase::connecting:
        return text(IDS_CONNECTION_CONNECTING);
    case presentation::ConnectionPhase::ready:
        return text(IDS_CONNECTION_CONNECTED);
    case presentation::ConnectionPhase::reconnecting:
        return text(IDS_CONNECTION_RECONNECTING);
    case presentation::ConnectionPhase::disconnecting:
        return text(IDS_CONNECTION_DISCONNECTED);
    }
    return text(IDS_UNKNOWN);
}

[[nodiscard]] auto scopeDescription(domain::VoiceScope scope) -> hstring
{
    switch (scope)
    {
    case domain::VoiceScope::team:
        return text(IDS_TEAM_SCOPE_DESCRIPTION);
    case domain::VoiceScope::specialization:
        return text(IDS_SPECIALIZATION_SCOPE_DESCRIPTION);
    case domain::VoiceScope::group:
        return text(IDS_GROUP_SCOPE_DESCRIPTION);
    }
    return {};
}

[[nodiscard]] auto scopeSymbol(domain::VoiceScope scope) -> Symbol
{
    switch (scope)
    {
    case domain::VoiceScope::team:
        return Symbol::People;
    case domain::VoiceScope::specialization:
        return Symbol::Contact;
    case domain::VoiceScope::group:
        return Symbol::World;
    }
    return Symbol::People;
}

[[nodiscard]] auto directoryPhaseText(presentation::DirectoryPhase phase) -> hstring
{
    switch (phase)
    {
    case presentation::DirectoryPhase::unavailable:
        return text(IDS_DIRECTORY_UNAVAILABLE);
    case presentation::DirectoryPhase::loading:
        return text(IDS_DIRECTORY_LOADING);
    case presentation::DirectoryPhase::ready:
        return {};
    case presentation::DirectoryPhase::stale:
        return text(IDS_DIRECTORY_STALE);
    case presentation::DirectoryPhase::unauthorized:
        return text(IDS_DIRECTORY_UNAUTHORIZED);
    }
    return text(IDS_DIRECTORY_UNAVAILABLE);
}

[[nodiscard]] auto presenceText(presentation::PresenceState presence) -> hstring
{
    switch (presence)
    {
    case presentation::PresenceState::unknown:
        return text(IDS_PRESENCE_UNKNOWN);
    case presentation::PresenceState::offline:
        return text(IDS_PRESENCE_OFFLINE);
    case presentation::PresenceState::online:
        return text(IDS_PRESENCE_ONLINE);
    }
    return text(IDS_PRESENCE_UNKNOWN);
}

[[nodiscard]] auto publicRolesText(const presentation::ParticipantState& participant,
                                   const std::optional<client::DirectoryView>& directory) -> hstring
{
    hstring result;
    for (const auto& role_id : participant.role_ids)
    {
        auto label = role_id;
        if (directory.has_value())
        {
            const auto role =
                std::ranges::find_if(directory->public_roles, [&role_id](const auto& candidate) {
                    return candidate.role_id == role_id;
                });
            if (role != directory->public_roles.end())
            {
                label = role->display_name;
            }
        }
        if (!result.empty())
        {
            result = result + text(IDS_ROLE_SEPARATOR);
        }
        result = result + to_hstring(label);
    }
    return result.empty() ? text(IDS_PUBLIC_ROLES_NONE) : result;
}

[[nodiscard]] auto bindingLabel(const std::vector<client::InputBinding>& bindings,
                                client::PushToTalkAction action) -> hstring
{
    for (const auto& binding : bindings)
    {
        if (binding.action != action || binding.chord.size() != 1)
        {
            continue;
        }
        const auto& control = binding.chord.front();
        if (control.device_kind == client::InputDeviceKind::keyboard && control.code >= VK_F1 &&
            control.code <= VK_F12)
        {
            return L"F" + to_hstring((control.code - VK_F1) + 1);
        }
    }
    return text(IDS_UNKNOWN);
}
} // namespace

MainWindow::~MainWindow()
{
    closing_ = true;
    ++session_generation_;
    if (participant_update_timer_ != nullptr)
    {
        participant_update_timer_.Stop();
    }
    if (settings_window_ != nullptr)
    {
        settings_window_->close();
    }
    if (diagnostics_window_ != nullptr)
    {
        diagnostics_window_->close();
    }
    if (session_ != nullptr)
    {
        session_->disconnect();
    }
}

void MainWindow::show()
{
    window_ = Window{};
    window_.Title(text(IDS_APP_TITLE));
    dispatcher_ = window_.DispatcherQueue();
    participant_update_timer_ = dispatcher_.CreateTimer();
    participant_update_timer_.Interval(std::chrono::milliseconds{150});
    participant_update_timer_.IsRepeating(false);
    const auto weak = weak_from_this();
    participant_update_timer_.Tick([weak](Dispatching::DispatcherQueueTimer const&, auto const&) {
        if (const auto self = weak.lock())
        {
            self->applyParticipantUpdatesAsync();
        }
    });

    root_ = Grid{};
    root_.RequestedTheme(ElementTheme::Dark);
    root_.Background(backgroundBrush());
    buildLoginView();
    buildReadyView();
    root_.Children().Append(login_view_);
    root_.Children().Append(ready_root_);
    window_.Content(root_);
    window_.Closed([weak](auto const&, WindowEventArgs const&) {
        if (const auto self = weak.lock())
        {
            self->closing_ = true;
            ++self->session_generation_;
            if (self->participant_update_timer_ != nullptr)
            {
                self->participant_update_timer_.Stop();
            }
            if (self->settings_window_ != nullptr)
            {
                self->settings_window_->close();
            }
            if (self->diagnostics_window_ != nullptr)
            {
                self->diagnostics_window_->close();
            }
            auto session = std::move(self->session_);
            if (session != nullptr)
            {
                self->shutdownSessionAsync(std::move(session));
            }
        }
    });
    render();
    window_.Activate();
    SetWindowPos(windowHandle(), nullptr, 0, 0, 1280, 820,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void MainWindow::buildLoginView()
{
    login_view_ = Border{};
    login_view_.HorizontalAlignment(HorizontalAlignment::Center);
    login_view_.VerticalAlignment(VerticalAlignment::Center);
    login_view_.MaxWidth(520.0);
    login_view_.Padding(Thickness{36.0});
    login_view_.Background(surfaceBrush());
    login_view_.BorderBrush(borderBrush());
    login_view_.BorderThickness(Thickness{1.0});
    login_view_.CornerRadius(CornerRadius{18.0});

    login_panel_ = StackPanel{};
    login_panel_.Spacing(18.0);

    auto brand = StackPanel{};
    brand.Orientation(Orientation::Horizontal);
    brand.Spacing(12.0);
    auto mark = Border{};
    mark.Width(46.0);
    mark.Height(46.0);
    mark.Background(accentBrush());
    mark.CornerRadius(CornerRadius{13.0});
    mark.Child(symbolIcon(Symbol::Remote));
    brand.Children().Append(mark);
    auto brand_text = StackPanel{};
    brand_text.Spacing(1.0);
    auto short_title = texts_.block(L"HVC", 22.0);
    short_title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    brand_text.Children().Append(short_title);
    auto product_title = texts_.block(text(IDS_APP_TITLE), 13.0);
    product_title.Foreground(mutedBrush());
    brand_text.Children().Append(product_title);
    brand.Children().Append(brand_text);
    login_panel_.Children().Append(brand);

    login_panel_.Children().Append(texts_.heading(IDS_LOGIN_DETAILS, 28.0));
    auto introduction = texts_.block(text(IDS_LOGIN_INTRODUCTION));
    introduction.Foreground(mutedBrush());
    login_panel_.Children().Append(introduction);
    connect_button_ = Button{};
    auto connect_content = StackPanel{};
    connect_content.Orientation(Orientation::Horizontal);
    connect_content.Spacing(8.0);
    connect_content.Children().Append(symbolIcon(Symbol::Remote));
    connect_content.Children().Append(texts_.block(text(IDS_CONNECT)));
    connect_button_.Content(connect_content);
    connect_button_.HorizontalAlignment(HorizontalAlignment::Stretch);
    connect_button_.HorizontalContentAlignment(HorizontalAlignment::Center);
    connect_button_.MinHeight(44.0);
    connect_button_.Background(accentBrush());
    connect_button_.Foreground(foregroundBrush());
    connect_button_.BorderThickness(Thickness{0.0});
    connect_button_.CornerRadius(CornerRadius{10.0});
    const auto weak = weak_from_this();
    connect_button_.Click([weak](auto const&, RoutedEventArgs const&) {
        if (const auto self = weak.lock())
        {
            self->requestConnect();
        }
    });
    login_panel_.Children().Append(connect_button_);
    login_error_text_ = texts_.block({});
    login_error_text_.Foreground(colorBrush(255, 170, 180));
    login_error_text_.Visibility(Visibility::Collapsed);
    login_panel_.Children().Append(login_error_text_);
    login_view_.Child(login_panel_);
}

void MainWindow::buildReadyView()
{
    ready_root_ = Grid{};
    ready_root_.Visibility(Visibility::Collapsed);

    auto title_row = RowDefinition{};
    title_row.Height(GridLength{64.0, GridUnitType::Pixel});
    auto content_row = RowDefinition{};
    content_row.Height(GridLength{1.0, GridUnitType::Star});
    auto dock_row = RowDefinition{};
    dock_row.Height(GridLength{96.0, GridUnitType::Pixel});
    ready_root_.RowDefinitions().Append(title_row);
    ready_root_.RowDefinitions().Append(content_row);
    ready_root_.RowDefinitions().Append(dock_row);

    buildTitleBar();

    ready_body_ = Grid{};
    auto sidebar_column = ColumnDefinition{};
    sidebar_column.Width(GridLength{sidebar_width, GridUnitType::Pixel});
    auto content_column = ColumnDefinition{};
    content_column.Width(GridLength{1.0, GridUnitType::Star});
    ready_body_.ColumnDefinitions().Append(sidebar_column);
    ready_body_.ColumnDefinitions().Append(content_column);
    buildSidebar();
    buildChannelView();
    Grid::SetRow(ready_body_, 1);
    ready_root_.Children().Append(ready_body_);

    buildVoiceDock();
}

void MainWindow::buildTitleBar()
{
    auto titlebar = Grid{};
    titlebar.Padding(Thickness{18.0, 10.0, 14.0, 10.0});
    titlebar.Background(surfaceBrush());
    auto left_column = ColumnDefinition{};
    left_column.Width(GridLength{1.0, GridUnitType::Star});
    auto right_column = ColumnDefinition{};
    right_column.Width(GridLength{0.0, GridUnitType::Auto});
    titlebar.ColumnDefinitions().Append(left_column);
    titlebar.ColumnDefinitions().Append(right_column);

    auto brand = StackPanel{};
    brand.Orientation(Orientation::Horizontal);
    brand.Spacing(10.0);
    brand.VerticalAlignment(VerticalAlignment::Center);
    auto mark = Border{};
    mark.Width(36.0);
    mark.Height(36.0);
    mark.CornerRadius(CornerRadius{10.0});
    mark.Background(accentBrush());
    mark.Child(symbolIcon(Symbol::Remote));
    brand.Children().Append(mark);
    auto brand_text = StackPanel{};
    brand_text.Spacing(0.0);
    auto product = texts_.block(L"HVC", 18.0);
    product.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    brand_text.Children().Append(product);
    auto subtitle = texts_.block(text(IDS_APP_TITLE), 11.0);
    subtitle.Foreground(mutedBrush());
    brand_text.Children().Append(subtitle);
    brand.Children().Append(brand_text);
    titlebar.Children().Append(brand);

    auto actions = StackPanel{};
    actions.Orientation(Orientation::Horizontal);
    actions.Spacing(6.0);
    actions.VerticalAlignment(VerticalAlignment::Center);
    Grid::SetColumn(actions, 1);

    connection_badge_ = Border{};
    connection_badge_.Padding(Thickness{10.0, 6.0, 10.0, 6.0});
    connection_badge_.CornerRadius(CornerRadius{999.0});
    auto badge_content = StackPanel{};
    badge_content.Orientation(Orientation::Horizontal);
    badge_content.Spacing(6.0);
    auto status_icon = symbolIcon(Symbol::Accept);
    status_icon.Width(14.0);
    status_icon.Height(14.0);
    badge_content.Children().Append(status_icon);
    connection_badge_text_ = texts_.block({}, 12.0);
    badge_content.Children().Append(connection_badge_text_);
    connection_badge_.Child(badge_content);
    actions.Children().Append(connection_badge_);

    auto settings = iconButton(Symbol::Setting, text(IDS_OPEN_SETTINGS));
    const auto weak = weak_from_this();
    settings.Click([weak](auto const&, RoutedEventArgs const&) {
        if (const auto self = weak.lock())
        {
            self->openSettings();
        }
    });
    actions.Children().Append(settings);
    titlebar.Children().Append(actions);

    Grid::SetRow(titlebar, 0);
    ready_root_.Children().Append(titlebar);
}

void MainWindow::buildSidebar()
{
    auto sidebar = Grid{};
    sidebar.Padding(Thickness{14.0});
    sidebar.Background(surfaceBrush());
    auto identity_row = RowDefinition{};
    identity_row.Height(GridLength{0.0, GridUnitType::Auto});
    auto hierarchy_row = RowDefinition{};
    hierarchy_row.Height(GridLength{1.0, GridUnitType::Star});
    auto footer_row = RowDefinition{};
    footer_row.Height(GridLength{0.0, GridUnitType::Auto});
    sidebar.RowDefinitions().Append(identity_row);
    sidebar.RowDefinitions().Append(hierarchy_row);
    sidebar.RowDefinitions().Append(footer_row);

    auto identity = Grid{};
    identity.Margin(Thickness{4.0, 2.0, 4.0, 14.0});
    auto avatar_column = ColumnDefinition{};
    avatar_column.Width(GridLength{44.0, GridUnitType::Pixel});
    auto identity_column = ColumnDefinition{};
    identity_column.Width(GridLength{1.0, GridUnitType::Star});
    identity.ColumnDefinitions().Append(avatar_column);
    identity.ColumnDefinitions().Append(identity_column);
    auto avatar = Border{};
    avatar.Width(38.0);
    avatar.Height(38.0);
    avatar.Background(accentSoftBrush());
    avatar.CornerRadius(CornerRadius{19.0});
    auto avatar_icon = symbolIcon(Symbol::Contact);
    avatar.Child(avatar_icon);
    identity.Children().Append(avatar);
    auto identity_text = StackPanel{};
    identity_text.Spacing(1.0);
    identity_text.VerticalAlignment(VerticalAlignment::Center);
    identity_name_text_ = texts_.block({}, 15.0);
    identity_name_text_.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    identity_detail_text_ = texts_.block({}, 11.0);
    identity_detail_text_.Foreground(mutedBrush());
    identity_text.Children().Append(identity_name_text_);
    identity_text.Children().Append(identity_detail_text_);
    Grid::SetColumn(identity_text, 1);
    identity.Children().Append(identity_text);
    sidebar.Children().Append(identity);

    const auto weak = weak_from_this();
    auto hierarchy_container = StackPanel{};
    hierarchy_container.Spacing(6.0);
    auto hierarchy_label = texts_.block(text(IDS_MY_AREAS), 11.0);
    hierarchy_label.Foreground(mutedBrush());
    hierarchy_container.Children().Append(hierarchy_label);
    hierarchy_panel_ = StackPanel{};
    hierarchy_panel_.Spacing(4.0);
    hierarchy_status_text_ = texts_.block(text(IDS_DIRECTORY_LOADING), 12.0);
    hierarchy_status_text_.Foreground(mutedBrush());
    hierarchy_status_text_.TextWrapping(TextWrapping::Wrap);
    hierarchy_status_text_.Margin(Thickness{8.0, 10.0, 8.0, 0.0});
    hierarchy_panel_.Children().Append(hierarchy_status_text_);
    hierarchy_container.Children().Append(hierarchy_panel_);
    auto hierarchy_scroll = ScrollViewer{};
    hierarchy_scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    hierarchy_scroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
    hierarchy_scroll.Content(hierarchy_container);
    Grid::SetRow(hierarchy_scroll, 1);
    sidebar.Children().Append(hierarchy_scroll);

    auto diagnostics = Button{};
    diagnostics.HorizontalAlignment(HorizontalAlignment::Stretch);
    diagnostics.HorizontalContentAlignment(HorizontalAlignment::Left);
    diagnostics.Background(colorBrush(0, 0, 0, 0));
    diagnostics.BorderBrush(colorBrush(0, 0, 0, 0));
    diagnostics.CornerRadius(CornerRadius{8.0});
    auto diagnostics_content = StackPanel{};
    diagnostics_content.Orientation(Orientation::Horizontal);
    diagnostics_content.Spacing(9.0);
    diagnostics_content.Children().Append(symbolIcon(Symbol::Repair));
    diagnostics_content.Children().Append(texts_.block(text(IDS_DIAGNOSTICS_TITLE)));
    diagnostics.Content(diagnostics_content);
    diagnostics.Click([weak](auto const&, RoutedEventArgs const&) {
        if (const auto self = weak.lock())
        {
            self->openDiagnostics();
        }
    });
    Grid::SetRow(diagnostics, 2);
    sidebar.Children().Append(diagnostics);
    ready_body_.Children().Append(sidebar);
}

void MainWindow::buildChannelView()
{
    auto content = Grid{};
    content.Padding(Thickness{26.0, 22.0, 26.0, 16.0});
    auto heading_row = RowDefinition{};
    heading_row.Height(GridLength{0.0, GridUnitType::Auto});
    auto banner_row = RowDefinition{};
    banner_row.Height(GridLength{0.0, GridUnitType::Auto});
    auto list_row = RowDefinition{};
    list_row.Height(GridLength{1.0, GridUnitType::Star});
    auto moderation_row = RowDefinition{};
    moderation_row.Height(GridLength{0.0, GridUnitType::Auto});
    content.RowDefinitions().Append(heading_row);
    content.RowDefinitions().Append(banner_row);
    content.RowDefinitions().Append(list_row);
    content.RowDefinitions().Append(moderation_row);

    auto heading = Grid{};
    heading.Margin(Thickness{0.0, 0.0, 0.0, 18.0});
    auto icon_column = ColumnDefinition{};
    icon_column.Width(GridLength{42.0, GridUnitType::Pixel});
    auto title_column = ColumnDefinition{};
    title_column.Width(GridLength{1.0, GridUnitType::Star});
    auto count_column = ColumnDefinition{};
    count_column.Width(GridLength{0.0, GridUnitType::Auto});
    heading.ColumnDefinitions().Append(icon_column);
    heading.ColumnDefinitions().Append(title_column);
    heading.ColumnDefinitions().Append(count_column);

    auto icon_surface = Border{};
    icon_surface.Width(34.0);
    icon_surface.Height(34.0);
    icon_surface.Background(accentSoftBrush());
    icon_surface.CornerRadius(CornerRadius{10.0});
    channel_icon_ = SymbolIcon{};
    channel_icon_.Foreground(foregroundBrush());
    icon_surface.Child(channel_icon_);
    heading.Children().Append(icon_surface);

    auto heading_text = StackPanel{};
    heading_text.Spacing(2.0);
    channel_breadcrumb_text_ = texts_.block({}, 11.0);
    channel_breadcrumb_text_.Foreground(mutedBrush());
    heading_text.Children().Append(channel_breadcrumb_text_);
    channel_title_text_ = texts_.block({}, 25.0);
    channel_title_text_.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    heading_text.Children().Append(channel_title_text_);
    channel_description_text_ = texts_.block({}, 13.0);
    channel_description_text_.Foreground(mutedBrush());
    heading_text.Children().Append(channel_description_text_);
    channel_access_text_ = texts_.block({}, 11.0);
    channel_access_text_.Foreground(mutedBrush());
    heading_text.Children().Append(channel_access_text_);
    Grid::SetColumn(heading_text, 1);
    heading.Children().Append(heading_text);

    participant_count_text_ = texts_.block({}, 12.0);
    participant_count_text_.Foreground(mutedBrush());
    participant_count_text_.VerticalAlignment(VerticalAlignment::Bottom);
    participant_count_text_.Margin(Thickness{12.0, 0.0, 0.0, 4.0});
    Grid::SetColumn(participant_count_text_, 2);
    heading.Children().Append(participant_count_text_);
    content.Children().Append(heading);

    auto banner_panel = StackPanel{};
    banner_panel.Spacing(8.0);
    directory_status_banner_ = Border{};
    directory_status_banner_.Padding(Thickness{12.0, 9.0, 12.0, 9.0});
    directory_status_banner_.Background(warningSoftBrush());
    directory_status_banner_.CornerRadius(CornerRadius{8.0});
    directory_status_text_ = texts_.block({}, 12.0);
    directory_status_text_.TextWrapping(TextWrapping::Wrap);
    directory_status_banner_.Child(directory_status_text_);
    directory_status_banner_.Visibility(Visibility::Collapsed);
    banner_panel.Children().Append(directory_status_banner_);

    error_banner_ = Border{};
    error_banner_.Padding(Thickness{12.0, 9.0, 12.0, 9.0});
    error_banner_.Background(dangerSoftBrush());
    error_banner_.CornerRadius(CornerRadius{8.0});
    error_status_text_ = texts_.block({}, 12.0);
    error_banner_.Child(error_status_text_);
    error_banner_.Visibility(Visibility::Collapsed);
    banner_panel.Children().Append(error_banner_);
    banner_panel.Margin(Thickness{0.0, 0.0, 0.0, 12.0});
    Grid::SetRow(banner_panel, 1);
    content.Children().Append(banner_panel);

    auto speakers_scroll = ScrollViewer{};
    speakers_scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    speaker_panel_ = StackPanel{};
    speaker_panel_.Spacing(0.0);
    speakers_scroll.Content(speaker_panel_);
    Grid::SetRow(speakers_scroll, 2);
    content.Children().Append(speakers_scroll);

    moderation_panel_ = Border{};
    moderation_panel_.Padding(Thickness{12.0});
    moderation_panel_.Margin(Thickness{0.0, 12.0, 0.0, 0.0});
    moderation_panel_.Background(warningSoftBrush());
    moderation_panel_.CornerRadius(CornerRadius{8.0});
    moderation_panel_.Child(texts_.block(text(IDS_MODERATION_AVAILABLE), 12.0));
    moderation_panel_.Visibility(Visibility::Collapsed);
    Grid::SetRow(moderation_panel_, 3);
    content.Children().Append(moderation_panel_);

    Grid::SetColumn(content, 1);
    ready_body_.Children().Append(content);
}

void MainWindow::buildVoiceDock()
{
    auto dock = Grid{};
    dock.Padding(Thickness{16.0, 11.0, 16.0, 11.0});
    dock.Background(surfaceBrush());
    dock.BorderBrush(borderBrush());
    dock.BorderThickness(Thickness{0.0, 1.0, 0.0, 0.0});
    auto status_column = ColumnDefinition{};
    status_column.Width(GridLength{220.0, GridUnitType::Pixel});
    auto ptt_column = ColumnDefinition{};
    ptt_column.Width(GridLength{1.0, GridUnitType::Star});
    auto action_column = ColumnDefinition{};
    action_column.Width(GridLength{150.0, GridUnitType::Pixel});
    dock.ColumnDefinitions().Append(status_column);
    dock.ColumnDefinitions().Append(ptt_column);
    dock.ColumnDefinitions().Append(action_column);

    auto voice_status = StackPanel{};
    voice_status.Spacing(2.0);
    voice_status.VerticalAlignment(VerticalAlignment::Center);
    active_scope_text_ = texts_.block({}, 14.0);
    active_scope_text_.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    connection_text_ = texts_.block({}, 11.0);
    connection_text_.Foreground(mutedBrush());
    send_text_ = texts_.block({}, 11.0);
    send_text_.Foreground(mutedBrush());
    receive_text_ = texts_.block({}, 11.0);
    receive_text_.Foreground(mutedBrush());
    voice_status.Children().Append(active_scope_text_);
    voice_status.Children().Append(connection_text_);
    voice_status.Children().Append(send_text_);
    voice_status.Children().Append(receive_text_);
    dock.Children().Append(voice_status);

    binding_summary_panel_ = StackPanel{};
    binding_summary_panel_.Orientation(Orientation::Horizontal);
    binding_summary_panel_.Spacing(8.0);
    binding_summary_panel_.HorizontalAlignment(HorizontalAlignment::Center);
    binding_summary_panel_.VerticalAlignment(VerticalAlignment::Center);

    const auto build_ptt_card = [this](domain::VoiceScope scope, Border& card,
                                       TextBlock& binding_text) {
        card = Border{};
        card.MinWidth(156.0);
        card.Padding(Thickness{12.0, 9.0, 12.0, 9.0});
        card.Background(softSurfaceBrush());
        card.BorderBrush(borderBrush());
        card.BorderThickness(Thickness{1.0});
        card.CornerRadius(CornerRadius{10.0});
        auto row = Grid{};
        auto icon_column = ColumnDefinition{};
        icon_column.Width(GridLength{28.0, GridUnitType::Pixel});
        auto label_column = ColumnDefinition{};
        label_column.Width(GridLength{1.0, GridUnitType::Star});
        row.ColumnDefinitions().Append(icon_column);
        row.ColumnDefinitions().Append(label_column);
        row.Children().Append(symbolIcon(scopeSymbol(scope)));
        auto labels = StackPanel{};
        labels.Spacing(0.0);
        auto label = texts_.block(scopeText(scope), 13.0);
        label.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        labels.Children().Append(label);
        binding_text = texts_.block({}, 11.0);
        binding_text.Foreground(mutedBrush());
        labels.Children().Append(binding_text);
        Grid::SetColumn(labels, 1);
        row.Children().Append(labels);
        card.Child(row);
        Automation::AutomationProperties::SetName(card, scopeText(scope));
        binding_summary_panel_.Children().Append(card);
    };

    build_ptt_card(domain::VoiceScope::team, team_ptt_card_, team_binding_text_);
    build_ptt_card(domain::VoiceScope::specialization, specialization_ptt_card_,
                   specialization_binding_text_);
    build_ptt_card(domain::VoiceScope::group, group_ptt_card_, group_binding_text_);
    Grid::SetColumn(binding_summary_panel_, 1);
    dock.Children().Append(binding_summary_panel_);

    auto disconnect = Button{};
    disconnect.HorizontalAlignment(HorizontalAlignment::Right);
    disconnect.VerticalAlignment(VerticalAlignment::Center);
    disconnect.Padding(Thickness{14.0, 9.0, 14.0, 9.0});
    disconnect.Background(colorBrush(0, 0, 0, 0));
    disconnect.BorderBrush(borderBrush());
    disconnect.BorderThickness(Thickness{1.0});
    disconnect.CornerRadius(CornerRadius{9.0});
    auto disconnect_content = StackPanel{};
    disconnect_content.Orientation(Orientation::Horizontal);
    disconnect_content.Spacing(8.0);
    disconnect_content.Children().Append(symbolIcon(Symbol::DisconnectDrive));
    disconnect_content.Children().Append(texts_.block(text(IDS_DISCONNECT), 13.0));
    disconnect.Content(disconnect_content);
    const auto weak = weak_from_this();
    disconnect.Click([weak](auto const&, RoutedEventArgs const&) {
        if (const auto self = weak.lock())
        {
            self->disconnectAsync();
        }
    });
    Grid::SetColumn(disconnect, 2);
    dock.Children().Append(disconnect);

    Grid::SetRow(dock, 2);
    ready_root_.Children().Append(dock);
}

void MainWindow::selectChannel(presentation::ChannelSelection selection)
{
    if (model_.selectChannel(std::move(selection)))
    {
        renderHierarchy();
        rebuildParticipants();
    }
}

auto MainWindow::windowHandle() const -> HWND
{
    const auto native_window = window_.as<::IWindowNative>();
    HWND handle{};
    check_hresult(native_window->get_WindowHandle(&handle));
    return handle;
}

void MainWindow::requestConnect()
{
    const auto labels = LoginDialogText{text(IDS_LOGIN_DETAILS).c_str(), text(IDS_SERVER).c_str(),
                                        text(IDS_CREDENTIAL).c_str(), text(IDS_CONNECT).c_str(),
                                        text(IDS_CANCEL).c_str()};
    auto result = showLoginDialog(windowHandle(), labels, server_url_);
    if (!result.accepted)
    {
        return;
    }
    server_url_ = result.server_url;
    connectAsync(std::move(result.server_url), std::move(result.credential));
}

fire_and_forget MainWindow::connectAsync(std::string server_url, std::string credential)
{
    const auto lifetime = shared_from_this();
    auto ui_thread = apartment_context{};
    if (!model_.beginConnect())
    {
        co_return;
    }
    render();
    const auto generation = ++session_generation_;
    const auto weak = weak_from_this();
    auto session = std::make_shared<ClientSession>([weak, generation](SessionEvent event) {
        if (const auto self = weak.lock())
        {
            self->dispatcher_.TryEnqueue([weak, generation, event = std::move(event)]() mutable {
                if (const auto queued_self = weak.lock())
                {
                    queued_self->handleSessionEvent(generation, std::move(event));
                }
            });
        }
    });

    co_await resume_background();
    auto result = session->connect(server_url, credential);
    std::fill(credential.begin(), credential.end(), '\0');
    credential.clear();
    presentation::SettingsState settings;
    if (result.successful && result.membership.has_value())
    {
        settings = settingsSnapshot(*session);
    }
    co_await ui_thread;
    if (closing_ || generation != session_generation_)
    {
        co_await resume_background();
        session->disconnect();
        co_return;
    }
    if (!result.successful || !result.membership.has_value() || !result.directory.has_value() ||
        !result.presence.has_value())
    {
        model_.connectionFailed("connection_failed", result.message);
        login_error_text_.Text(text(IDS_LOGIN_FAILED) + L" " + to_hstring(result.message));
        login_error_text_.Visibility(Visibility::Visible);
        render();
        co_return;
    }
    const auto applied = model_.connectionSucceeded(std::move(*result.membership));
    if (!applied)
    {
        model_.connectionFailed(std::string{presentation::errorCodeName(applied.error)},
                                "the initial membership is invalid for presentation");
        co_await resume_background();
        session->disconnect();
        co_await ui_thread;
        render();
        co_return;
    }
    const auto directory_applied = model_.applyDirectory(std::move(*result.directory));
    const auto presence_applied = model_.applyPresence(std::move(*result.presence));
    if (!directory_applied || !presence_applied)
    {
        const auto failed = !directory_applied ? directory_applied : presence_applied;
        model_.connectionFailed(std::string{presentation::errorCodeName(failed.error)},
                                failed.field);
        co_await resume_background();
        session->disconnect();
        co_await ui_thread;
        render();
        co_return;
    }
    session_ = std::move(session);
    model_.replaceSettings(std::move(settings));
    login_error_text_.Visibility(Visibility::Collapsed);
    applyTextScale();
    render();
}

fire_and_forget MainWindow::disconnectAsync()
{
    const auto lifetime = shared_from_this();
    auto ui_thread = apartment_context{};
    if (!model_.beginDisconnect())
    {
        co_return;
    }
    ++session_generation_;
    participant_update_timer_.Stop();
    pending_participant_updates_.clear();
    if (settings_window_ != nullptr)
    {
        settings_window_->close();
    }
    if (diagnostics_window_ != nullptr)
    {
        diagnostics_window_->close();
    }
    auto session = std::move(session_);
    render();
    co_await resume_background();
    if (session != nullptr)
    {
        session->disconnect();
    }
    co_await ui_thread;
    if (!closing_)
    {
        model_.disconnected();
        render();
    }
}

fire_and_forget MainWindow::shutdownSessionAsync(std::shared_ptr<ClientSession> session)
{
    const auto lifetime = shared_from_this();
    co_await resume_background();
    session->disconnect();
}

void MainWindow::handleSessionEvent(std::uint64_t generation, SessionEvent event)
{
    if (closing_ || generation != session_generation_)
    {
        return;
    }
    switch (event.kind)
    {
    case SessionEventKind::connection_state:
        if (event.connection_event.has_value())
        {
            model_.updateVoiceState(*event.connection_event);
        }
        break;
    case SessionEventKind::remote_voice:
        if (event.remote_event.has_value())
        {
            model_.applyVoiceRemoteEvent(*event.remote_event);
        }
        break;
    case SessionEventKind::transmission_started:
        if (event.scope.has_value())
        {
            model_.transmissionStarted(*event.scope);
        }
        break;
    case SessionEventKind::transmission_stopped:
        model_.transmissionStopped();
        break;
    case SessionEventKind::membership_updated:
        if (event.membership.has_value())
        {
            const auto updated = model_.updateMembership(std::move(*event.membership));
            if (!updated && updated.error != presentation::ErrorCode::stale_version)
            {
                model_.recordError(std::string{presentation::errorCodeName(updated.error)},
                                   updated.field);
            }
        }
        break;
    case SessionEventKind::directory_updated:
        if (event.directory.has_value())
        {
            const auto updated = model_.applyDirectory(std::move(*event.directory));
            if (!updated && updated.error != presentation::ErrorCode::stale_version)
            {
                model_.recordError(std::string{presentation::errorCodeName(updated.error)},
                                   updated.field);
            }
        }
        break;
    case SessionEventKind::presence_updated:
        if (event.presence.has_value())
        {
            const auto updated = model_.applyPresence(std::move(*event.presence));
            if (!updated && updated.error != presentation::ErrorCode::stale_version)
            {
                model_.recordError(std::string{presentation::errorCodeName(updated.error)},
                                   updated.field);
            }
        }
        break;
    case SessionEventKind::directory_error:
    case SessionEventKind::presence_error:
        model_.directoryRefreshFailed(event.error_code);
        model_.recordError(std::move(event.error_code), std::move(event.diagnostic));
        break;
    case SessionEventKind::error:
        model_.recordError(std::move(event.error_code), std::move(event.diagnostic));
        break;
    }
    render();
    if (diagnostics_window_ != nullptr && diagnostics_window_->isOpen())
    {
        diagnostics_window_->update(model_.state().diagnostics);
    }
}

void MainWindow::openSettings()
{
    if (!model_.validate(presentation::Command{presentation::CommandKind::open_settings}) ||
        session_ == nullptr)
    {
        return;
    }
    if (settings_window_ != nullptr && settings_window_->isOpen())
    {
        settings_window_->show();
        return;
    }
    const auto weak = weak_from_this();
    settings_window_ = std::make_shared<SettingsWindow>(
        session_, model_.state().settings, [weak](const presentation::SettingsState& settings) {
            if (const auto self = weak.lock())
            {
                self->settingsChanged(settings);
            }
        });
    settings_window_->show();
}

void MainWindow::openDiagnostics()
{
    if (!model_.validate(presentation::Command{presentation::CommandKind::open_diagnostics}))
    {
        return;
    }
    if (diagnostics_window_ == nullptr)
    {
        diagnostics_window_ = std::make_shared<DiagnosticsWindow>();
    }
    diagnostics_window_->show(model_.state().diagnostics,
                              model_.state().settings.text_scale_percent);
}

void MainWindow::settingsChanged(const presentation::SettingsState& settings)
{
    presentation::Command command{presentation::CommandKind::apply_settings};
    command.settings = settings;
    const auto validation = model_.validate(command);
    if (!validation)
    {
        model_.recordError(std::string{presentation::errorCodeName(validation.error)},
                           validation.field);
        return;
    }
    model_.replaceSettings(settings);
    applyTextScale();
    rebuildBindingSummary();
    rebuildParticipants();
}

void MainWindow::render()
{
    const auto& state = model_.state();
    const auto signed_out = state.connection == presentation::ConnectionPhase::signed_out;
    const auto connecting = state.connection == presentation::ConnectionPhase::connecting &&
                            !state.membership.has_value();
    login_view_.Visibility(signed_out || connecting ? Visibility::Visible : Visibility::Collapsed);
    ready_root_.Visibility(!signed_out && !connecting ? Visibility::Visible
                                                      : Visibility::Collapsed);
    connect_button_.IsEnabled(signed_out);
    if (connecting)
    {
        login_error_text_.Text(text(IDS_CONNECTING));
        login_error_text_.Visibility(Visibility::Visible);
    }
    ready_root_.IsHitTestVisible(state.connection != presentation::ConnectionPhase::disconnecting);
    if (!state.membership.has_value())
    {
        return;
    }
    renderHierarchy();
    renderStatus();
    rebuildBindingSummary();
    rebuildParticipants();
    moderation_panel_.Visibility(state.administration.can_moderate ||
                                         state.administration.can_administrate
                                     ? Visibility::Visible
                                     : Visibility::Collapsed);
}

void MainWindow::renderHierarchy()
{
    const auto& state = model_.state();
    const auto& membership = *state.membership;
    const auto local = state.participants.find(membership.player_id.value());
    identity_name_text_.Text(local == state.participants.end()
                                 ? to_hstring(membership.player_id.value())
                                 : to_hstring(local->second.display_name));
    identity_detail_text_.Text(
        to_hstring(membership.team_id.value()) + L" · " +
        (membership.can_receive_voice ? text(IDS_RECEIVE_ALLOWED) : text(IDS_RECEIVE_DISABLED)) +
        L" · " +
        (membership.transmit_muted ? text(IDS_TRANSMIT_MUTED) : text(IDS_TRANSMIT_ALLOWED)));

    const auto directory_version =
        state.directory.has_value() ? std::optional{state.directory->version} : std::nullopt;
    if (directory_version != rendered_directory_version_ ||
        state.directory_phase != rendered_directory_phase_)
    {
        rendered_directory_version_ = directory_version;
        rendered_directory_phase_ = state.directory_phase;
        rebuildHierarchyTree();
    }

    const auto selected = state.selected_channel.value_or(presentation::ChannelSelection{
        domain::VoiceScope::team, std::string{membership.team_id.value()}});
    const auto selected_node =
        std::ranges::find_if(state.channel_nodes, [&selected](const auto& candidate) {
            return candidate.channel == selected;
        });
    channel_icon_.Symbol(scopeSymbol(selected.scope));
    channel_title_text_.Text(selected_node == state.channel_nodes.end()
                                 ? to_hstring(selected.node_id)
                                 : to_hstring(selected_node->display_name));
    channel_description_text_.Text(scopeDescription(selected.scope));
    channel_access_text_.Text(
        (membership.can_receive_voice ? text(IDS_RECEIVE_ALLOWED) : text(IDS_RECEIVE_DISABLED)) +
        L" · " +
        (membership.transmit_muted ? text(IDS_TRANSMIT_MUTED) : text(IDS_TRANSMIT_ALLOWED)));

    std::vector<hstring> breadcrumb;
    auto current = selected_node;
    while (current != state.channel_nodes.end())
    {
        breadcrumb.push_back(to_hstring(current->display_name));
        if (!current->parent_node_id.has_value())
        {
            break;
        }
        const auto parent_id = *current->parent_node_id;
        current = std::ranges::find_if(state.channel_nodes, [&parent_id](const auto& candidate) {
            return candidate.channel.node_id == parent_id;
        });
    }
    std::ranges::reverse(breadcrumb);
    hstring breadcrumb_text;
    for (const auto& part : breadcrumb)
    {
        breadcrumb_text = breadcrumb_text.empty() ? part : breadcrumb_text + L" / " + part;
    }
    channel_breadcrumb_text_.Text(breadcrumb_text.empty() ? scopeText(selected.scope)
                                                          : breadcrumb_text);

    const auto directory_message = directoryPhaseText(state.directory_phase);
    directory_status_text_.Text(directory_message);
    directory_status_banner_.Visibility(directory_message.empty() ? Visibility::Collapsed
                                                                  : Visibility::Visible);
    directory_status_banner_.Background(
        state.directory_phase == presentation::DirectoryPhase::unauthorized ||
                state.directory_phase == presentation::DirectoryPhase::unavailable
            ? dangerSoftBrush()
            : warningSoftBrush());
    updateScopeButtonStyles();
    updatePttCardStyles();
}

void MainWindow::rebuildHierarchyTree()
{
    hierarchy_panel_.Children().Clear();
    channel_buttons_.clear();
    const auto& state = model_.state();
    if (state.channel_nodes.empty())
    {
        hierarchy_status_text_.Text(directoryPhaseText(state.directory_phase));
        hierarchy_panel_.Children().Append(hierarchy_status_text_);
        return;
    }

    const auto weak = weak_from_this();
    for (const auto& node : state.channel_nodes)
    {
        auto button = Button{};
        button.HorizontalAlignment(HorizontalAlignment::Stretch);
        button.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        button.MinHeight(48.0);
        button.Margin(Thickness{static_cast<double>(node.depth) * 14.0, 0.0, 0.0, 0.0});
        button.Padding(Thickness{10.0, 6.0, 8.0, 6.0});
        button.BorderThickness(Thickness{1.0});
        button.CornerRadius(CornerRadius{9.0});

        auto content = Grid{};
        auto icon_column = ColumnDefinition{};
        icon_column.Width(GridLength{28.0, GridUnitType::Pixel});
        auto text_column = ColumnDefinition{};
        text_column.Width(GridLength{1.0, GridUnitType::Star});
        content.ColumnDefinitions().Append(icon_column);
        content.ColumnDefinitions().Append(text_column);
        auto icon = symbolIcon(scopeSymbol(node.channel.scope));
        icon.VerticalAlignment(VerticalAlignment::Center);
        content.Children().Append(icon);

        auto labels = StackPanel{};
        labels.Spacing(1.0);
        auto name = texts_.block(to_hstring(node.display_name), 13.0);
        name.FontWeight(node.contains_local_player ? Windows::UI::Text::FontWeights::SemiBold()
                                                   : Windows::UI::Text::FontWeights::Normal());
        labels.Children().Append(name);
        auto detail = scopeText(node.channel.scope) + L" · " + to_hstring(node.participant_count) +
                      L" " + text(IDS_PARTICIPANTS);
        if (node.contains_local_player)
        {
            detail = detail + L" · " + text(IDS_YOU_ARE_HERE);
        }
        auto detail_text = texts_.block(detail, 10.0);
        detail_text.Foreground(mutedBrush());
        labels.Children().Append(detail_text);
        Grid::SetColumn(labels, 1);
        content.Children().Append(labels);
        button.Content(content);
        Automation::AutomationProperties::SetName(button, to_hstring(node.display_name) + L", " +
                                                              scopeText(node.channel.scope));
        const auto selection = node.channel;
        button.Click([weak, selection](auto const&, RoutedEventArgs const&) {
            if (const auto self = weak.lock())
            {
                self->selectChannel(selection);
            }
        });
        channel_buttons_.emplace(node.channel.node_id, button);
        hierarchy_panel_.Children().Append(button);
    }
    updateScopeButtonStyles();
}

void MainWindow::renderStatus()
{
    const auto& state = model_.state();
    const auto connection_value = connectionText(state.connection);
    connection_badge_text_.Text(connection_value);
    connection_text_.Text(labeledValue(IDS_CONNECTION, connection_value));
    switch (state.connection)
    {
    case presentation::ConnectionPhase::ready:
        connection_badge_.Background(successSoftBrush());
        break;
    case presentation::ConnectionPhase::connecting:
    case presentation::ConnectionPhase::reconnecting:
        connection_badge_.Background(warningSoftBrush());
        break;
    case presentation::ConnectionPhase::signed_out:
    case presentation::ConnectionPhase::disconnecting:
        connection_badge_.Background(softSurfaceBrush());
        break;
    }

    const auto sending = state.active_transmission_scope.has_value();
    send_text_.Text(labeledValue(IDS_SEND, sending ? scopeText(*state.active_transmission_scope)
                                                   : text(IDS_SEND_IDLE)));
    const auto receiving = std::ranges::any_of(
        state.participants, [](const auto& entry) { return entry.second.speaking; });
    receive_text_.Text(
        labeledValue(IDS_RECEIVE, receiving ? text(IDS_RECEIVE_ACTIVE) : text(IDS_RECEIVE_IDLE)));
    active_scope_text_.Text(text(IDS_PUSH_TO_TALK));
    if (sending)
    {
        active_scope_text_.Text(text(IDS_SEND_ACTIVE) + L" · " +
                                scopeText(*state.active_transmission_scope));
    }

    const auto has_error = !state.diagnostics.last_error_code.empty();
    error_banner_.Visibility(has_error ? Visibility::Visible : Visibility::Collapsed);
    if (has_error)
    {
        error_status_text_.Text(
            labeledValue(IDS_ERROR, to_hstring(state.diagnostics.last_error_code)));
    }
    updatePttCardStyles();
}

void MainWindow::rebuildBindingSummary()
{
    const auto& bindings = model_.state().settings.bindings;
    team_binding_text_.Text(bindingLabel(bindings, client::PushToTalkAction::team) + L" · PTT");
    specialization_binding_text_.Text(
        bindingLabel(bindings, client::PushToTalkAction::specialization) + L" · PTT");
    group_binding_text_.Text(bindingLabel(bindings, client::PushToTalkAction::group) + L" · PTT");
}

void MainWindow::rebuildParticipants()
{
    speaker_panel_.Children().Clear();
    const auto& state = model_.state();
    const auto& membership = *state.membership;
    const auto strong = state.settings.strong_speaker_indicators;
    const auto weak = weak_from_this();
    const auto selected_scope = state.selected_channel.has_value() ? state.selected_channel->scope
                                                                   : domain::VoiceScope::team;
    const auto participant_count = state.selected_participant_ids.size();
    auto found_participant = false;

    const auto avatar = [this](const hstring& name, bool speaking) {
        auto result = Border{};
        result.Width(40.0);
        result.Height(40.0);
        result.CornerRadius(CornerRadius{20.0});
        result.Background(speaking ? successSoftBrush() : accentSoftBrush());
        result.BorderBrush(speaking ? successBrush() : colorBrush(0, 0, 0, 0));
        result.BorderThickness(Thickness{speaking ? 2.0 : 0.0});
        auto initials_text = texts_.block(initials(name), 13.0);
        initials_text.HorizontalAlignment(HorizontalAlignment::Center);
        initials_text.VerticalAlignment(VerticalAlignment::Center);
        initials_text.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        result.Child(initials_text);
        return result;
    };

    const auto scope_pill = [this](const hstring& label, bool active) {
        auto result = Border{};
        result.Padding(Thickness{8.0, 3.0, 8.0, 3.0});
        result.CornerRadius(CornerRadius{999.0});
        result.Background(active ? successSoftBrush() : softSurfaceBrush());
        auto label_text = texts_.block(label, 11.0);
        label_text.Foreground(active ? foregroundBrush() : mutedBrush());
        result.Child(label_text);
        return result;
    };

    const auto show_self =
        std::ranges::find(state.selected_participant_ids, membership.player_id.value()) !=
        state.selected_participant_ids.end();
    if (show_self)
    {
        auto self_row = Grid{};
        self_row.Padding(Thickness{4.0, 12.0, 4.0, 12.0});
        auto self_avatar_column = ColumnDefinition{};
        self_avatar_column.Width(GridLength{52.0, GridUnitType::Pixel});
        auto self_name_column = ColumnDefinition{};
        self_name_column.Width(GridLength{1.0, GridUnitType::Star});
        auto self_status_column = ColumnDefinition{};
        self_status_column.Width(GridLength{0.0, GridUnitType::Auto});
        self_row.ColumnDefinitions().Append(self_avatar_column);
        self_row.ColumnDefinitions().Append(self_name_column);
        self_row.ColumnDefinitions().Append(self_status_column);
        auto self_copy = StackPanel{};
        self_copy.Spacing(2.0);
        const auto self_participant = state.participants.find(membership.player_id.value());
        const auto self_display_name = self_participant == state.participants.end()
                                           ? to_hstring(membership.player_id.value())
                                           : to_hstring(self_participant->second.display_name);
        self_row.Children().Append(
            avatar(self_display_name, state.active_transmission_scope.has_value() &&
                                          *state.active_transmission_scope == selected_scope));
        auto self_name = texts_.block(self_display_name, 15.0);
        self_name.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        self_copy.Children().Append(self_name);
        auto self_detail =
            texts_.block(text(IDS_YOU) + L" · " +
                             (self_participant == state.participants.end()
                                  ? rolesText(membership)
                                  : publicRolesText(self_participant->second, state.directory)),
                         11.0);
        self_detail.Foreground(mutedBrush());
        self_copy.Children().Append(self_detail);
        Grid::SetColumn(self_copy, 1);
        self_row.Children().Append(self_copy);
        auto self_status = scope_pill(state.active_transmission_scope.has_value()
                                          ? scopeText(*state.active_transmission_scope) + L" · " +
                                                text(IDS_SPEAKING)
                                          : text(IDS_CONNECTION_CONNECTED),
                                      state.active_transmission_scope.has_value());
        self_status.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(self_status, 2);
        self_row.Children().Append(self_status);
        speaker_panel_.Children().Append(self_row);
        speaker_panel_.Children().Append(separator());
        found_participant = true;
    }

    for (const auto& participant_id : state.selected_participant_ids)
    {
        if (participant_id == membership.player_id.value())
        {
            continue;
        }
        const auto participant_entry = state.participants.find(participant_id);
        if (participant_entry == state.participants.end())
        {
            continue;
        }
        const auto& participant = participant_entry->second;
        found_participant = true;
        auto row = Grid{};
        row.Padding(Thickness{4.0, 12.0, 4.0, 12.0});
        auto avatar_column = ColumnDefinition{};
        avatar_column.Width(GridLength{52.0, GridUnitType::Pixel});
        auto name_column = ColumnDefinition{};
        name_column.Width(GridLength{1.0, GridUnitType::Star});
        auto scope_column = ColumnDefinition{};
        scope_column.Width(GridLength{0.0, GridUnitType::Auto});
        auto volume_column = ColumnDefinition{};
        volume_column.Width(GridLength{188.0, GridUnitType::Pixel});
        auto mute_column = ColumnDefinition{};
        mute_column.Width(GridLength{48.0, GridUnitType::Pixel});
        auto block_column = ColumnDefinition{};
        block_column.Width(GridLength{48.0, GridUnitType::Pixel});
        row.ColumnDefinitions().Append(avatar_column);
        row.ColumnDefinitions().Append(name_column);
        row.ColumnDefinitions().Append(scope_column);
        row.ColumnDefinitions().Append(volume_column);
        row.ColumnDefinitions().Append(mute_column);
        row.ColumnDefinitions().Append(block_column);

        const auto display_name = to_hstring(participant.display_name);
        row.Children().Append(avatar(display_name, participant.speaking));

        auto copy = StackPanel{};
        copy.Spacing(2.0);
        auto name = texts_.block(display_name, strong ? 17.0 : 15.0);
        name.FontWeight(strong ? Windows::UI::Text::FontWeights::Bold()
                               : Windows::UI::Text::FontWeights::SemiBold());
        copy.Children().Append(name);
        auto detail = texts_.block(publicRolesText(participant, state.directory) + L" · " +
                                       presenceText(participant.presence) + L" · " +
                                       (participant.audio_available ? text(IDS_AUDIO_AVAILABLE)
                                                                    : text(IDS_AUDIO_UNAVAILABLE)),
                                   11.0);
        detail.Foreground(mutedBrush());
        copy.Children().Append(detail);
        Grid::SetColumn(copy, 1);
        row.Children().Append(copy);

        auto participant_status =
            participant.speaking && participant.speaking_scope.has_value()
                ? scopeText(*participant.speaking_scope) + L" · " + text(IDS_SPEAKING)
                : text(IDS_NOT_SPEAKING);
        if (participant.blocked)
        {
            participant_status = participant_status + L" · " + text(IDS_BLOCKED);
        }
        auto status_pill = scope_pill(participant_status, participant.speaking);
        status_pill.Margin(Thickness{8.0, 0.0, 10.0, 0.0});
        status_pill.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(status_pill, 2);
        row.Children().Append(status_pill);

        auto volume = Slider{};
        volume.Minimum(0.0);
        volume.Maximum(100.0);
        volume.Value(static_cast<double>(participant.volume) * 100.0);
        volume.StepFrequency(5.0);
        volume.ValueChanged(
            [weak, participant_id](
                auto const& sender,
                Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&) {
                if (const auto self = weak.lock())
                {
                    const auto slider = sender.as<Slider>();
                    self->queueParticipantVolume(participant_id,
                                                 static_cast<float>(slider.Value() / 100.0));
                }
            });
        volume.VerticalAlignment(VerticalAlignment::Center);
        Automation::AutomationProperties::SetName(volume, text(IDS_VOLUME));
        Automation::AutomationProperties::SetAutomationId(volume, L"participant-volume-" +
                                                                      to_hstring(participant_id));

        auto volume_controls = Grid{};
        auto volume_down_column = ColumnDefinition{};
        volume_down_column.Width(GridLength{30.0, GridUnitType::Pixel});
        auto volume_slider_column = ColumnDefinition{};
        volume_slider_column.Width(GridLength{1.0, GridUnitType::Star});
        auto volume_up_column = ColumnDefinition{};
        volume_up_column.Width(GridLength{30.0, GridUnitType::Pixel});
        volume_controls.ColumnDefinitions().Append(volume_down_column);
        volume_controls.ColumnDefinitions().Append(volume_slider_column);
        volume_controls.ColumnDefinitions().Append(volume_up_column);

        const auto volume_button = [this](const hstring& label, const hstring& accessible_name,
                                          const hstring& automation_id) {
            auto button = Button{};
            button.Width(28.0);
            button.Height(32.0);
            button.Padding(Thickness{0.0});
            button.Content(texts_.block(label, 15.0));
            button.HorizontalContentAlignment(HorizontalAlignment::Center);
            button.VerticalContentAlignment(VerticalAlignment::Center);
            button.Background(colorBrush(0, 0, 0, 0));
            button.BorderBrush(borderBrush());
            button.CornerRadius(CornerRadius{7.0});
            Automation::AutomationProperties::SetName(button, accessible_name);
            Automation::AutomationProperties::SetAutomationId(button, automation_id);
            ToolTipService::SetToolTip(button, box_value(accessible_name));
            return button;
        };
        auto volume_down = volume_button(L"\u2212", text(IDS_VOLUME_DOWN),
                                         L"participant-volume-down-" + to_hstring(participant_id));
        volume_down.Click([volume](auto const&, RoutedEventArgs const&) {
            volume.Value(std::max(volume.Minimum(), volume.Value() - volume.StepFrequency()));
        });
        volume_controls.Children().Append(volume_down);
        Grid::SetColumn(volume, 1);
        volume_controls.Children().Append(volume);
        auto volume_up = volume_button(L"+", text(IDS_VOLUME_UP),
                                       L"participant-volume-up-" + to_hstring(participant_id));
        volume_up.Click([volume](auto const&, RoutedEventArgs const&) {
            volume.Value(std::min(volume.Maximum(), volume.Value() + volume.StepFrequency()));
        });
        Grid::SetColumn(volume_up, 2);
        volume_controls.Children().Append(volume_up);
        Grid::SetColumn(volume_controls, 3);
        row.Children().Append(volume_controls);

        auto mute =
            iconButton(participant.muted ? Symbol::Mute : Symbol::Volume, text(IDS_LOCAL_MUTE));
        mute.Width(38.0);
        mute.Height(38.0);
        mute.Background(participant.muted ? accentSoftBrush() : colorBrush(0, 0, 0, 0));
        mute.BorderBrush(borderBrush());
        Automation::AutomationProperties::SetAutomationId(mute, L"participant-mute-" +
                                                                    to_hstring(participant_id));
        mute.Click([weak, participant_id, muted = !participant.muted](auto const&,
                                                                      RoutedEventArgs const&) {
            if (const auto self = weak.lock())
            {
                self->queueParticipantMuted(participant_id, muted);
            }
        });
        mute.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(mute, 4);
        row.Children().Append(mute);

        auto block = iconButton(Symbol::Cancel, text(IDS_LOCAL_BLOCK));
        block.Width(38.0);
        block.Height(38.0);
        block.Background(participant.blocked ? dangerSoftBrush() : colorBrush(0, 0, 0, 0));
        block.BorderBrush(borderBrush());
        Automation::AutomationProperties::SetAutomationId(block, L"participant-block-" +
                                                                     to_hstring(participant_id));
        block.Click([weak, participant_id, blocked = !participant.blocked](auto const&,
                                                                           RoutedEventArgs const&) {
            if (const auto self = weak.lock())
            {
                self->queueParticipantBlocked(participant_id, blocked);
            }
        });
        block.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(block, 5);
        row.Children().Append(block);
        speaker_panel_.Children().Append(row);
        speaker_panel_.Children().Append(separator());
    }
    if (!found_participant)
    {
        auto empty = Border{};
        empty.Margin(Thickness{0.0, 18.0, 0.0, 0.0});
        empty.Padding(Thickness{24.0});
        empty.Background(softSurfaceBrush());
        empty.CornerRadius(CornerRadius{12.0});
        auto content = StackPanel{};
        content.Spacing(6.0);
        content.HorizontalAlignment(HorizontalAlignment::Center);
        auto icon = symbolIcon(Symbol::People);
        icon.Width(24.0);
        icon.Height(24.0);
        content.Children().Append(icon);
        auto title = texts_.block(text(IDS_EMPTY_CHANNEL_TITLE), 16.0);
        title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        title.HorizontalAlignment(HorizontalAlignment::Center);
        content.Children().Append(title);
        auto detail = texts_.block(text(IDS_EMPTY_CHANNEL_DETAIL), 12.0);
        detail.Foreground(mutedBrush());
        detail.TextAlignment(TextAlignment::Center);
        content.Children().Append(detail);
        empty.Child(content);
        speaker_panel_.Children().Append(empty);
    }

    participant_count_text_.Text(to_hstring(participant_count) + L" " + text(IDS_PARTICIPANTS));
}

void MainWindow::updateScopeButtonStyles()
{
    const auto& selected = model_.state().selected_channel;
    for (const auto& [node_id, button] : channel_buttons_)
    {
        const auto is_selected = selected.has_value() && node_id == selected->node_id;
        button.Background(is_selected ? accentSoftBrush() : colorBrush(0, 0, 0, 0));
        button.BorderBrush(is_selected ? accentBrush() : colorBrush(0, 0, 0, 0));
    }
}

void MainWindow::updatePttCardStyles()
{
    if (team_ptt_card_ == nullptr)
    {
        return;
    }
    const auto& state = model_.state();
    const auto selected_scope = state.selected_channel.has_value() ? state.selected_channel->scope
                                                                   : domain::VoiceScope::team;
    const std::array cards{
        std::pair{domain::VoiceScope::team, team_ptt_card_},
        std::pair{domain::VoiceScope::specialization, specialization_ptt_card_},
        std::pair{domain::VoiceScope::group, group_ptt_card_},
    };
    for (const auto& [scope, card] : cards)
    {
        const auto transmitting = state.active_transmission_scope.has_value() &&
                                  *state.active_transmission_scope == scope;
        const auto selected = selected_scope == scope;
        card.Background(transmitting ? accentBrush()
                                     : (selected ? accentSoftBrush() : softSurfaceBrush()));
        card.BorderBrush(transmitting || selected ? accentBrush() : borderBrush());
    }
}

void MainWindow::applyTextScale()
{
    const auto scale = model_.state().settings.text_scale_percent;
    texts_.setScale(scale);
    if (diagnostics_window_ != nullptr && diagnostics_window_->isOpen())
    {
        diagnostics_window_->setTextScale(scale);
    }
}

void MainWindow::queueParticipantVolume(const std::string& participant_id, float volume)
{
    pending_participant_updates_[participant_id].volume = volume;
    startParticipantUpdateTimer();
}

void MainWindow::queueParticipantMuted(const std::string& participant_id, bool muted)
{
    pending_participant_updates_[participant_id].muted = muted;
    startParticipantUpdateTimer();
}

void MainWindow::queueParticipantBlocked(const std::string& participant_id, bool blocked)
{
    pending_participant_updates_[participant_id].blocked = blocked;
    startParticipantUpdateTimer();
}

void MainWindow::startParticipantUpdateTimer()
{
    if (!participant_updates_running_ && participant_update_timer_ != nullptr)
    {
        participant_update_timer_.Stop();
        participant_update_timer_.Start();
    }
}

fire_and_forget MainWindow::applyParticipantUpdatesAsync()
{
    const auto lifetime = shared_from_this();
    auto ui_thread = apartment_context{};
    if (participant_updates_running_ || session_ == nullptr || pending_participant_updates_.empty())
    {
        co_return;
    }
    participant_updates_running_ = true;
    auto updates = std::move(pending_participant_updates_);
    pending_participant_updates_.clear();
    const auto session = session_;
    const auto generation = session_generation_;
    std::vector<std::pair<std::string, client::VoiceTransportResult>> results;
    std::map<std::string, PendingParticipantUpdate, std::less<>> applied_updates;
    co_await resume_background();
    for (const auto& [participant_id, update] : updates)
    {
        auto result = client::VoiceTransportResult::success();
        if (update.volume.has_value())
        {
            result = session->setParticipantVolume(participant_id, *update.volume);
            if (result)
            {
                applied_updates[participant_id].volume = update.volume;
            }
        }
        if (result && update.muted.has_value())
        {
            result = session->setParticipantMuted(participant_id, *update.muted);
            if (result)
            {
                applied_updates[participant_id].muted = update.muted;
            }
        }
        if (result && update.blocked.has_value())
        {
            result = session->setParticipantBlocked(participant_id, *update.blocked);
            if (result)
            {
                applied_updates[participant_id].blocked = update.blocked;
            }
        }
        results.emplace_back(participant_id, std::move(result));
    }
    co_await ui_thread;
    participant_updates_running_ = false;
    if (closing_ || generation != session_generation_)
    {
        co_return;
    }
    for (const auto& [participant_id, result] : results)
    {
        const auto update = applied_updates.find(participant_id);
        if (update != applied_updates.end())
        {
            if (update->second.volume.has_value())
            {
                static_cast<void>(
                    model_.setParticipantVolume(participant_id, *update->second.volume));
            }
            if (update->second.muted.has_value())
            {
                static_cast<void>(
                    model_.setParticipantMuted(participant_id, *update->second.muted));
            }
            if (update->second.blocked.has_value())
            {
                static_cast<void>(
                    model_.setParticipantBlocked(participant_id, *update->second.blocked));
            }
        }
        if (!result)
        {
            showOperationFailure(result);
        }
    }
    rebuildParticipants();
    if (!pending_participant_updates_.empty())
    {
        startParticipantUpdateTimer();
    }
}

void MainWindow::showOperationFailure(const client::VoiceTransportResult& result)
{
    model_.recordError(std::to_string(static_cast<std::uint8_t>(result.error)), result.message);
    renderStatus();
    if (diagnostics_window_ != nullptr && diagnostics_window_->isOpen())
    {
        diagnostics_window_->update(model_.state().diagnostics);
    }
}

auto MainWindow::settingsSnapshot(const ClientSession& session) -> presentation::SettingsState
{
    presentation::SettingsState settings;
    settings.audio = session.audioEngineConfig();
    settings.recording_devices = session.recordingDevices();
    settings.playout_devices = session.playoutDevices();
    settings.bindings = session.bindings();
    settings.input_devices = session.inputDevices();
    if (!settings.recording_devices.empty())
    {
        settings.recording_device_id = settings.recording_devices.front().id;
    }
    if (!settings.playout_devices.empty())
    {
        settings.playout_device_id = settings.playout_devices.front().id;
    }
    return settings;
}
} // namespace hvc::windows_client
