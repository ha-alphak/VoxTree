#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstddef>
#include <cstdint>
#include <hvc/client/win_http_transport.hpp>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <windows.h>
#include <winhttp.h>

namespace hvc::client
{
namespace
{
constexpr std::size_t maximum_response_size = static_cast<std::size_t>(1024U) * 1024U;

class InternetHandle final
{
  public:
    InternetHandle() = default;
    explicit InternetHandle(HINTERNET handle) noexcept : handle_(handle)
    {
    }
    ~InternetHandle()
    {
        if (handle_ != nullptr)
        {
            WinHttpCloseHandle(handle_);
        }
    }

    InternetHandle(const InternetHandle&) = delete;
    auto operator=(const InternetHandle&) -> InternetHandle& = delete;
    InternetHandle(InternetHandle&& other) noexcept : handle_(std::exchange(other.handle_, nullptr))
    {
    }
    auto operator=(InternetHandle&& other) noexcept -> InternetHandle&
    {
        if (this != &other)
        {
            if (handle_ != nullptr)
            {
                WinHttpCloseHandle(handle_);
            }
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] auto get() const noexcept -> HINTERNET
    {
        return handle_;
    }

  private:
    HINTERNET handle_{nullptr};
};

[[nodiscard]] auto wide(std::string_view value) -> std::wstring
{
    if (value.empty())
    {
        return {};
    }
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        throw std::invalid_argument{"UTF-8 value is too large"};
    }
    const auto required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                              static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0)
    {
        throw std::invalid_argument{"value is not valid UTF-8"};
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), required) != required)
    {
        throw std::runtime_error{"UTF-8 conversion failed"};
    }
    return result;
}

[[nodiscard]] auto winHttpError(std::string_view operation) -> std::string
{
    return std::string{operation} + " failed with Windows error " + std::to_string(GetLastError());
}

[[nodiscard]] auto queryHeader(HINTERNET request, const wchar_t* name) -> std::string
{
    DWORD size{0};
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, name, nullptr, &size,
                            WINHTTP_NO_HEADER_INDEX) != FALSE ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        return {};
    }
    std::wstring buffer(size / sizeof(wchar_t), L'\0');
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, name, buffer.data(), &size,
                            WINHTTP_NO_HEADER_INDEX) == FALSE)
    {
        return {};
    }
    while (!buffer.empty() && buffer.back() == L'\0')
    {
        buffer.pop_back();
    }
    if (buffer.empty())
    {
        return {};
    }
    const auto required =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, buffer.data(),
                            static_cast<int>(buffer.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0)
    {
        return {};
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, buffer.data(),
                            static_cast<int>(buffer.size()), result.data(), required, nullptr,
                            nullptr) != required)
    {
        return {};
    }
    return result;
}
} // namespace

