/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/
#ifndef SKYFIRE_MODULE_CLIENT_CLOCK_MONITOR_H
#define SKYFIRE_MODULE_CLIENT_CLOCK_MONITOR_H
#include <cstdint>

namespace SkyFireAnticheat
{
// Observe clock acceleration over a server-time window; never use client time
// to increase the movement budget. Unsigned subtraction handles 32-bit wrap.
class ClientClockMonitor
{
public:
    void Reset() { initialized = false; }
    std::uint64_t ServerElapsed() const { return observedServer; }
    std::uint32_t ClientElapsed() const { return observedClient; }
    bool Observe(std::uint64_t now, std::uint32_t clientTime,
        std::uint64_t maximumGap, std::uint64_t window, double ratio, std::uint32_t slack)
    {
        observedServer = 0;
        observedClient = 0;
        if (!initialized || now < lastServer || now - lastServer > maximumGap)
        {
            Begin(now, clientTime);
            return false;
        }
        lastServer = now;
        std::uint32_t elapsedClient = clientTime - startClient;
        // Backward/reset timestamps are ambiguous, not evidence of acceleration.
        if (elapsedClient > 0x7FFFFFFFu)
        {
            Begin(now, clientTime);
            return false;
        }
        std::uint64_t elapsedServer = now - startServer;
        if (elapsedServer < window)
            return false;
        observedServer = elapsedServer;
        observedClient = elapsedClient;
        bool accelerated = double(elapsedClient) > double(elapsedServer) * ratio + slack;
        Begin(now, clientTime);
        return accelerated;
    }
private:
    void Begin(std::uint64_t now, std::uint32_t clientTime)
    {
        initialized = true;
        lastServer = startServer = now;
        startClient = clientTime;
    }
    bool initialized = false;
    std::uint64_t startServer = 0;
    std::uint64_t lastServer = 0;
    std::uint32_t startClient = 0;
    std::uint64_t observedServer = 0;
    std::uint32_t observedClient = 0;
};
}
#endif
