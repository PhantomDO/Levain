#include "levain/core/log_ring.hpp"

#include <utility>

namespace levain::core
{

namespace
{

void countMessage(LogCounts& counts, LogLevel level)
{
    switch (severityOf(level))
    {
    case LogSeverity::Info:
        ++counts.infos;
        break;
    case LogSeverity::Warning:
        ++counts.warnings;
        break;
    case LogSeverity::Error:
        ++counts.errors;
        break;
    }
}

} // namespace

void pushLogEntry(LogRing& ring, LogEntry entry)
{
    const std::scoped_lock lock{ring.mutex};
    countMessage(ring.counts, entry.level);
    ring.entries.push_back(std::move(entry));
    while (ring.entries.size() > ring.capacity)
    {
        ring.entries.pop_front();
    }
    ++ring.generation;
}

void clearLogRing(LogRing& ring)
{
    const std::scoped_lock lock{ring.mutex};
    ring.entries.clear();
    ring.counts = {};
    ++ring.generation;
}

LogSnapshot snapshotOf(const LogRing& ring)
{
    const std::scoped_lock lock{ring.mutex};
    return {.entries = {ring.entries.begin(), ring.entries.end()},
            .counts = ring.counts,
            .generation = ring.generation};
}

std::uint64_t generationOf(const LogRing& ring)
{
    const std::scoped_lock lock{ring.mutex};
    return ring.generation;
}

} // namespace levain::core
