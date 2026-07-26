#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../resources/resource.h"
#include "client_session.hpp"
#include "localized_text.hpp"
#include "login_dialog.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <map>
#include <memory>
#include <microsoft.ui.xaml.window.h>
#include <string>
#include <utility>
#include <vector>
#include <windows.h>
#undef GetCurrentTime
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/base.h>

namespace hvc::windows_client
{
using namespace winrt;
using namespace Microsoft::UI;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using Windows::Foundation::IAsyncAction;

namespace
{
[[nodiscard]] auto text(std::uint32_t resource_id) -> hstring
{
    return localizedText(resource_id);
}

[[nodiscard]] auto scopeText(domain::VoiceScope scope) -> hstring
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

[[nodiscard]] auto labeledValue(std::uint32_t label_id, const hstring& value) -> hstring
{
    return text(label_id) + L": " + value;
}

[[nodiscard]] auto rolesText(const client::MembershipView& membership) -> hstring
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

[[nodiscard]] auto hasModeratorRole(const client::MembershipView& membership) -> bool
{
    return std::ranges::any_of(membership.role_ids, [](const auto& role_id) {
        auto role = std::string{role_id.value()};
        std::ranges::transform(role, role.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return role.find("moderator") != std::string::npos ||
               role.find("administrator") != std::string::npos || role == "admin";
    });
}

[[nodiscard]] auto keyboardBinding(client::PushToTalkAction action, std::uint16_t virtual_key)
    -> client::InputBinding
{
    return {action, {{client::InputDeviceKind::keyboard, 0, virtual_key, false, {}}}};
}

} // namespace

class App : public ApplicationT<App>
{
  public:
    void OnLaunched(LaunchActivatedEventArgs const&)
    {
        window_ = Window{};
        window_.Title(text(IDS_APP_TITLE));
        dispatcher_ = window_.DispatcherQueue();

        auto root = Grid{};
        root.Padding(Thickness{24.0});
        root.MaxWidth(1040.0);
        root.HorizontalAlignment(HorizontalAlignment::Stretch);
        root.VerticalAlignment(VerticalAlignment::Stretch);

        buildLoginView();
        buildReadyView();
        root.Children().Append(login_panel_);
        root.Children().Append(ready_scroll_);
        window_.Content(root);
        window_.Activate();
    }

  private:
    struct SpeakerState final
    {
        domain::VoiceScope scope{domain::VoiceScope::team};
        bool muted{false};
        double volume{100.0};
    };

    auto trackedText(const hstring& value, double font_size = 14.0) -> TextBlock
    {
        auto block = TextBlock{};
        block.Text(value);
        block.FontSize(font_size);
        block.TextWrapping(TextWrapping::Wrap);
        scalable_text_.emplace_back(block, font_size);
        return block;
    }

    auto transientText(const hstring& value, double font_size = 14.0) const -> TextBlock
    {
        const auto scale = text_scale_slider_ == nullptr ? 1.0 : text_scale_slider_.Value() / 100.0;
        auto block = TextBlock{};
        block.Text(value);
        block.FontSize(font_size * scale);
        block.TextWrapping(TextWrapping::Wrap);
        return block;
    }

    auto heading(std::uint32_t resource_id, double font_size = 22.0) -> TextBlock
    {
        auto block = trackedText(text(resource_id), font_size);
        block.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        return block;
    }

    auto section(std::uint32_t title_id, const UIElement& content, bool = true) -> StackPanel
    {
        auto section_panel = StackPanel{};
        section_panel.Spacing(8.0);
        section_panel.Children().Append(heading(title_id, 20.0));
        section_panel.Children().Append(content);
        return section_panel;
    }

