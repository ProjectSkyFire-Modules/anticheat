/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/
#ifndef SKYFIRE_MODULE_TRAVERSAL_MONITOR_H
#define SKYFIRE_MODULE_TRAVERSAL_MONITOR_H
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace SkyFireAnticheat
{
struct TraversalSample
{
    std::uint64_t time = 0;
    double x = 0, y = 0, z = 0;
    double speed = 0;
    bool jump = false;
    bool groundSampled = false;
    bool groundKnown = false;
    bool grounded = false;
    bool terrainExempt = false;
};
struct TraversalLimits
{
    std::uint64_t maximumGap = 5000;
    double teleportDistance = 50;
    double speedMultiplier = 1.3;
    double distanceSlack = 10;
    double jumpRise = 2;
    double climbRise = 5;
    double climbSlope = 3;
};
struct TraversalResult
{
    bool teleport = false, jump = false, climb = false;
    double horizontal = 0, allowance = 0, rise = 0, slope = 0;
    std::uint64_t elapsed = 0;
};

// Heuristics for investigation only. Ground observations come from server terrain,
// not a client's falling/ground flag. Missing terrain never counts as evidence.
class TraversalMonitor
{
public:
    void Reset() { initialized = groundBaseline = airborne = false; }
    TraversalResult Observe(TraversalSample const& sample, TraversalLimits const& limits)
    {
        TraversalResult result;
        if (!std::isfinite(sample.x) || !std::isfinite(sample.y) || !std::isfinite(sample.z) ||
            !std::isfinite(sample.speed) || sample.speed < 0)
        {
            Reset();
            return result;
        }
        if (!initialized || sample.time < previous.time || sample.time - previous.time > limits.maximumGap)
        {
            Reset();
            initialized = true;
            previous = sample;
        }
        else
        {
            result.elapsed = sample.time - previous.time;
            double dx = sample.x - previous.x, dy = sample.y - previous.y;
            result.horizontal = std::sqrt(dx * dx + dy * dy);
            result.allowance = std::max(limits.teleportDistance,
                sample.speed * limits.speedMultiplier * result.elapsed / 1000.0 + limits.distanceSlack);
            result.teleport = result.horizontal > result.allowance;
            previous = sample;
        }

        if (sample.terrainExempt)
        {
            groundBaseline = airborne = false;
            return result;
        }
        if (airborne && sample.time - jumpTime > 10000)
            airborne = false;
        if (sample.jump)
        {
            if (airborne && sample.groundKnown && !sample.grounded)
            {
                result.rise = sample.z - jumpZ;
                result.jump = result.rise > limits.jumpRise;
            }
            airborne = true;
            jumpTime = sample.time;
            jumpZ = sample.z;
            groundBaseline = false;
            return result;
        }
        if (!sample.groundKnown)
        {
            if (sample.groundSampled)
                groundBaseline = false;
            return result;
        }
        if (!sample.grounded)
        {
            groundBaseline = false;
            return result;
        }
        // A verified return to the ground ends a jump; a forged landing opcode does not.
        bool landed = airborne;
        airborne = false;
        if (!groundBaseline || landed || sample.time - ground.time > limits.maximumGap)
        {
            ground = sample;
            groundBaseline = true;
            return result;
        }
        // Accumulate enough displacement to avoid flagging individual stair steps.
        if (sample.time - ground.time < 500)
            return result;
        double dx = sample.x - ground.x, dy = sample.y - ground.y;
        double run = std::sqrt(dx * dx + dy * dy);
        result.rise = sample.z - ground.z;
        result.slope = result.rise / std::max(run, 0.1);
        result.climb = result.rise > limits.climbRise && result.slope > limits.climbSlope;
        ground = sample;
        return result;
    }
private:
    bool initialized = false, groundBaseline = false, airborne = false;
    TraversalSample previous, ground;
    std::uint64_t jumpTime = 0;
    double jumpZ = 0;
};
}
#endif
