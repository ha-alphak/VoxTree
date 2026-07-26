#pragma once

#include "client_session.hpp"
#include "view_helpers.hpp"

#include <functional>
#include <hvc/presentation/desktop_model.hpp>
#include <memory>
#include <string>
#include <vector>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace hvc::windows_client
{
/**
 * Edit session-scoped settings in an independent non-modal window.
 *
 * Potentially blocking audio and adapter operations run on a worker thread.
 * The window holds a shared session reference only while it is open or an
 * already-started apply operation is completing.
 */
class SettingsWindow final : public std::enable_shared_from_this<SettingsWindow>
{
  public:
    /// Receive a confirmed settings snapshot on the UI thread.
    using StateChangedCallback = std::function<void(const presentation::SettingsState&)>;

    /**
     * Construct a settings window for one connected session.
     *
     * @param session Connected session shared with the main-window coordinator.
     * @param settings Initial settings snapshot.
     * @param state_changed Callback for confirmed or immediate local changes.
     */
    SettingsWindow(std::shared_ptr<ClientSession> session, presentation::SettingsState settings,
                   StateChangedCallback state_changed);

    /// Create or activate the non-modal window.
    void show();
    /// Close the window and release its session reference.
    void close() noexcept;
    /**
     * Return whether a native window is currently open.
     *
     * @returns `true` after activation and before the close event.
     */
    [[nodiscard]] auto isOpen() const noexcept -> bool;

  private:
    void buildContent();
    void populateControls();
    [[nodiscard]] auto bindingCombo(std::uint32_t header_id, client::PushToTalkAction action)
        -> winrt::Microsoft::UI::Xaml::Controls::ComboBox;
    [[nodiscard]] auto selectedBindings() const -> std::vector<client::InputBinding>;
    void accessibilityChanged();
    winrt::fire_and_forget applyAudioAsync();
    winrt::fire_and_forget applyBindingsAsync();
    void showAudioResult(const client::VoiceTransportResult& result);
    void setApplying(bool applying);

    std::shared_ptr<ClientSession> session_;
    presentation::SettingsState settings_;
    StateChangedCallback state_changed_;
    winrt::Microsoft::UI::Xaml::Window window_{nullptr};
    ViewTextRegistry texts_;
    winrt::Microsoft::UI::Xaml::Controls::ComboBox recording_combo_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ComboBox playout_combo_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Slider maximum_streams_slider_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Slider team_streams_slider_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Slider specialization_streams_slider_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Slider group_streams_slider_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Slider team_under_specialization_slider_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Slider team_under_group_slider_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Slider specialization_under_group_slider_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock input_devices_text_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ComboBox team_binding_combo_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ComboBox specialization_binding_combo_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ComboBox group_binding_combo_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Slider text_scale_slider_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch strong_indicators_toggle_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock status_text_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Button apply_audio_button_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Button apply_bindings_button_{nullptr};
    bool open_{false};
};
} // namespace hvc::windows_client