    auto labeledSlider(std::uint32_t label_id, double minimum, double maximum, double value,
                       double step) -> std::pair<StackPanel, Slider>
    {
        auto panel = StackPanel{};
        panel.Spacing(4.0);
        panel.Children().Append(trackedText(text(label_id)));
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

    void buildLoginView()
    {
        login_panel_ = StackPanel{};
        login_panel_.Spacing(16.0);
        login_panel_.VerticalAlignment(VerticalAlignment::Center);
        login_panel_.MaxWidth(720.0);

        login_panel_.Children().Append(heading(IDS_APP_TITLE, 32.0));
        auto introduction = trackedText(text(IDS_LOGIN_INTRODUCTION));
        introduction.Opacity(0.72);
        login_panel_.Children().Append(introduction);

        connect_button_ = Button{};
        connect_button_.Content(box_value(text(IDS_CONNECT)));
        connect_button_.HorizontalAlignment(HorizontalAlignment::Left);
        connect_button_.Click([this](IInspectable const&, RoutedEventArgs const&) {
            const auto labels = LoginDialogText{
                text(IDS_LOGIN_DETAILS).c_str(), text(IDS_SERVER).c_str(),
                text(IDS_CREDENTIAL).c_str(), text(IDS_CONNECT).c_str(), text(IDS_CANCEL).c_str()};
            const auto result = showLoginDialog(windowHandle(), labels, server_url_);
            if (!result.accepted)
            {
                return;
            }
            server_url_ = result.server_url;
            credential_ = result.credential;
            pending_action_ = connectAsync();
        });
        login_panel_.Children().Append(connect_button_);

        error_text_ = trackedText({});
        error_text_.TextWrapping(TextWrapping::Wrap);
        error_text_.Visibility(Visibility::Collapsed);
        login_panel_.Children().Append(error_text_);
    }

    [[nodiscard]] auto windowHandle() const -> HWND
    {
        const auto native_window = window_.as<::IWindowNative>();
        HWND handle{};
        check_hresult(native_window->get_WindowHandle(&handle));
        return handle;
    }

    void buildReadyView()
    {
        ready_scroll_ = ScrollViewer{};
        ready_scroll_.Visibility(Visibility::Collapsed);
        ready_scroll_.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);

        ready_panel_ = StackPanel{};
        ready_panel_.Spacing(14.0);
        ready_panel_.Children().Append(heading(IDS_READY, 30.0));

        status_summary_ = trackedText(text(IDS_STATUS_READY));
        status_summary_.Opacity(0.76);
        ready_panel_.Children().Append(status_summary_);

        auto disconnect_button = Button{};
        disconnect_button.Content(box_value(text(IDS_DISCONNECT)));
        disconnect_button.HorizontalAlignment(HorizontalAlignment::Left);
        disconnect_button.Click([this](IInspectable const&, RoutedEventArgs const&) {
            pending_action_ = disconnectAsync();
        });
        ready_panel_.Children().Append(disconnect_button);

        buildHierarchySection();
        buildStatusSection();
        buildScopeSection();
        buildSpeakerSection();
        buildSettingsSection();
        buildModerationSection();

        ready_scroll_.Content(ready_panel_);
    }

    void buildHierarchySection()
    {
        hierarchy_panel_ = StackPanel{};
        hierarchy_panel_.Spacing(6.0);
        hierarchy_panel_.Children().Append(
            trackedText(labeledValue(IDS_PLAYER, text(IDS_UNKNOWN))));
        hierarchy_panel_.Children().Append(trackedText(labeledValue(IDS_GROUP, text(IDS_UNKNOWN))));
        hierarchy_panel_.Children().Append(
            trackedText(labeledValue(IDS_SPECIALIZATION, text(IDS_UNKNOWN))));
        hierarchy_panel_.Children().Append(trackedText(labeledValue(IDS_TEAM, text(IDS_UNKNOWN))));
        hierarchy_panel_.Children().Append(trackedText(labeledValue(IDS_ROLES, text(IDS_UNKNOWN))));
        hierarchy_panel_.Children().Append(
            trackedText(labeledValue(IDS_MEMBERSHIP_VERSION, text(IDS_UNKNOWN))));
        ready_panel_.Children().Append(section(IDS_HIERARCHY, hierarchy_panel_));
    }

    void buildStatusSection()
    {
        auto panel = StackPanel{};
        panel.Spacing(6.0);
        connection_text_ =
            trackedText(labeledValue(IDS_CONNECTION, text(IDS_CONNECTION_CONNECTED)));
        send_text_ = trackedText(labeledValue(IDS_SEND, text(IDS_SEND_IDLE)));
        receive_text_ = trackedText(labeledValue(IDS_RECEIVE, text(IDS_RECEIVE_IDLE)));
        error_status_text_ = trackedText(labeledValue(IDS_ERROR, text(IDS_DIAGNOSTICS_EMPTY)));
        panel.Children().Append(connection_text_);
        panel.Children().Append(send_text_);
        panel.Children().Append(receive_text_);
        panel.Children().Append(error_status_text_);

        diagnostics_ = StackPanel{};
        diagnostics_.Spacing(4.0);
        diagnostics_.Children().Append(trackedText(text(IDS_SHOW_DIAGNOSTICS)));
        diagnostics_text_ = trackedText(text(IDS_DIAGNOSTICS_EMPTY));
        diagnostics_.Children().Append(diagnostics_text_);
        panel.Children().Append(diagnostics_);
        ready_panel_.Children().Append(section(IDS_VOICE_STATUS, panel));
    }

