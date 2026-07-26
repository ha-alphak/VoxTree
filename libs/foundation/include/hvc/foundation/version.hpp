#pragma once

#include <string_view>

/**
 * Provide build and release metadata shared by all HVC components.
 */
namespace hvc::foundation
{

/**
 * Return the configured project version.
 *
 * @returns A view of the immutable semantic version compiled into the binary.
 */
[[nodiscard]] auto version() noexcept -> std::string_view;

} // namespace hvc::foundation
