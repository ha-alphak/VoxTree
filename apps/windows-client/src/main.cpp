#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "client_session.hpp"

#include <exception>
#include <memory>
#include <string>
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

class App : public ApplicationT<App>
{
  public:
    void OnLaunched(LaunchActivatedEventArgs const&)
    {
        window_ = Window{};
        window_.Title(L"Hierarchical Voice Communication");
        dispatcher_ = window_.DispatcherQueue();

        auto root = Grid{};
        root.Padding(Thickness{32.0});
        root.MaxWidth(760.0);
        root.HorizontalAlignment(HorizontalAlignment::Stretch);
        root.VerticalAlignment(VerticalAlignment::Stretch);

        login_panel_ = StackPanel{};
        login_panel_.Spacing(16.0);
        login_panel_.VerticalAlignment(VerticalAlignment::Center);

        auto title = TextBlock{};
        title.Text(L"Hierarchical Voice Communication");
        title.FontSize(32.0);
        title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        login_panel_.Children().Append(title);

        auto introduction = TextBlock{};
        introduction.Text(
            L"Mit der Control Plane anmelden und die autorisierten Voice-Räume verbinden.");
        introduction.TextWrapping(TextWrapping::Wrap);
        introduction.Opacity(0.72);
        login_panel_.Children().Append(introduction);

        server_box_ = TextBox{};
        server_box_.Header(box_value(L"Server"));
        server_box_.Text(L"http://127.0.0.1:8080");
        server_box_.PlaceholderText(L"https://voice.example.org");
        login_panel_.Children().Append(server_box_);

        credential_box_ = PasswordBox{};
        credential_box_.Header(box_value(L"Anmelde-Credential"));
        credential_box_.PlaceholderText(L"Bootstrap- oder Provider-Credential");
        login_panel_.Children().Append(credential_box_);

        connect_button_ = Button{};
        connect_button_.Content(box_value(L"Verbinden"));
        connect_button_.HorizontalAlignment(HorizontalAlignment::Left);
        connect_button_.Click([this](IInspectable const&, RoutedEventArgs const&) {
            pending_action_ = connectAsync();
        });
        login_panel_.Children().Append(connect_button_);

        error_text_ = TextBlock{};
        error_text_.TextWrapping(TextWrapping::Wrap);
        error_text_.Visibility(Visibility::Collapsed);
        login_panel_.Children().Append(error_text_);

        ready_panel_ = StackPanel{};
        ready_panel_.Spacing(14.0);
        ready_panel_.VerticalAlignment(VerticalAlignment::Center);
        ready_panel_.Visibility(Visibility::Collapsed);

        auto ready_title = TextBlock{};
        ready_title.Text(L"Verbunden und bereit");
        ready_title.FontSize(30.0);
        ready_title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        ready_panel_.Children().Append(ready_title);

        membership_text_ = TextBlock{};
        membership_text_.TextWrapping(TextWrapping::Wrap);
        ready_panel_.Children().Append(membership_text_);

        auto bindings = TextBlock{};
        bindings.Text(L"Push-to-Talk:  Team F9   ·   Specialization F10   ·   Group F11");
        bindings.TextWrapping(TextWrapping::Wrap);
        bindings.Opacity(0.72);
        ready_panel_.Children().Append(bindings);

        status_text_ = TextBlock{};
        status_text_.Text(L"Bereit.");
        status_text_.TextWrapping(TextWrapping::Wrap);
        ready_panel_.Children().Append(status_text_);

        auto disconnect_button = Button{};
        disconnect_button.Content(box_value(L"Trennen"));
        disconnect_button.HorizontalAlignment(HorizontalAlignment::Left);
        disconnect_button.Click([this](IInspectable const&, RoutedEventArgs const&) {
            pending_action_ = disconnectAsync();
        });
        ready_panel_.Children().Append(disconnect_button);

        root.Children().Append(login_panel_);
        root.Children().Append(ready_panel_);
        window_.Content(root);
        window_.Activate();
    }

  private:
    IAsyncAction connectAsync()
    {
        const auto lifetime = get_strong();
        auto ui_thread = apartment_context{};
        connect_button_.IsEnabled(false);
        error_text_.Visibility(Visibility::Collapsed);
        const auto server_url = to_string(server_box_.Text());
        const auto credential = to_string(credential_box_.Password());
        auto session = std::make_shared<ClientSession>([this](std::string message) {
            const auto status = to_hstring(message);
            dispatcher_.TryEnqueue([this, status]() {
                if (status_text_ != nullptr)
                {
                    status_text_.Text(status);
                }
            });
        });

        co_await resume_background();
        auto result = session->connect(server_url, credential);
        co_await ui_thread;

        connect_button_.IsEnabled(true);
        if (!result.successful || !result.membership.has_value())
        {
            error_text_.Text(to_hstring(result.message));
            error_text_.Visibility(Visibility::Visible);
            co_return;
        }

        session_ = std::move(session);
        credential_box_.Password(L"");
        const auto& membership = *result.membership;
        auto description = std::string{"Spieler: "};
        description += membership.player_id.value();
        description += "\nGruppe: ";
        description += membership.group_id.value();
        description += "\nSpecialization: ";
        description += membership.specialization_id.value();
        description += "\nTeam: ";
        description += membership.team_id.value();
        description += "\nMembership-Version: ";
        description += std::to_string(membership.version);
        membership_text_.Text(to_hstring(description));
        status_text_.Text(L"Control Plane, Voice-Transport und Raw Input sind bereit.");
        login_panel_.Visibility(Visibility::Collapsed);
        ready_panel_.Visibility(Visibility::Visible);
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
        ready_panel_.Visibility(Visibility::Collapsed);
        login_panel_.Visibility(Visibility::Visible);
        error_text_.Visibility(Visibility::Collapsed);
    }

    Window window_{nullptr};
    Dispatching::DispatcherQueue dispatcher_{nullptr};
    StackPanel login_panel_{nullptr};
    StackPanel ready_panel_{nullptr};
    TextBox server_box_{nullptr};
    PasswordBox credential_box_{nullptr};
    Button connect_button_{nullptr};
    TextBlock error_text_{nullptr};
    TextBlock membership_text_{nullptr};
    TextBlock status_text_{nullptr};
    IAsyncAction pending_action_{nullptr};
    std::shared_ptr<ClientSession> session_;
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
