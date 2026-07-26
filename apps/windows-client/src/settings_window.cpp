#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "settings_window.hpp"

#include "../resources/resource.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <windows.h>
#undef GetCurrentTime
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/base.h>

namespace hvc::windows_client
{
using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace
{
[[nodiscard]] auto keyboardBinding(client::PushToTalkAction action, std::uint16_t virtual_key)
    -> client::InputBinding
{
    return {action, {{client::InputDeviceKind::keyboard, 0, virtual_key, false, {}}}};
}

[[nodiscard]] auto functionKeyIndex(const std::vector<client::InputBinding>& bindings,
                                    client::PushToTalkAction action) -> int
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
            return static_cast<int>(control.code - VK_F1);
        }
    }
    switch (action)
    {
    case client::PushToTalkAction::team:
        return 8;
    case client::PushToTalkAction::specialization:
        return 9;
    case client::PushToTalkAction::group:
        return 10;
    }
    return 0;
}
} // namespace

SettingsWindow::SettingsWindow(std::shared_ptr<ClientSession> session,
                               presentation::SettingsState settings,
                               StateChangedCallback state_changed)
    : session_(std::move(session)), settings_(std::move(settings)),
      state_changed_(std::move(state_changed))
{
}

void SettingsWindow::show()
{
    if (open_)
    {
        window_.Activate();
        return;
    }
    window_ = Window{};
    window_.Title(text(IDS_SETTINGS_TITLE));
    texts_.setScale(settings_.text_scale_percent);
    buildContent();
    populateControls();
    open_ = true;
    const auto weak = weak_from_this();
    window_.Closed([weak](auto const&, WindowEventArgs const&) {
        if (const auto self = weak.lock())
        {
            self->open_ = false;
            self->session_.reset();
            self->window_ = nullptr;
        }
    });
    window_.Activate();
}

void SettingsWindow::close() noexcept
{
    if (open_ && window_ != nullptr)
    {
        window_.Close();
    }
    session_.reset();
}

auto SettingsWindow::isOpen() const noexcept -> bool
{
    return open_;
}

