/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/
#include "../src/MovementMonitor.h"
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