    void buildScopeSection()
    {
        auto panel = StackPanel{};
        panel.Spacing(8.0);
        active_scope_text_ =
            trackedText(labeledValue(IDS_ACTIVE_SCOPE, text(IDS_SCOPE_IDLE)), 18.0);
        active_scope_text_.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        panel.Children().Append(active_scope_text_);
        binding_summary_panel_ = StackPanel{};
        binding_summary_panel_.Spacing(4.0);
        panel.Children().Append(binding_summary_panel_);
        ready_panel_.Children().Append(section(IDS_SCOPE_AND_BINDINGS, panel));
    }

    void buildSpeakerSection()
    {
        speaker_panel_ = StackPanel{};
        speaker_panel_.Spacing(10.0);
        ready_panel_.Children().Append(section(IDS_SPEAKERS, speaker_panel_));
        rebuildSpeakers();
    }

    void buildSettingsSection()
    {
        auto settings_panel = StackPanel{};
        settings_panel.Spacing(10.0);

        auto audio_panel = StackPanel{};
        audio_panel.Spacing(10.0);
        recording_combo_ = ComboBox{};
        recording_combo_.Header(box_value(text(IDS_RECORDING_DEVICE)));
        recording_combo_.HorizontalAlignment(HorizontalAlignment::Stretch);
        audio_panel.Children().Append(recording_combo_);
        playout_combo_ = ComboBox{};
        playout_combo_.Header(box_value(text(IDS_PLAYOUT_DEVICE)));
        playout_combo_.HorizontalAlignment(HorizontalAlignment::Stretch);
        audio_panel.Children().Append(playout_combo_);

        auto [maximum_panel, maximum_slider] =
            labeledSlider(IDS_MAXIMUM_STREAMS, 1.0, 16.0, 8.0, 1.0);
        maximum_streams_slider_ = maximum_slider;
        audio_panel.Children().Append(maximum_panel);
        auto [team_panel, team_slider] = labeledSlider(IDS_TEAM_STREAMS, 1.0, 8.0, 5.0, 1.0);
        team_streams_slider_ = team_slider;
        audio_panel.Children().Append(team_panel);
        auto [specialization_panel, specialization_slider] =
            labeledSlider(IDS_SPECIALIZATION_STREAMS, 1.0, 8.0, 4.0, 1.0);
        specialization_streams_slider_ = specialization_slider;
        audio_panel.Children().Append(specialization_panel);
        auto [group_panel, group_slider] = labeledSlider(IDS_GROUP_STREAMS, 1.0, 8.0, 2.0, 1.0);
        group_streams_slider_ = group_slider;
        audio_panel.Children().Append(group_panel);
        auto [team_specialization_panel, team_specialization_slider] =
            labeledSlider(IDS_TEAM_UNDER_SPECIALIZATION, 0.0, 100.0, 50.0, 5.0);
        team_under_specialization_slider_ = team_specialization_slider;
        audio_panel.Children().Append(team_specialization_panel);
        auto [team_group_panel, team_group_slider] =
            labeledSlider(IDS_TEAM_UNDER_GROUP, 0.0, 100.0, 25.0, 5.0);
        team_under_group_slider_ = team_group_slider;
        audio_panel.Children().Append(team_group_panel);
        auto [specialization_group_panel, specialization_group_slider] =
            labeledSlider(IDS_SPECIALIZATION_UNDER_GROUP, 0.0, 100.0, 50.0, 5.0);
        specialization_under_group_slider_ = specialization_group_slider;
        audio_panel.Children().Append(specialization_group_panel);

        auto apply_audio = Button{};
        apply_audio.Content(box_value(text(IDS_APPLY_AUDIO)));
        apply_audio.Click([this](IInspectable const&, RoutedEventArgs const&) {
            pending_action_ = applyAudioSettingsAsync();
        });
        audio_panel.Children().Append(apply_audio);
        settings_panel.Children().Append(section(IDS_AUDIO_SETTINGS, audio_panel, false));

        auto input_panel = StackPanel{};
        input_panel.Spacing(10.0);
        input_devices_text_ = trackedText(text(IDS_NO_INPUT_DEVICES));
        input_panel.Children().Append(input_devices_text_);
        team_binding_combo_ = bindingCombo(IDS_BINDING_TEAM, 8);
        specialization_binding_combo_ = bindingCombo(IDS_BINDING_SPECIALIZATION, 9);
        group_binding_combo_ = bindingCombo(IDS_BINDING_GROUP, 10);
        input_panel.Children().Append(team_binding_combo_);
        input_panel.Children().Append(specialization_binding_combo_);
        input_panel.Children().Append(group_binding_combo_);
        auto apply_bindings = Button{};
        apply_bindings.Content(box_value(text(IDS_APPLY_BINDINGS)));
        apply_bindings.Click(
            [this](IInspectable const&, RoutedEventArgs const&) { applyBindings(); });
        input_panel.Children().Append(apply_bindings);
        settings_panel.Children().Append(section(IDS_INPUT_SETTINGS, input_panel, false));

        auto accessibility_panel = StackPanel{};
        accessibility_panel.Spacing(10.0);
        auto [scale_panel, scale_slider] = labeledSlider(IDS_TEXT_SCALE, 100.0, 150.0, 100.0, 10.0);
        text_scale_slider_ = scale_slider;
        text_scale_slider_.ValueChanged(
            [this](
                IInspectable const&,
                Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&) {
                applyTextScale();
            });
        accessibility_panel.Children().Append(scale_panel);
        strong_indicators_toggle_ = ToggleSwitch{};
        strong_indicators_toggle_.Header(box_value(text(IDS_STRONG_SPEAKER_INDICATORS)));
        strong_indicators_toggle_.Toggled(
            [this](IInspectable const&, RoutedEventArgs const&) { rebuildSpeakers(); });
        accessibility_panel.Children().Append(strong_indicators_toggle_);
        auto accessibility_help = trackedText(text(IDS_ACCESSIBILITY_HELP));
        accessibility_help.Opacity(0.72);
        accessibility_panel.Children().Append(accessibility_help);
        settings_panel.Children().Append(
            section(IDS_ACCESSIBILITY_SETTINGS, accessibility_panel, false));

        ready_panel_.Children().Append(section(IDS_SETTINGS, settings_panel, false));
        rebuildBindingSummary();
    }

