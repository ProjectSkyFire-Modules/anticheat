/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/
#ifndef SKYFIRE_MODULE_ANTICHEAT_CONFIG_H
#define SKYFIRE_MODULE_ANTICHEAT_CONFIG_H
#include <cstdint>

namespace SkyFireAnticheat
{
// YYYYMMDDNN; increment with configuration changes and update the .conf.dist.
constexpr std::uint32_t ConfigVersion = 2026100601;
enum class ConfigCompatibility { Outdated, Current, Newer };
inline ConfigCompatibility CheckConfigVersion(std::int64_t version)
{
    if (version < ConfigVersion)
        return ConfigCompatibility::Outdated;
    if (version > ConfigVersion)
        return ConfigCompatibility::Newer;
    return ConfigCompatibility::Current;
}
}
#endif