void SettingsWindow::buildContent()
{
    auto scroll = ScrollViewer{};
    scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    auto root = StackPanel{};
    root.Padding(Thickness{24.0});
    root.Spacing(14.0);
    root.Children().Append(texts_.heading(IDS_SETTINGS_TITLE, 28.0));

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
        labeledSlider(texts_, IDS_MAXIMUM_STREAMS, 1.0, 16.0, 8.0, 1.0);
    maximum_streams_slider_ = maximum_slider;
    audio_panel.Children().Append(maximum_panel);
    auto [team_panel, team_slider] = labeledSlider(texts_, IDS_TEAM_STREAMS, 1.0, 8.0, 5.0, 1.0);
    team_streams_slider_ = team_slider;
    audio_panel.Children().Append(team_panel);
    auto [specialization_panel, specialization_slider] =
        labeledSlider(texts_, IDS_SPECIALIZATION_STREAMS, 1.0, 8.0, 4.0, 1.0);
    specialization_streams_slider_ = specialization_slider;
    audio_panel.Children().Append(specialization_panel);
    auto [group_panel, group_slider] = labeledSlider(texts_, IDS_GROUP_STREAMS, 1.0, 8.0, 2.0, 1.0);
    group_streams_slider_ = group_slider;
    audio_panel.Children().Append(group_panel);
    auto [team_specialization_panel, team_specialization_slider] =
        labeledSlider(texts_, IDS_TEAM_UNDER_SPECIALIZATION, 0.0, 100.0, 50.0, 5.0);
    team_under_specialization_slider_ = team_specialization_slider;
    audio_panel.Children().Append(team_specialization_panel);
    auto [team_group_panel, team_group_slider] =
        labeledSlider(texts_, IDS_TEAM_UNDER_GROUP, 0.0, 100.0, 25.0, 5.0);
    team_under_group_slider_ = team_group_slider;
    audio_panel.Children().Append(team_group_panel);
    auto [specialization_group_panel, specialization_group_slider] =
        labeledSlider(texts_, IDS_SPECIALIZATION_UNDER_GROUP, 0.0, 100.0, 50.0, 5.0);
    specialization_under_group_slider_ = specialization_group_slider;
    audio_panel.Children().Append(specialization_group_panel);

    apply_audio_button_ = Button{};
    apply_audio_button_.Content(box_value(text(IDS_APPLY_AUDIO)));
    const auto weak = weak_from_this();
    apply_audio_button_.Click([weak](auto const&, RoutedEventArgs const&) {
        if (const auto self = weak.lock())
        {
            self->applyAudioAsync();
        }
    });
    audio_panel.Children().Append(apply_audio_button_);
    root.Children().Append(texts_.section(IDS_AUDIO_SETTINGS, audio_panel));

    auto input_panel = StackPanel{};
    input_panel.Spacing(10.0);
    input_devices_text_ = texts_.block(text(IDS_NO_INPUT_DEVICES));
    input_panel.Children().Append(input_devices_text_);
    team_binding_combo_ = bindingCombo(IDS_BINDING_TEAM, client::PushToTalkAction::team);
    specialization_binding_combo_ =
        bindingCombo(IDS_BINDING_SPECIALIZATION, client::PushToTalkAction::specialization);
    group_binding_combo_ = bindingCombo(IDS_BINDING_GROUP, client::PushToTalkAction::group);
    input_panel.Children().Append(team_binding_combo_);
    input_panel.Children().Append(specialization_binding_combo_);
    input_panel.Children().Append(group_binding_combo_);
    apply_bindings_button_ = Button{};
    apply_bindings_button_.Content(box_value(text(IDS_APPLY_BINDINGS)));
    apply_bindings_button_.Click([weak](auto const&, RoutedEventArgs const&) {
        if (const auto self = weak.lock())
        {
            self->applyBindingsAsync();
        }
    });
    input_panel.Children().Append(apply_bindings_button_);
    root.Children().Append(texts_.section(IDS_INPUT_SETTINGS, input_panel));

    auto accessibility_panel = StackPanel{};
    accessibility_panel.Spacing(10.0);
    auto [scale_panel, scale_slider] =
        labeledSlider(texts_, IDS_TEXT_SCALE, 100.0, 200.0,
                      static_cast<double>(settings_.text_scale_percent), 10.0);
    text_scale_slider_ = scale_slider;
    text_scale_slider_.ValueChanged(
        [weak](auto const&,
               Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const&) {
            if (const auto self = weak.lock())
            {
                self->accessibilityChanged();
            }
        });
    accessibility_panel.Children().Append(scale_panel);
    strong_indicators_toggle_ = ToggleSwitch{};
    strong_indicators_toggle_.Header(box_value(text(IDS_STRONG_SPEAKER_INDICATORS)));
    strong_indicators_toggle_.Toggled([weak](auto const&, RoutedEventArgs const&) {
        if (const auto self = weak.lock())
        {
            self->accessibilityChanged();
        }
    });
    accessibility_panel.Children().Append(strong_indicators_toggle_);
    auto accessibility_help = texts_.block(text(IDS_ACCESSIBILITY_HELP));
    accessibility_help.Opacity(0.72);
    accessibility_panel.Children().Append(accessibility_help);
    root.Children().Append(texts_.section(IDS_ACCESSIBILITY_SETTINGS, accessibility_panel));

    status_text_ = texts_.block({});
    status_text_.Visibility(Visibility::Collapsed);
    root.Children().Append(status_text_);
    auto close_button = Button{};
    close_button.Content(box_value(text(IDS_CLOSE)));
    close_button.HorizontalAlignment(HorizontalAlignment::Left);
    close_button.Click([weak](auto const&, RoutedEventArgs const&) {
        if (const auto self = weak.lock())
        {
            self->close();
        }
    });
    root.Children().Append(close_button);
    scroll.Content(root);
    window_.Content(scroll);
}

