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
#include <string>

namespace SkyFireAnticheat
{
// Hex SQL literals avoid escaping through a shared MySQL connection on map threads.
inline std::string HexEvidence(std::string const& evidence)
{
    static char const digits[] = "0123456789abcdef";
    std::string hex;
    std::size_t count = std::min(evidence.size(), std::size_t(512));
    hex.reserve(count * 2);
    for (std::size_t i = 0; i < count; ++i)
    {
        unsigned char value = static_cast<unsigned char>(evidence[i]);
        hex += digits[value >> 4];
        hex += digits[value & 15];
    }
    return hex;
}

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
