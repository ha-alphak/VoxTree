#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "application.hpp"

#include <exception>
#include <windows.h>
#undef GetCurrentTime
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/base.h>

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
