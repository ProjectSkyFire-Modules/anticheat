/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/
#ifndef SKYFIRE_MODULE_MOVEMENT_MONITOR_H
#define SKYFIRE_MODULE_MOVEMENT_MONITOR_H

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace SkyFireAnticheat
{
// Independent of the core so boundary cases can be tested without a worldserver.
// A bounded distance budget tolerates packet batching without trusting client time.
class MovementMonitor
{
public:
    void Reset() { initialized = false; }
    double Distance() const { return distance; }
    double Allowance() const { return allowance; }
    std::uint64_t Elapsed() const { return elapsed; }

    bool Observe(std::uint64_t now, double x, double y, double speed,
        double slack, double multiplier, std::uint64_t maximumGap)
    {
        distance = allowance = 0;
        elapsed = 0;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(speed) || speed < 0)
        {
            Reset();
            return true;
        }

        double capacity = std::max(slack, speed * multiplier);
        if (!initialized || now < lastTime || now - lastTime > maximumGap)
        {
            initialized = true;
            lastTime = now;
            lastX = x;
            lastY = y;
            budget = capacity;
            return false;
        }

        elapsed = now - lastTime;
        budget = std::min(capacity, budget + speed * multiplier * elapsed / 1000.0);
        allowance = budget;
        double dx = x - lastX;
        double dy = y - lastY;
        distance = std::sqrt(dx * dx + dy * dy);
        budget -= distance;
        lastTime = now;
        lastX = x;
        lastY = y;
        bool exceeded = budget < -0.001;
        // Each sample starts from the last accepted position, even after a report.
        budget = std::max(0.0, budget);
        return exceeded;
    }

private:
    bool initialized = false;
    std::uint64_t lastTime = 0;
    double lastX = 0;
    double lastY = 0;
    double budget = 0;
    double distance = 0;
    double allowance = 0;
    std::uint64_t elapsed = 0;
};
}
#endif
