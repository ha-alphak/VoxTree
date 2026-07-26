#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "client_session.hpp"
#include "diagnostics_window.hpp"
#include "settings_window.hpp"
#include "view_helpers.hpp"

#include <cstdint>
#include <hvc/presentation/desktop_model.hpp>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <windows.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/base.h>

namespace hvc::windows_client
{
/**
 * Coordinate the primary window, presentation model, and one client session.
 *
 * The coordinator and presentation model are confined to the WinUI thread.
 * Session callbacks are generation-tagged and queued through `DispatcherQueue`.
 * A stale callback can therefore neither access a destroyed window nor mutate
 * a newer authenticated session.
 */
class MainWindow final : public std::enable_shared_from_this<MainWindow>
{
  public:
    /// Construct a closed, signed-out main window.
    MainWindow() = default;
    /// Close child windows and release the active session.
    ~MainWindow();

    /// Build and activate the primary window.
    void show();

  private:
    struct PendingParticipantUpdate final
    {
        std::optional<float> volume;
        std::optional<bool> muted;
    };

    void buildLoginView();
    void buildReadyView();
    void buildTitleBar();
    void buildSidebar();
    void buildChannelView();
    void buildVoiceDock();
    void selectScope(domain::VoiceScope scope);
    [[nodiscard]] auto windowHandle() const -> HWND;
    void requestConnect();
    winrt::fire_and_forget connectAsync(std::string server_url, std::string credential);
    winrt::fire_and_forget disconnectAsync();
    winrt::fire_and_forget shutdownSessionAsync(std::shared_ptr<ClientSession> session);
    void handleSessionEvent(std::uint64_t generation, SessionEvent event);
    void openSettings();
    void openDiagnostics();
    void settingsChanged(const presentation::SettingsState& settings);
    void render();
    void renderHierarchy();
    void renderStatus();
    void rebuildBindingSummary();
    void rebuildSpeakers();
    void updateScopeButtonStyles();
    void updatePttCardStyles();
    void applyTextScale();
    void queueParticipantVolume(const std::string& participant_id, float volume);
    void queueParticipantMuted(const std::string& participant_id, bool muted);
    void startParticipantUpdateTimer();
    winrt::fire_and_forget applyParticipantUpdatesAsync();
    void showOperationFailure(const client::VoiceTransportResult& result);
    [[nodiscard]] static auto settingsSnapshot(const ClientSession& session)
        -> presentation::SettingsState;

    winrt::Microsoft::UI::Xaml::Window window_{nullptr};
    winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher_{nullptr};
    winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer participant_update_timer_{nullptr};
    ViewTextRegistry texts_;
    winrt::Microsoft::UI::Xaml::Controls::Grid root_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Border login_view_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::StackPanel login_panel_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Grid ready_root_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Grid ready_body_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Button connect_button_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock login_error_text_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Border connection_badge_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock connection_badge_text_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock identity_name_text_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock identity_detail_text_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::StackPanel hierarchy_panel_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Button group_button_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Button specialization_button_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Button team_button_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock group_id_text_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock specialization_id_text_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock team_id_text_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::SymbolIcon channel_icon_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock channel_breadcrumb_text_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock channel_title_text_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock channel_description_text_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock participant_count_text_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Border error_banner_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock connection_text_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock send_text_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock receive_text_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock error_status_text_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock active_scope_text_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::StackPanel binding_summary_panel_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Border team_ptt_card_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Border specialization_ptt_card_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Border group_ptt_card_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock team_binding_text_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock specialization_binding_text_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::TextBlock group_binding_text_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::StackPanel speaker_panel_{nullptr};
    winrt::Microsoft::UI::Xaml::Controls::Border moderation_panel_{nullptr};
    std::shared_ptr<ClientSession> session_;
    std::shared_ptr<SettingsWindow> settings_window_;
    std::shared_ptr<DiagnosticsWindow> diagnostics_window_;
    presentation::DesktopModel model_;
    std::map<std::string, PendingParticipantUpdate, std::less<>> pending_participant_updates_;
    std::string server_url_{"http://127.0.0.1:8080"};
    std::uint64_t session_generation_{0};
    bool participant_updates_running_{false};
    bool closing_{false};
};
} // namespace hvc::windows_client
