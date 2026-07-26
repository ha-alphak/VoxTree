#include "application.hpp"

#include "main_window.hpp"

namespace hvc::windows_client
{
void App::OnLaunched(const winrt::Microsoft::UI::Xaml::LaunchActivatedEventArgs&)
{
    main_window_ = std::make_shared<MainWindow>();
    main_window_->show();
}
} // namespace hvc::windows_client
