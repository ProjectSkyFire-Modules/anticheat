/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/
#include "../src/TraversalMonitor.h"
#include "../src/ReportValues.h"
#include <cstdlib>
#include <iostream>
#include <limits>

void Check(bool condition, char const* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}
SkyFireAnticheat::TraversalSample Sample(std::uint64_t time, double x, double z,
    bool grounded = true, bool jump = false)
{
    SkyFireAnticheat::TraversalSample sample;
    sample.time = time;
    sample.x = x;
    sample.z = z;
    sample.speed = 7;
    sample.groundKnown = true;
    sample.groundSampled = true;
    sample.grounded = grounded;
    sample.jump = jump;
    return sample;
}

int main()
{
    using namespace SkyFireAnticheat;
    TraversalLimits limits;
    TraversalMonitor normal;
    for (unsigned i = 0; i < 100; ++i)
    {
        auto report = normal.Observe(Sample(i * 500, i * 3.5, i * 0.5), limits);
        Check(!report.teleport && !report.jump && !report.climb, "normal running on stairs");
    }
    TraversalMonitor teleport;
    teleport.Observe(Sample(0, 0, 0), limits);
    auto report = teleport.Observe(Sample(100, 100, 0), limits);
    Check(report.teleport && report.horizontal == 100 && report.allowance == 50, "large displacement evidence");
    teleport.Reset();
    Check(!teleport.Observe(Sample(200, 10000, 0), limits).teleport, "server teleport reset");
    Check(!teleport.Observe(Sample(10000, -10000, 0), limits).teleport, "long gap baseline");
    Check(!teleport.Observe(Sample(9000, 0, 0), limits).teleport, "server clock regression");
    auto invalid = Sample(9100, 0, 0);
    invalid.x = std::numeric_limits<double>::quiet_NaN();
    Check(!teleport.Observe(invalid, limits).teleport, "invalid sample not traversal evidence");
    Check(!teleport.Observe(Sample(9200, 10000, 0), limits).teleport, "invalid sample resets baseline");

    TraversalMonitor jump;
    Check(!jump.Observe(Sample(0, 0, 0, true, true), limits).jump, "first jump");
    Check(jump.Observe(Sample(500, 1, 4, false, true), limits).jump, "rising repeated airborne jump");
    Check(!jump.Observe(Sample(1000, 2, 0), limits).jump, "verified landing");
    Check(!jump.Observe(Sample(1100, 2, 0, true, true), limits).jump, "jump after landing");
    auto missing = Sample(1500, 3, 5, false, true);
    missing.groundKnown = false;
    Check(!jump.Observe(missing, limits).jump, "missing terrain cannot prove airborne jump");
    auto flying = Sample(2000, 4, 15, false, true);
    flying.terrainExempt = true;
    Check(!jump.Observe(flying, limits).jump, "authorized aerial movement exemption");
    jump.Reset();
    Check(!jump.Observe(Sample(2500, 5, 20, false, true), limits).jump, "forced movement reset");

    TraversalMonitor climb;
    climb.Observe(Sample(0, 0, 0), limits);
    report = climb.Observe(Sample(500, 1, 10), limits);
    Check(report.climb && report.rise == 10 && report.slope == 10, "steep grounded ascent");
    Check(!climb.Observe(Sample(1000, 2, 0), limits).climb, "descent is not climb");
    climb.Reset();
    climb.Observe(Sample(0, 0, 0, true, true), limits);
    Check(!climb.Observe(Sample(500, 1, 10, false), limits).climb, "airborne ascent is not climb");
    Check(!climb.Observe(Sample(1000, 1, 10), limits).climb, "landing rebases climb");
    climb.Reset();
    climb.Observe(Sample(0, 0, 0), limits);
    auto unknown = Sample(500, 1, 10);
    unknown.groundKnown = false;
    Check(!climb.Observe(unknown, limits).climb, "unknown terrain is not climb evidence");
    Check(!climb.Observe(Sample(1000, 2, 20), limits).climb, "unknown terrain breaks climb baseline");

    Check(HexEvidence("a'\\") == "61275c", "safe evidence SQL literal");
    Check(HexEvidence(std::string(600, 'a')).size() == 1024, "evidence length bound");
    Check(HexEvidence("").empty(), "empty evidence");
    std::cout << "Traversal tests passed\n";
}
