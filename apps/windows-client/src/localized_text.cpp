#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "localized_text.hpp"

#include <windows.h>

namespace hvc::windows_client
{
auto localizedText(std::uint32_t resource_id) -> winrt::hstring
{
    const wchar_t* text = nullptr;
    const auto length =
        LoadStringW(GetModuleHandleW(nullptr), resource_id, reinterpret_cast<wchar_t*>(&text), 0);
    if (length == 0)
    {
        winrt::throw_last_error();
    }
    return {text, static_cast<std::uint32_t>(length)};
}
} // namespace hvc::windows_client