void SettingsWindow::populateControls()
{
    for (const auto& device : settings_.recording_devices)
    {
        recording_combo_.Items().Append(box_value(to_hstring(device.display_name)));
    }
    if (settings_.recording_devices.empty())
    {
        recording_combo_.Items().Append(box_value(text(IDS_NO_AUDIO_DEVICES)));
        recording_combo_.IsEnabled(false);
    }
    else
    {
        const auto selected = std::ranges::find(
            settings_.recording_devices, settings_.recording_device_id, &client::AudioDevice::id);
        recording_combo_.SelectedIndex(
            selected == settings_.recording_devices.end()
                ? 0
                : static_cast<int>(std::distance(settings_.recording_devices.begin(), selected)));
    }

    for (const auto& device : settings_.playout_devices)
    {
        playout_combo_.Items().Append(box_value(to_hstring(device.display_name)));
    }
    if (settings_.playout_devices.empty())
    {
        playout_combo_.Items().Append(box_value(text(IDS_NO_AUDIO_DEVICES)));
        playout_combo_.IsEnabled(false);
    }
    else
    {
        const auto selected = std::ranges::find(
            settings_.playout_devices, settings_.playout_device_id, &client::AudioDevice::id);
        playout_combo_.SelectedIndex(
            selected == settings_.playout_devices.end()
                ? 0
                : static_cast<int>(std::distance(settings_.playout_devices.begin(), selected)));
    }

    const auto& config = settings_.audio;
    maximum_streams_slider_.Value(static_cast<double>(config.maximum_streams));
    team_streams_slider_.Value(static_cast<double>(config.maximum_streams_per_scope[0]));
    specialization_streams_slider_.Value(static_cast<double>(config.maximum_streams_per_scope[1]));
    group_streams_slider_.Value(static_cast<double>(config.maximum_streams_per_scope[2]));
    team_under_specialization_slider_.Value(
        static_cast<double>(config.team_gain_under_specialization) * 100.0);
    team_under_group_slider_.Value(static_cast<double>(config.team_gain_under_group) * 100.0);
    specialization_under_group_slider_.Value(
        static_cast<double>(config.specialization_gain_under_group) * 100.0);

    if (settings_.input_devices.empty())
    {
        input_devices_text_.Text(text(IDS_NO_INPUT_DEVICES));
    }
    else
    {
        auto description =
            to_hstring(settings_.input_devices.size()) + L" " + text(IDS_INPUT_DEVICE_COUNT);
        for (const auto& device : settings_.input_devices)
        {
            description = description + L"\n• " + to_hstring(device.display_name);
        }
        input_devices_text_.Text(description);
    }
    strong_indicators_toggle_.IsOn(settings_.strong_speaker_indicators);
}

auto SettingsWindow::bindingCombo(std::uint32_t header_id, client::PushToTalkAction action)
    -> ComboBox
{
    auto combo = ComboBox{};
    combo.Header(box_value(text(header_id)));
    combo.HorizontalAlignment(HorizontalAlignment::Stretch);
    for (int function_key = 1; function_key <= 12; ++function_key)
    {
        combo.Items().Append(box_value(L"F" + to_hstring(function_key)));
    }
    combo.SelectedIndex(functionKeyIndex(settings_.bindings, action));
    return combo;
}

auto SettingsWindow::selectedBindings() const -> std::vector<client::InputBinding>
{
    return {
        keyboardBinding(client::PushToTalkAction::team,
                        static_cast<std::uint16_t>(VK_F1 + team_binding_combo_.SelectedIndex())),
        keyboardBinding(
            client::PushToTalkAction::specialization,
            static_cast<std::uint16_t>(VK_F1 + specialization_binding_combo_.SelectedIndex())),
        keyboardBinding(client::PushToTalkAction::group,
                        static_cast<std::uint16_t>(VK_F1 + group_binding_combo_.SelectedIndex()))};
}

void SettingsWindow::accessibilityChanged()
{
    if (text_scale_slider_ == nullptr || strong_indicators_toggle_ == nullptr)
    {
        return;
    }
    settings_.text_scale_percent = static_cast<std::uint16_t>(text_scale_slider_.Value());
    settings_.strong_speaker_indicators = strong_indicators_toggle_.IsOn();
    texts_.setScale(settings_.text_scale_percent);
    if (state_changed_)
    {
        state_changed_(settings_);
    }
}

