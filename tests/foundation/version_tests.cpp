#include <cstdio>
#include <hvc/foundation/version.hpp>
#include <string_view>

auto main() noexcept -> int
{
    constexpr std::string_view expected_version{"0.1.0"};
    const std::string_view actual_version = hvc::foundation::version();

    if (actual_version != expected_version)
    {
        std::fputs("The generated project version does not match the expected version.\n", stderr);
        return 1;
    }

    return 0;
}
