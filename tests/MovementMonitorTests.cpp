/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/
#include "../src/MovementMonitor.h"
#include "../src/ClientClockMonitor.h"
#include "../src/ModuleConfig.h"
#include "../src/ReportValues.h"
#include <cstdlib>
#include <iostream>
#include <limits>

void Require(bool success, char const* message)
{
    if (!success)
    {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

int main()
{
    SkyFireAnticheat::ClientClockMonitor normalClock;
    SkyFireAnticheat::ClientClockMonitor fastClock;
    SkyFireAnticheat::ClientClockMonitor wrappedClock;
    bool clockDetected = false;
    for (std::uint32_t i = 0; i <= 30; ++i)
    {
        Require(!normalClock.Observe(i * 1000, i * 1000, 5000, 10000, 1.5, 2000), "normal clock");
        Require(!wrappedClock.Observe(i * 1000, 0xFFFFF000u + i * 1000, 5000, 10000, 1.5, 2000), "32-bit client clock wrap");
        clockDetected = fastClock.Observe(i * 1000, i * 3000, 5000, 10000, 1.5, 2000) || clockDetected;
    }
    Require(clockDetected, "accelerated client clock");
    normalClock.Reset();
    Require(!normalClock.Observe(100000, 500000, 5000, 10000, 1.5, 2000), "clock reset baseline");
    Require(!normalClock.Observe(120000, 900000, 5000, 10000, 1.5, 2000), "clock long-gap reset");
    Require(!normalClock.Observe(119000, 0, 5000, 10000, 1.5, 2000), "server clock regression");
    SkyFireAnticheat::ClientClockMonitor batchedClock;
    batchedClock.Observe(0, 0, 5000, 10000, 1.5, 2000);
    for (std::uint32_t i = 1; i <= 10; ++i)
        Require(!batchedClock.Observe(i * 1000, i * 1000 + (i % 2 ? 1000 : 0), 5000, 10000, 1.5, 2000), "clock jitter tolerance");
    Require(!batchedClock.Observe(11000, 0, 5000, 10000, 1.5, 2000), "backward client clock rebases");

    using SkyFireAnticheat::CheckConfigVersion;
    using SkyFireAnticheat::ConfigCompatibility;
    Require(CheckConfigVersion(0) == ConfigCompatibility::Outdated, "missing config serial");
    Require(CheckConfigVersion(-1) == ConfigCompatibility::Outdated, "negative config serial");
    Require(CheckConfigVersion(SkyFireAnticheat::ConfigVersion - 1) == ConfigCompatibility::Outdated, "old config serial");
    Require(CheckConfigVersion(SkyFireAnticheat::ConfigVersion) == ConfigCompatibility::Current, "matching config serial");
    Require(CheckConfigVersion(SkyFireAnticheat::ConfigVersion + 1) == ConfigCompatibility::Newer, "newer config serial");
    std::uint32_t guid;
    Require(SkyFireAnticheat::ParsePositiveUint32("4294967295", guid) && guid == 4294967295u, "maximum GUID");
    Require(SkyFireAnticheat::ParsePositiveUint32("0001", guid) && guid == 1, "leading zeros");
    for (char const* invalid : { "", "0", "-1", "+1", "1;DELETE", "1 OR 1=1", "4294967296", "9999999999999999999999" })
        Require(!SkyFireAnticheat::ParsePositiveUint32(invalid, guid), "reject invalid history arguments");
    Require(!SkyFireAnticheat::ParsePositiveUint32(nullptr, guid), "null history argument");
    Require(SkyFireAnticheat::CoordinateMilli(-12.5f) == -12500, "signed position encoding");
    Require(SkyFireAnticheat::CoordinateMilli(std::numeric_limits<float>::max()) == std::numeric_limits<std::int32_t>::max(), "large position bounds");
    Require(SkyFireAnticheat::CoordinateMilli(-std::numeric_limits<float>::max()) == std::numeric_limits<std::int32_t>::min(), "negative position bounds");
    Require(SkyFireAnticheat::CoordinateMilli(std::numeric_limits<float>::infinity()) == 0, "nonfinite position");
    using SkyFireAnticheat::MovementMonitor;
    MovementMonitor normal;
    for (unsigned i = 0; i < 1000; ++i)
        Require(!normal.Observe(i * 100, i * 0.7, 0, 7, 10, 1.3, 5000), "ordinary running");

    MovementMonitor fast;
    bool detected = false;
    for (unsigned i = 0; i < 100; ++i)
        detected = fast.Observe(i * 100, i * 2.1, 0, 7, 10, 1.3, 5000) || detected;
    Require(detected, "sustained triple-speed movement must be reported");

    MovementMonitor burst;
    Require(!burst.Observe(0, 0, 0, 7, 10, 1.3, 5000), "baseline");
    Require(!burst.Observe(1000, 7, 0, 7, 10, 1.3, 5000), "batched packets");
    Require(burst.Observe(1000, 30, 0, 7, 10, 1.3, 5000), "same-time displacement");
    Require(!burst.Observe(1100, 30.7, 0, 7, 10, 1.3, 5000), "reports must not poison the next sample");
    burst.Reset();
    Require(!burst.Observe(1200, 10000, 10000, 7, 10, 1.3, 5000), "authorized teleport reset");
    Require(!burst.Observe(12000, -10000, 0, 7, 10, 1.3, 5000), "long stall reset");
    Require(!burst.Observe(11900, 0, 0, 7, 10, 1.3, 5000), "clock regression reset");
    Require(burst.Observe(12000, std::numeric_limits<double>::quiet_NaN(), 0, 7, 10, 1.3, 5000), "invalid coordinates");
    Require(!burst.Observe(12100, 0, 0, 7, 10, 1.3, 5000), "invalid sample must not poison baseline");

    MovementMonitor idle;
    idle.Observe(0, 0, 0, 7, 10, 1.3, 5000);
    for (unsigned i = 1; i < 100; ++i)
        Require(!idle.Observe(i * 1000, 0, 0, 7, 10, 1.3, 5000), "stationary updates");
    Require(idle.Observe(100000, 100, 0, 7, 10, 1.3, 5000), "idle time cannot accumulate unlimited credit");
    std::cout << "Movement monitor tests passed\n";
}
