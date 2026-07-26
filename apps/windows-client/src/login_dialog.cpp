#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "login_dialog.hpp"

#include <array>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <winrt/base.h>

namespace hvc::windows_client
{
namespace
{
constexpr auto dialog_class_name = L"HvcCredentialDialog";
constexpr auto server_control_id = 1001;
constexpr auto credential_control_id = 1002;
constexpr auto connect_control_id = IDOK;
constexpr auto cancel_control_id = IDCANCEL;

struct DialogState final
{
    const LoginDialogText* labels{nullptr};
    std::wstring initial_server;
    HWND server_edit{nullptr};
    HWND credential_edit{nullptr};
    bool accepted{false};
    std::wstring server_url;
    std::wstring credential;
};

void useDialogFont(HWND control)
{
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
                 TRUE);
}

[[nodiscard]] auto createControl(const wchar_t* control_class, const wchar_t* content, DWORD style,
                                 int horizontal, int vertical, int width, int height, HWND parent,
                                 int identifier) -> HWND
{
    const auto control = CreateWindowExW(0, control_class, content, WS_CHILD | WS_VISIBLE | style,
                                         horizontal, vertical, width, height, parent,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(identifier)),
                                         GetModuleHandleW(nullptr), nullptr);
    if (control == nullptr)
    {
        winrt::throw_last_error();
    }
    useDialogFont(control);
    return control;
}

[[nodiscard]] auto controlText(HWND control) -> std::wstring
{
    const auto length = GetWindowTextLengthW(control);
    std::vector<wchar_t> buffer(static_cast<std::size_t>(length) + 1U);
    const auto copied = GetWindowTextW(control, buffer.data(), static_cast<int>(buffer.size()));
    if (copied < 0)
    {
        winrt::throw_last_error();
    }
    return {buffer.data(), static_cast<std::size_t>(copied)};
}

auto CALLBACK dialogWindowProcedure(HWND window, UINT message, WPARAM parameter, LPARAM data)
    -> LRESULT
{
    auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(data);
        state = static_cast<DialogState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message)
    {
    case WM_CREATE: {
        static_cast<void>(createControl(L"STATIC", state->labels->server_label.c_str(), 0, 20, 20,
                                        470, 20, window, -1));
        state->server_edit = createControl(L"EDIT", state->initial_server.c_str(),
                                           WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 20, 45, 470, 25,
                                           window, server_control_id);
        static_cast<void>(createControl(L"STATIC", state->labels->credential_label.c_str(), 0, 20,
                                        85, 470, 20, window, -1));
        state->credential_edit =
            createControl(L"EDIT", L"", WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD, 20,
                          110, 470, 25, window, credential_control_id);
        static_cast<void>(createControl(L"BUTTON", state->labels->connect_label.c_str(),
                                        WS_TABSTOP | BS_DEFPUSHBUTTON, 290, 155, 95, 30, window,
                                        connect_control_id));
        static_cast<void>(createControl(L"BUTTON", state->labels->cancel_label.c_str(),
                                        WS_TABSTOP | BS_PUSHBUTTON, 395, 155, 95, 30, window,
                                        cancel_control_id));
        SetFocus(state->credential_edit);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(parameter))
        {
        case connect_control_id:
            state->server_url = controlText(state->server_edit);
            state->credential = controlText(state->credential_edit);
            state->accepted = true;
            DestroyWindow(window);
            return 0;
        case cancel_control_id:
            DestroyWindow(window);
            return 0;
        default:
            break;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, parameter, data);
}

void ensureDialogClassRegistered()
{
    static const auto registered = [] {
        WNDCLASSW window_class{};
        window_class.lpfnWndProc = dialogWindowProcedure;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        window_class.lpszClassName = dialog_class_name;
        const auto atom = RegisterClassW(&window_class);
        if (atom == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            winrt::throw_last_error();
        }
        return true;
    }();
    static_cast<void>(registered);
}

void centerOverOwner(HWND dialog, HWND owner)
{
    RECT owner_bounds{};
    RECT dialog_bounds{};
    if (GetWindowRect(owner, &owner_bounds) == FALSE ||
        GetWindowRect(dialog, &dialog_bounds) == FALSE)
    {
        return;
    }
    const auto width = dialog_bounds.right - dialog_bounds.left;
    const auto height = dialog_bounds.bottom - dialog_bounds.top;
    const auto horizontal =
        owner_bounds.left + ((owner_bounds.right - owner_bounds.left - width) / 2);
    const auto vertical =
        owner_bounds.top + ((owner_bounds.bottom - owner_bounds.top - height) / 2);
    SetWindowPos(dialog, nullptr, horizontal, vertical, 0, 0,
                 SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER);
}
} // namespace

auto showLoginDialog(HWND owner, const LoginDialogText& labels, const std::string& initial_server)
    -> LoginDialogResult
{
    ensureDialogClassRegistered();
    DialogState state{&labels, winrt::to_hstring(initial_server).c_str()};
    const auto dialog =
        CreateWindowExW(WS_EX_DLGMODALFRAME, dialog_class_name, labels.title.c_str(),
                        WS_CAPTION | WS_SYSMENU | WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, 530, 235,
                        owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (dialog == nullptr)
    {
        winrt::throw_last_error();
    }

    centerOverOwner(dialog, owner);
    EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);

    MSG message{};
    while (IsWindow(dialog) != FALSE)
    {
        const auto result = GetMessageW(&message, nullptr, 0, 0);
        if (result == -1)
        {
            EnableWindow(owner, TRUE);
            winrt::throw_last_error();
        }
        if (result == 0)
        {
            break;
        }
        if (IsDialogMessageW(dialog, &message) == FALSE)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);

    return {state.accepted, winrt::to_string(state.server_url), winrt::to_string(state.credential)};
}
} // namespace hvc::windows_client