    void buildModerationSection()
    {
        auto moderation_text = trackedText(text(IDS_MODERATION_AVAILABLE));
        moderation_panel_ = section(IDS_MODERATION, moderation_text, false);
        moderation_panel_.Visibility(Visibility::Collapsed);
        ready_panel_.Children().Append(moderation_panel_);
    }

    auto bindingCombo(std::uint32_t header_id, int selected_index) -> ComboBox
    {
        auto combo = ComboBox{};
        combo.Header(box_value(text(header_id)));
        combo.HorizontalAlignment(HorizontalAlignment::Stretch);
        for (int function_key = 1; function_key <= 12; ++function_key)
        {
            combo.Items().Append(box_value(L"F" + to_hstring(function_key)));
        }
        combo.SelectedIndex(selected_index);
        return combo;
    }

    IAsyncAction connectAsync()
    {
        const auto lifetime = get_strong();
        auto ui_thread = apartment_context{};
        connect_button_.IsEnabled(false);
        error_text_.Visibility(Visibility::Collapsed);
        const auto server_url = server_url_;
        const auto credential = credential_;
        auto session = std::make_shared<ClientSession>([this](SessionEvent event) {
            dispatcher_.TryEnqueue(
                [this, event = std::move(event)]() { handleSessionEvent(event); });
        });

        co_await resume_background();
        auto result = session->connect(server_url, credential);
        co_await ui_thread;

        connect_button_.IsEnabled(true);
        if (!result.successful || !result.membership.has_value())
        {
            error_text_.Text(text(IDS_LOGIN_FAILED) + L" " + to_hstring(result.message));
            error_text_.Visibility(Visibility::Visible);
            co_return;
        }

        session_ = std::move(session);
        credential_.clear();
        membership_ = *result.membership;
        populateConnectedView();
        login_panel_.Visibility(Visibility::Collapsed);
        ready_scroll_.Visibility(Visibility::Visible);
    }

    IAsyncAction disconnectAsync()
    {
        const auto lifetime = get_strong();
        auto ui_thread = apartment_context{};
        auto session = std::move(session_);
        ready_panel_.IsHitTestVisible(false);
        co_await resume_background();
        if (session != nullptr)
        {
            session->disconnect();
        }
        co_await ui_thread;
        ready_panel_.IsHitTestVisible(true);
        ready_scroll_.Visibility(Visibility::Collapsed);
        login_panel_.Visibility(Visibility::Visible);
        error_text_.Visibility(Visibility::Collapsed);
        speakers_.clear();
        rebuildSpeakers();
        membership_.reset();
    }

