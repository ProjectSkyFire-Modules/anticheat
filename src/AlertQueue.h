/*
* This file is part of Project SkyFire https://www.projectskyfire.org.
* See LICENSE.md file for Copyright information
*/
#ifndef SKYFIRE_MODULE_ALERT_QUEUE_H
#define SKYFIRE_MODULE_ALERT_QUEUE_H
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace SkyFireAnticheat
{
// Caller owns synchronization. Bound storage and delivery independently.
class AlertQueue
{
public:
    void Clear() { entries.clear(); }
    void Push(std::uint64_t now, std::string const& text)
    {
        if (entries.size() == 100)
            entries.pop_front();
        entries.push_back({now, text});
    }
    std::vector<std::string> Take(std::uint64_t now)
    {
        std::vector<std::string> batch;
        while (!entries.empty())
        {
            Entry const& entry = entries.front();
            if (now < entry.time || now - entry.time > 10000)
            {
                entries.pop_front();
                continue;
            }
            if (batch.size() == 5)
                break;
            batch.push_back(entry.text);
            entries.pop_front();
        }
        return batch;
    }
private:
    struct Entry { std::uint64_t time; std::string text; };
    std::deque<Entry> entries;
};
}
#endif
