// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>

namespace DeskPortTraffic {
// Today's client traffic across every session. Sessions report cumulative
// byte counters; only increases are added. A counter that falls belongs to a
// new session and counts from zero. Local midnight starts a new day.
class DailyUsage {
public:
    std::int64_t account(const std::string& today, const std::string& session, std::int64_t total) {
        rollover(today);
        if (total < 0) return 0;
        const auto previous = last.find(session);
        const std::int64_t delta = previous == last.end() || total < previous->second ? total : total - previous->second;
        last[session] = total;
        used += delta;
        return delta;
    }
    void rollover(const std::string& today) {
        if (today != day) { day = today; used = 0; }
    }
    void forget(const std::string& session) { last.erase(session); }
    void reset() { used = 0; }
    std::string day;
    std::int64_t used = 0;
private:
    std::map<std::string, std::int64_t> last;
};

enum class Level { Normal = 0, Warning = 1, Critical = 2, Exceeded = 3 };
// Yellow from 70%, red from 85%, exceeded at 100% of a positive daily limit.
inline Level level(std::int64_t used, std::int64_t limit) {
    if (limit <= 0 || used < 0) return Level::Normal;
    if (used >= limit) return Level::Exceeded;
    if (used * 100 >= limit * 85) return Level::Critical;
    if (used * 100 >= limit * 70) return Level::Warning;
    return Level::Normal;
}
}