    IAsyncAction applyAudioSettingsAsync()
    {
        const auto lifetime = get_strong();
        auto ui_thread = apartment_context{};
        if (session_ == nullptr)
        {
            co_return;
        }

        auto config = client::AudioEngineConfig{};
        config.maximum_streams = static_cast<std::size_t>(maximum_streams_slider_.Value());
        config.maximum_streams_per_scope = {
            static_cast<std::size_t>(team_streams_slider_.Value()),
            static_cast<std::size_t>(specialization_streams_slider_.Value()),
            static_cast<std::size_t>(group_streams_slider_.Value())};
        config.team_gain_under_specialization =
            static_cast<float>(team_under_specialization_slider_.Value() / 100.0);
        config.team_gain_under_group = static_cast<float>(team_under_group_slider_.Value() / 100.0);
        config.specialization_gain_under_group =
            static_cast<float>(specialization_under_group_slider_.Value() / 100.0);
        const auto recording_index = recording_combo_.SelectedIndex();
        const auto playout_index = playout_combo_.SelectedIndex();
        const auto session = session_;

        co_await resume_background();
        auto result = session->setAudioEngineConfig(config);
        if (result && recording_index >= 0 &&
            static_cast<std::size_t>(recording_index) < recording_device_ids_.size())
        {
            result = session->selectRecordingDevice(
                recording_device_ids_[static_cast<std::size_t>(recording_index)]);
        }
        if (result && playout_index >= 0 &&
            static_cast<std::size_t>(playout_index) < playout_device_ids_.size())
        {
            result = session->selectPlayoutDevice(
                playout_device_ids_[static_cast<std::size_t>(playout_index)]);
        }
        const auto operation_result = result;
        co_await ui_thread;
        showOperationResult(operation_result, IDS_SETTINGS_APPLIED, IDS_SETTINGS_FAILED);
    }

    void populateConnectedView()
    {
        if (!membership_.has_value() || session_ == nullptr)
        {
            return;
        }
        const auto& membership = *membership_;
        hierarchy_panel_.Children().Clear();
        hierarchy_panel_.Children().Append(
            trackedText(labeledValue(IDS_PLAYER, to_hstring(membership.player_id.value()))));
        hierarchy_panel_.Children().Append(
            trackedText(labeledValue(IDS_GROUP, to_hstring(membership.group_id.value()))));
        hierarchy_panel_.Children().Append(trackedText(
            labeledValue(IDS_SPECIALIZATION, to_hstring(membership.specialization_id.value()))));
        hierarchy_panel_.Children().Append(
            trackedText(labeledValue(IDS_TEAM, to_hstring(membership.team_id.value()))));
        hierarchy_panel_.Children().Append(
            trackedText(labeledValue(IDS_ROLES, rolesText(membership))));
        hierarchy_panel_.Children().Append(
            trackedText(labeledValue(IDS_MEMBERSHIP_VERSION, to_hstring(membership.version))));
        moderation_panel_.Visibility(hasModeratorRole(membership) ? Visibility::Visible
                                                                  : Visibility::Collapsed);
        connection_text_.Text(labeledValue(IDS_CONNECTION, text(IDS_CONNECTION_CONNECTED)));
        send_text_.Text(labeledValue(IDS_SEND, text(IDS_SEND_IDLE)));
        receive_text_.Text(labeledValue(IDS_RECEIVE, text(IDS_RECEIVE_IDLE)));
        status_summary_.Text(text(IDS_STATUS_READY));
        error_status_text_.Text(labeledValue(IDS_ERROR, text(IDS_DIAGNOSTICS_EMPTY)));
        diagnostics_text_.Text(text(IDS_DIAGNOSTICS_EMPTY));
        active_scope_text_.Text(labeledValue(IDS_ACTIVE_SCOPE, text(IDS_SCOPE_IDLE)));
        populateAudioSettings();
        populateInputDevices();
        rebuildBindingSummary();
    }

