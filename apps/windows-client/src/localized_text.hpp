#pragma once

#include <cstdint>
#include <winrt/base.h>

/// Provide localized Win32 string-table resources to the WinUI shell.
namespace hvc::windows_client
{
/**
 * Load one string using the current Windows user-interface language.
 *
 * @param resource_id Identifier declared by the client resource table.
 * @returns Localized text.
 * @throws winrt::hresult_error if the resource is missing.
 */
[[nodiscard]] auto localizedText(std::uint32_t resource_id) -> winrt::hstring;
} // namespace hvc::windows_client
