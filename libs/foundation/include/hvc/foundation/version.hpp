#pragma once

#include <string_view>

namespace hvc::foundation
{

[[nodiscard]] auto version() noexcept -> std::string_view;

} // namespace hvc::foundation