    void populateAudioSettings()
    {
        recording_combo_.Items().Clear();
        recording_device_ids_.clear();
        const auto recording_devices = session_->recordingDevices();
        for (const auto& device : recording_devices)
        {
            recording_combo_.Items().Append(box_value(to_hstring(device.display_name)));
            recording_device_ids_.push_back(device.id);
        }
        if (recording_devices.empty())
        {
            recording_combo_.Items().Append(box_value(text(IDS_NO_AUDIO_DEVICES)));
            recording_combo_.IsEnabled(false);
        }
        else
        {
            recording_combo_.IsEnabled(true);
            recording_combo_.SelectedIndex(0);
        }

        playout_combo_.Items().Clear();
        playout_device_ids_.clear();
        const auto playout_devices = session_->playoutDevices();
        for (const auto& device : playout_devices)
        {
            playout_combo_.Items().Append(box_value(to_hstring(device.display_name)));
            playout_device_ids_.push_back(device.id);
        }
        if (playout_devices.empty())
        {
            playout_combo_.Items().Append(box_value(text(IDS_NO_AUDIO_DEVICES)));
            playout_combo_.IsEnabled(false);
        }
        else
        {
            playout_combo_.IsEnabled(true);
            playout_combo_.SelectedIndex(0);
        }

        const auto config = session_->audioEngineConfig();
        maximum_streams_slider_.Value(static_cast<double>(config.maximum_streams));
        team_streams_slider_.Value(static_cast<double>(config.maximum_streams_per_scope[0]));
        specialization_streams_slider_.Value(
            static_cast<double>(config.maximum_streams_per_scope[1]));
        group_streams_slider_.Value(static_cast<double>(config.maximum_streams_per_scope[2]));
        team_under_specialization_slider_.Value(
            static_cast<double>(config.team_gain_under_specialization) * 100.0);
        team_under_group_slider_.Value(static_cast<double>(config.team_gain_under_group) * 100.0);
        specialization_under_group_slider_.Value(
            static_cast<double>(config.specialization_gain_under_group) * 100.0);
    }

    void populateInputDevices()
    {
        const auto devices = session_->inputDevices();
        if (devices.empty())
        {
            input_devices_text_.Text(text(IDS_NO_INPUT_DEVICES));
            return;
        }
        auto description = to_hstring(devices.size()) + L" " + text(IDS_INPUT_DEVICE_COUNT);
        for (const auto& device : devices)
        {
            description = description + L"\n• " + to_hstring(device.display_name);
        }
        input_devices_text_.Text(description);
    }

    void applyBindings()
    {
        if (session_ == nullptr)
        {
            return;
        }
        const auto team_key =
            static_cast<std::uint16_t>(VK_F1 + team_binding_combo_.SelectedIndex());
        const auto specialization_key =
            static_cast<std::uint16_t>(VK_F1 + specialization_binding_combo_.SelectedIndex());
        const auto group_key =
            static_cast<std::uint16_t>(VK_F1 + group_binding_combo_.SelectedIndex());
        const std::array bindings{
            keyboardBinding(client::PushToTalkAction::team, team_key),
            keyboardBinding(client::PushToTalkAction::specialization, specialization_key),
            keyboardBinding(client::PushToTalkAction::group, group_key)};
        const auto result = session_->setBindings(bindings);
        if (result)
        {
            status_summary_.Text(text(IDS_BINDINGS_APPLIED));
            rebuildBindingSummary();
            return;
        }
        status_summary_.Text(text(IDS_BINDINGS_FAILED));
        if (!result.errors.empty())
        {
            diagnostics_text_.Text(to_hstring(result.errors.front().message));
        }
    }

    void rebuildBindingSummary()
    {
        binding_summary_panel_.Children().Clear();
        const auto append_binding = [this](std::uint32_t label_id, const ComboBox& combo) {
            const auto selected = combo.SelectedIndex() + 1;
            binding_summary_panel_.Children().Append(
                transientText(labeledValue(label_id, L"F" + to_hstring(selected))));
        };
        append_binding(IDS_BINDING_TEAM, team_binding_combo_);
        append_binding(IDS_BINDING_SPECIALIZATION, specialization_binding_combo_);
        append_binding(IDS_BINDING_GROUP, group_binding_combo_);
    }