class WinHttpTransport::Impl final
{
  public:
    Impl(std::string base_url, std::chrono::milliseconds request_timeout)
    {
        if (request_timeout.count() <= 0 ||
            request_timeout.count() > std::numeric_limits<int>::max())
        {
            throw std::invalid_argument{"WinHTTP timeout is outside the supported range"};
        }

        auto url = wide(base_url);
        URL_COMPONENTS components{};
        components.dwStructSize = sizeof(components);
        components.dwSchemeLength = static_cast<DWORD>(-1);
        components.dwHostNameLength = static_cast<DWORD>(-1);
        components.dwUrlPathLength = static_cast<DWORD>(-1);
        if (WinHttpCrackUrl(url.data(), static_cast<DWORD>(url.size()), 0, &components) == FALSE ||
            components.lpszHostName == nullptr || components.dwHostNameLength == 0)
        {
            throw std::invalid_argument{"control-plane base URL is invalid"};
        }
        if (components.nScheme != INTERNET_SCHEME_HTTP &&
            components.nScheme != INTERNET_SCHEME_HTTPS)
        {
            throw std::invalid_argument{"control-plane URL must use HTTP or HTTPS"};
        }
        const std::wstring_view path{components.lpszUrlPath, components.dwUrlPathLength};
        if (!path.empty() && path != L"/")
        {
            throw std::invalid_argument{"control-plane base URL must not contain a path"};
        }
        host_.assign(components.lpszHostName, components.dwHostNameLength);
        port_ = components.nPort;
        secure_ = components.nScheme == INTERNET_SCHEME_HTTPS;

        session_ = InternetHandle{WinHttpOpen(L"HierarchicalVoiceCommunication/0.1",
                                              WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
        if (session_.get() == nullptr)
        {
            throw std::runtime_error{winHttpError("WinHttpOpen")};
        }
        const auto timeout = static_cast<int>(request_timeout.count());
        if (WinHttpSetTimeouts(session_.get(), timeout, timeout, timeout, timeout) == FALSE)
        {
            throw std::runtime_error{winHttpError("WinHttpSetTimeouts")};
        }
        connection_ = InternetHandle{WinHttpConnect(session_.get(), host_.c_str(), port_, 0)};
        if (connection_.get() == nullptr)
        {
            throw std::runtime_error{winHttpError("WinHttpConnect")};
        }
    }

    [[nodiscard]] auto send(const ClientHttpRequest& request) const -> ClientHttpResponse
    {
        try
        {
            return sendChecked(request);
        }
        catch (const std::exception& error)
        {
            return {0, {}, {}, error.what()};
        }
    }

  private:
    [[nodiscard]] auto sendChecked(const ClientHttpRequest& request) const -> ClientHttpResponse
    {
        if (request.method.empty() || request.target.empty() || request.target.front() != '/')
        {
            throw std::invalid_argument{"HTTP request method and absolute target are required"};
        }
        if (request.body.size() > std::numeric_limits<DWORD>::max())
        {
            throw std::invalid_argument{"HTTP request body is too large"};
        }
        const auto method = wide(request.method);
        const auto target = wide(request.target);
        const DWORD flags = secure_ ? WINHTTP_FLAG_SECURE : 0;
        InternetHandle http_request{WinHttpOpenRequest(connection_.get(), method.c_str(),
                                                       target.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags)};
        if (http_request.get() == nullptr)
        {
            throw std::runtime_error{winHttpError("WinHttpOpenRequest")};
        }
        DWORD disabled_features = WINHTTP_DISABLE_REDIRECTS;
        if (WinHttpSetOption(http_request.get(), WINHTTP_OPTION_DISABLE_FEATURE, &disabled_features,
                             sizeof(disabled_features)) == FALSE)
        {
            throw std::runtime_error{winHttpError("WinHttpSetOption")};
        }

        for (const auto& [name, value] : request.headers)
        {
            if (name.find_first_of("\r\n") != std::string::npos ||
                value.find_first_of("\r\n") != std::string::npos)
            {
                throw std::invalid_argument{"HTTP header contains a line break"};
            }
            std::string header_value{name};
            header_value += ": ";
            header_value += value;
            const auto header = wide(header_value);
            if (WinHttpAddRequestHeaders(
                    http_request.get(), header.c_str(), static_cast<DWORD>(header.size()),
                    WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) == FALSE)
            {
                throw std::runtime_error{winHttpError("WinHttpAddRequestHeaders")};
            }
        }
        if (!request.body.empty())
        {
            const auto content_type = wide("content-type: application/json; charset=utf-8");
            if (WinHttpAddRequestHeaders(http_request.get(), content_type.c_str(),
                                         static_cast<DWORD>(content_type.size()),
                                         WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) ==
                FALSE)
            {
                throw std::runtime_error{winHttpError("WinHttpAddRequestHeaders")};
            }
        }

        auto body_data = request.body;
        void* body =
            body_data.empty() ? WINHTTP_NO_REQUEST_DATA : static_cast<void*>(body_data.data());
        const auto body_size = static_cast<DWORD>(body_data.size());
        if (WinHttpSendRequest(http_request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, body,
                               body_size, body_size, 0) == FALSE)
        {
            throw std::runtime_error{winHttpError("WinHttpSendRequest")};
        }
        if (WinHttpReceiveResponse(http_request.get(), nullptr) == FALSE)
        {
            throw std::runtime_error{winHttpError("WinHttpReceiveResponse")};
        }

        DWORD status{0};
        DWORD status_size = sizeof(status);
        if (WinHttpQueryHeaders(http_request.get(),
                                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                                WINHTTP_NO_HEADER_INDEX) == FALSE)
        {
            throw std::runtime_error{winHttpError("WinHttpQueryHeaders")};
        }

        std::string response_body;
        while (true)
        {
            DWORD available{0};
            if (WinHttpQueryDataAvailable(http_request.get(), &available) == FALSE)
            {
                throw std::runtime_error{winHttpError("WinHttpQueryDataAvailable")};
            }
            if (available == 0)
            {
                break;
            }
            if (response_body.size() + available > maximum_response_size)
            {
                throw std::runtime_error{"control-plane response exceeds the client limit"};
            }
            std::vector<char> buffer(available);
            DWORD read{0};
            if (WinHttpReadData(http_request.get(), buffer.data(), available, &read) == FALSE)
            {
                throw std::runtime_error{winHttpError("WinHttpReadData")};
            }
            response_body.append(buffer.data(), read);
        }

        std::map<std::string, std::string, std::less<>> headers;
        const auto capture_header = [&](const wchar_t* wire_name, const char* normalized_name) {
            auto value = queryHeader(http_request.get(), wire_name);
            if (!value.empty())
            {
                headers.emplace(normalized_name, std::move(value));
            }
        };
        capture_header(L"X-HVC-API-Version", "x-hvc-api-version");
        capture_header(L"ETag", "etag");
        capture_header(L"Retry-After", "retry-after");
        return {static_cast<int>(status), std::move(headers), std::move(response_body), {}};
    }

    std::wstring host_;
    INTERNET_PORT port_{0};
    bool secure_{false};
    InternetHandle session_;
    InternetHandle connection_;
};

WinHttpTransport::WinHttpTransport(std::string base_url, std::chrono::milliseconds request_timeout)
    : impl_(std::make_unique<Impl>(std::move(base_url), request_timeout))
{
}

WinHttpTransport::~WinHttpTransport() = default;

auto WinHttpTransport::send(const ClientHttpRequest& request) -> ClientHttpResponse
{
    return impl_->send(request);
}
} // namespace hvc::client
