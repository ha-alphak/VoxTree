#include <hvc/foundation/version.hpp>
#include <hvc/foundation/version_config.hpp>

namespace hvc::foundation
{

auto version() noexcept -> std::string_view
{
    return build::version;
}

} // namespace hvc::foundation
