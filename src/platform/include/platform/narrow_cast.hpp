// Copyright (c) 2026 Lorenzo Caprari
// SPDX-License-Identifier: Apache-2.0

#ifndef PLATFORM_NARROW_CAST_HPP
#define PLATFORM_NARROW_CAST_HPP

#include <stdexcept>
#include <utility> // std::in_range

namespace nesso::platform
{

/// Convert From to To when the value is in range for To. Throws std::logic_error
/// instead of wrapping when it is not.
template <typename To, typename From> [[nodiscard]] constexpr To narrowCast(From value)
{
    if (!std::in_range<To>(value))
    {
        throw std::logic_error("narrowCast out of range");
    }
    return static_cast<To>(value);
}

} // namespace nesso::platform

#endif