    void handleSessionEvent(const SessionEvent& event)
    {
        switch (event.kind)
        {
        case SessionEventKind::connection_state:
            handleConnectionState(event.voice_state);
            break;
        case SessionEventKind::speaker_started:
            if (event.scope.has_value())
            {
                speakers_.insert_or_assign(event.participant_id, SpeakerState{*event.scope});
                rebuildSpeakers();
            }
            break;
        case SessionEventKind::speaker_stopped:
            speakers_.erase(event.participant_id);
            rebuildSpeakers();
            break;
        case SessionEventKind::transmission_started:
            send_text_.Text(labeledValue(IDS_SEND, text(IDS_SEND_ACTIVE)));
            if (event.scope.has_value())
            {
                active_scope_text_.Text(labeledValue(IDS_ACTIVE_SCOPE, scopeText(*event.scope)));
            }
            break;
        case SessionEventKind::transmission_stopped:
            send_text_.Text(labeledValue(IDS_SEND, text(IDS_SEND_IDLE)));
            active_scope_text_.Text(labeledValue(IDS_ACTIVE_SCOPE, text(IDS_SCOPE_IDLE)));
            break;
        case SessionEventKind::membership_updated:
            if (event.membership.has_value())
            {
                membership_ = *event.membership;
                populateConnectedView();
            }
            break;
        case SessionEventKind::error:
            error_status_text_.Text(labeledValue(IDS_ERROR, to_hstring(event.error_code)));
            diagnostics_text_.Text(to_hstring(event.diagnostic));
            status_summary_.Text(text(IDS_VOICE_ERROR_REPORTED));
            break;
        }
    }

    void handleConnectionState(client::VoiceTransportState state)
    {
        switch (state)
        {
        case client::VoiceTransportState::disconnected:
            connection_text_.Text(labeledValue(IDS_CONNECTION, text(IDS_CONNECTION_DISCONNECTED)));
            send_text_.Text(labeledValue(IDS_SEND, text(IDS_SEND_IDLE)));
            active_scope_text_.Text(labeledValue(IDS_ACTIVE_SCOPE, text(IDS_SCOPE_IDLE)));
            break;
        case client::VoiceTransportState::connecting:
            connection_text_.Text(labeledValue(IDS_CONNECTION, text(IDS_CONNECTION_CONNECTING)));
            break;
        case client::VoiceTransportState::connected:
            connection_text_.Text(labeledValue(IDS_CONNECTION, text(IDS_CONNECTION_CONNECTED)));
            break;
        case client::VoiceTransportState::reconnecting:
            connection_text_.Text(labeledValue(IDS_CONNECTION, text(IDS_CONNECTION_RECONNECTING)));
            send_text_.Text(labeledValue(IDS_SEND, text(IDS_SEND_IDLE)));
            active_scope_text_.Text(labeledValue(IDS_ACTIVE_SCOPE, text(IDS_SCOPE_IDLE)));
            break;
        }
    }

    void rebuildSpeakers()
    {
        speaker_panel_.Children().Clear();
        if (speakers_.empty())
        {
            speaker_panel_.Children().Append(transientText(text(IDS_NO_SPEAKERS)));
            receive_text_.Text(labeledValue(IDS_RECEIVE, text(IDS_RECEIVE_IDLE)));
            return;
        }

        receive_text_.Text(labeledValue(IDS_RECEIVE, text(IDS_RECEIVE_ACTIVE)));
        for (const auto& [participant_id, speaker] : speakers_)
        {
            auto speaker_card = StackPanel{};
            speaker_card.Spacing(5.0);
            auto speaker_title =
                transientText(scopeText(speaker.scope) + L" · " + to_hstring(participant_id) +
                                  L" · " + text(IDS_SPEAKING),
                              strong_indicators_toggle_.IsOn() ? 20.0 : 16.0);
            speaker_title.FontWeight(strong_indicators_toggle_.IsOn()
                                         ? Windows::UI::Text::FontWeights::Bold()
                                         : Windows::UI::Text::FontWeights::SemiBold());
            speaker_card.Children().Append(speaker_title);
            auto role = transientText(labeledValue(IDS_ROLES, text(IDS_SPEAKER_ROLE_UNKNOWN)));
            role.Opacity(0.72);
            speaker_card.Children().Append(role);

            auto volume_panel = StackPanel{};
            volume_panel.Spacing(3.0);
            volume_panel.Children().Append(transientText(text(IDS_VOLUME)));
            auto volume = Slider{};
            volume.Minimum(0.0);
            volume.Maximum(100.0);
            volume.Value(speaker.volume);
            volume.StepFrequency(5.0);
            volume.ValueChanged([this, participant_id](IInspectable const& sender,
                                                       Microsoft::UI::Xaml::Controls::Primitives::
                                                           RangeBaseValueChangedEventArgs const&) {
                if (session_ == nullptr)
                {
                    return;
                }
                const auto slider = sender.as<Slider>();
                auto& current = speakers_.at(participant_id);
                current.volume = slider.Value();
                const auto result = session_->setParticipantVolume(
                    participant_id, static_cast<float>(slider.Value() / 100.0));
                if (!result)
                {
                    showOperationResult(result, IDS_SETTINGS_APPLIED, IDS_SETTINGS_FAILED);
                }
            });
            volume_panel.Children().Append(volume);
            speaker_card.Children().Append(volume_panel);

            auto mute = ToggleSwitch{};
            mute.Header(box_value(text(IDS_LOCAL_MUTE)));
            mute.IsOn(speaker.muted);
            mute.Toggled([this, participant_id](IInspectable const& sender,
                                                RoutedEventArgs const&) {
                if (session_ == nullptr)
                {
                    return;
                }
                const auto toggle = sender.as<ToggleSwitch>();
                auto& current = speakers_.at(participant_id);
                current.muted = toggle.IsOn();
                const auto result = session_->setParticipantMuted(participant_id, toggle.IsOn());
                if (!result)
                {
                    showOperationResult(result, IDS_SETTINGS_APPLIED, IDS_SETTINGS_FAILED);
                }
            });
            speaker_card.Children().Append(mute);
            speaker_panel_.Children().Append(speaker_card);
        }
    }

