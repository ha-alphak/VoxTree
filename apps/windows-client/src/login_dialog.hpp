#pragma once

#include <string>
#include <windows.h>

/// Assemble the Windows UI shell and its client-side service graph.
namespace hvc::windows_client
{
/// Hold localized labels used by the native credential-entry dialog.
struct LoginDialogText final
{
    /// Dialog window title.
    std::wstring title;
    /// Server-address field label.
    std::wstring server_label;
    /// Credential field label.
    std::wstring credential_label;
    /// Confirmation-button label.
    std::wstring connect_label;
    /// Cancellation-button label.
    std::wstring cancel_label;
};

/// Hold user input returned by the credential-entry dialog.
struct LoginDialogResult final
{
    /// Whether the user confirmed the dialog.
    bool accepted{false};
    /// UTF-8 control-plane base URL.
    std::string server_url;
    /// UTF-8 external credential.
    std::string credential;
};

/**
 * Collect server and credential text in a password-masked native dialog.
 *
 * The modal dialog keeps the credential outside the WinUI visual tree. Its
 * controls use the system dialog font, tab navigation, and standard Windows
 * accessibility semantics.
 *
 * @param owner Parent application window.
 * @param labels Localized static dialog labels.
 * @param initial_server Server URL prefilled when the dialog opens.
 * @returns Confirmed UTF-8 values, or `accepted=false` after cancellation.
 */
[[nodiscard]] auto showLoginDialog(HWND owner, const LoginDialogText& labels,
                                   const std::string& initial_server) -> LoginDialogResult;
} // namespace hvc::windows_client