fire_and_forget SettingsWindow::applyAudioAsync()
{
    const auto lifetime = shared_from_this();
    auto ui_thread = apartment_context{};
    if (session_ == nullptr)
    {
        co_return;
    }
    setApplying(true);
    auto candidate = settings_;
    candidate.audio.maximum_streams = static_cast<std::size_t>(maximum_streams_slider_.Value());
    candidate.audio.maximum_streams_per_scope = {
        static_cast<std::size_t>(team_streams_slider_.Value()),
        static_cast<std::size_t>(specialization_streams_slider_.Value()),
        static_cast<std::size_t>(group_streams_slider_.Value())};
    candidate.audio.team_gain_under_specialization =
        static_cast<float>(team_under_specialization_slider_.Value() / 100.0);
    candidate.audio.team_gain_under_group =
        static_cast<float>(team_under_group_slider_.Value() / 100.0);
    candidate.audio.specialization_gain_under_group =
        static_cast<float>(specialization_under_group_slider_.Value() / 100.0);
    const auto recording_index = recording_combo_.SelectedIndex();
    const auto playout_index = playout_combo_.SelectedIndex();
    if (recording_index >= 0 &&
        static_cast<std::size_t>(recording_index) < candidate.recording_devices.size())
    {
        candidate.recording_device_id =
            candidate.recording_devices[static_cast<std::size_t>(recording_index)].id;
    }
    if (playout_index >= 0 &&
        static_cast<std::size_t>(playout_index) < candidate.playout_devices.size())
    {
        candidate.playout_device_id =
            candidate.playout_devices[static_cast<std::size_t>(playout_index)].id;
    }
    const auto session = session_;

    co_await resume_background();
    auto result = session->setAudioEngineConfig(candidate.audio);
    if (result && !candidate.recording_device_id.empty())
    {
        result = session->selectRecordingDevice(candidate.recording_device_id);
    }
    if (result && !candidate.playout_device_id.empty())
    {
        result = session->selectPlayoutDevice(candidate.playout_device_id);
    }
    co_await ui_thread;
    if (!open_)
    {
        co_return;
    }
    setApplying(false);
    showAudioResult(result);
    if (result)
    {
        settings_ = std::move(candidate);
        settings_.apply_phase = presentation::OperationPhase::succeeded;
        if (state_changed_)
        {
            state_changed_(settings_);
        }
    }
    else
    {
        settings_.apply_phase = presentation::OperationPhase::failed;
    }
}

fire_and_forget SettingsWindow::applyBindingsAsync()
{
    const auto lifetime = shared_from_this();
    auto ui_thread = apartment_context{};
    if (session_ == nullptr)
    {
        co_return;
    }
    setApplying(true);
    auto bindings = selectedBindings();
    const auto session = session_;
    co_await resume_background();
    const auto result = session->setBindings(bindings);
    co_await ui_thread;
    if (!open_)
    {
        co_return;
    }
    setApplying(false);
    if (result)
    {
        settings_.bindings = std::move(bindings);
        settings_.apply_phase = presentation::OperationPhase::succeeded;
        status_text_.Text(text(IDS_BINDINGS_APPLIED));
        if (state_changed_)
        {
            state_changed_(settings_);
        }
    }
    else
    {
        settings_.apply_phase = presentation::OperationPhase::failed;
        status_text_.Text(text(IDS_BINDINGS_FAILED));
        if (!result.errors.empty())
        {
            status_text_.Text(status_text_.Text() + L"\n" +
                              to_hstring(result.errors.front().message));
        }
    }
    status_text_.Visibility(Visibility::Visible);
}

void SettingsWindow::showAudioResult(const client::VoiceTransportResult& result)
{
    status_text_.Text(result ? text(IDS_SETTINGS_APPLIED)
                             : text(IDS_SETTINGS_FAILED) + L"\n" + to_hstring(result.message));
    status_text_.Visibility(Visibility::Visible);
}

void SettingsWindow::setApplying(bool applying)
{
    apply_audio_button_.IsEnabled(!applying);
    apply_bindings_button_.IsEnabled(!applying);
    status_text_.Text(text(IDS_OPERATION_PENDING));
    status_text_.Visibility(applying ? Visibility::Visible : Visibility::Collapsed);
    settings_.apply_phase =
        applying ? presentation::OperationPhase::pending : presentation::OperationPhase::idle;
}
} // namespace hvc::windows_client