    void applyTextScale()
    {
        const auto factor = text_scale_slider_.Value() / 100.0;
        for (const auto& [block, base_size] : scalable_text_)
        {
            block.FontSize(base_size * factor);
        }
        rebuildBindingSummary();
        rebuildSpeakers();
    }

    void showOperationResult(const client::VoiceTransportResult& result,
                             std::uint32_t success_message_id, std::uint32_t failure_message_id)
    {
        if (result)
        {
            status_summary_.Text(text(success_message_id));
            return;
        }
        status_summary_.Text(text(failure_message_id));
        error_status_text_.Text(labeledValue(IDS_ERROR, text(failure_message_id)));
        diagnostics_text_.Text(to_hstring(result.message));
    }

    Window window_{nullptr};
    Dispatching::DispatcherQueue dispatcher_{nullptr};
    StackPanel login_panel_{nullptr};
    ScrollViewer ready_scroll_{nullptr};
    StackPanel ready_panel_{nullptr};
    Button connect_button_{nullptr};
    TextBlock error_text_{nullptr};
    TextBlock status_summary_{nullptr};
    StackPanel hierarchy_panel_{nullptr};
    TextBlock connection_text_{nullptr};
    TextBlock send_text_{nullptr};
    TextBlock receive_text_{nullptr};
    TextBlock error_status_text_{nullptr};
    StackPanel diagnostics_{nullptr};
    TextBlock diagnostics_text_{nullptr};
    TextBlock active_scope_text_{nullptr};
    StackPanel binding_summary_panel_{nullptr};
    StackPanel speaker_panel_{nullptr};
    ComboBox recording_combo_{nullptr};
    ComboBox playout_combo_{nullptr};
    Slider maximum_streams_slider_{nullptr};
    Slider team_streams_slider_{nullptr};
    Slider specialization_streams_slider_{nullptr};
    Slider group_streams_slider_{nullptr};
    Slider team_under_specialization_slider_{nullptr};
    Slider team_under_group_slider_{nullptr};
    Slider specialization_under_group_slider_{nullptr};
    TextBlock input_devices_text_{nullptr};
    ComboBox team_binding_combo_{nullptr};
    ComboBox specialization_binding_combo_{nullptr};
    ComboBox group_binding_combo_{nullptr};
    Slider text_scale_slider_{nullptr};
    ToggleSwitch strong_indicators_toggle_{nullptr};
    StackPanel moderation_panel_{nullptr};
    IAsyncAction pending_action_{nullptr};
    std::shared_ptr<ClientSession> session_;
    std::optional<client::MembershipView> membership_;
    std::map<std::string, SpeakerState> speakers_;
    std::vector<std::string> recording_device_ids_;
    std::vector<std::string> playout_device_ids_;
    std::vector<std::pair<TextBlock, double>> scalable_text_;
    std::string server_url_{"http://127.0.0.1:8080"};
    std::string credential_;
};
} // namespace hvc::windows_client

auto __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) -> int
{
    try
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        winrt::Microsoft::UI::Xaml::Application::Start(
            [](auto&&) { winrt::make<hvc::windows_client::App>(); });
        return 0;
    }
    catch (const winrt::hresult_error& error)
    {
        MessageBoxW(nullptr, error.message().c_str(), L"Hierarchical Voice Communication",
                    MB_OK | MB_ICONERROR);
        return 1;
    }
    catch (const std::exception& error)
    {
        MessageBoxA(nullptr, error.what(), "Hierarchical Voice Communication",
                    MB_OK | MB_ICONERROR);
        return 1;
    }
}
