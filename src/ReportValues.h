/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/
#ifndef SKYFIRE_MODULE_REPORT_VALUES_H
#define SKYFIRE_MODULE_REPORT_VALUES_H
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace SkyFireAnticheat
{
inline bool ParsePositiveUint32(char const* text, std::uint32_t& result)
{
    result = 0;
    if (!text || !*text)
        return false;
    std::uint64_t value = 0;
    for (char const* c = text; *c; ++c)
    {
        if (*c < '0' || *c > '9')
            return false;
        value = value * 10 + (*c - '0');
        if (value > std::numeric_limits<std::uint32_t>::max())
            return false;
    }
    result = std::uint32_t(value);
    return result != 0;
}

inline std::int32_t CoordinateMilli(float value)
{
    // Integer SQL values are locale-independent; bound conversion before casting.
    double scaled = double(value) * 1000.0;
    if (!std::isfinite(scaled))
        return 0;
    return std::int32_t(std::max(double(std::numeric_limits<std::int32_t>::min()),
        std::min(double(std::numeric_limits<std::int32_t>::max()), scaled)));
}
}
#endif
