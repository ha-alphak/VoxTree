#pragma once

#include <memory>
#include <winrt/Microsoft.UI.Xaml.h>

namespace hvc::windows_client
{
class MainWindow;

/// Coordinate application launch and ownership of the main window.
class App : public winrt::Microsoft::UI::Xaml::ApplicationT<App>
{
  public:
    /**
     * Create and activate the main window.
     *
     * @param args Platform launch arguments.
     */
    void OnLaunched(const winrt::Microsoft::UI::Xaml::LaunchActivatedEventArgs& args);

  private:
    std::shared_ptr<MainWindow> main_window_;
};
} // namespace hvc::windows_client
