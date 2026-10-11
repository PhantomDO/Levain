#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "levain/core/log.hpp"
#include "levain/core/log_ring.hpp"

using levain::core::LogEntry;
using levain::core::LogLevel;
using levain::core::LogRing;
using levain::core::LogSeverity;

namespace
{

LogEntry entryOf(LogLevel level, std::string text)
{
    return {.level = level, .category = "test", .text = std::move(text), .seconds = 0.0};
}

} // namespace

TEST_CASE("une erreur est comptée, et un niveau critique avec elle")
{
    LogRing ring{8};
    levain::core::pushLogEntry(ring, entryOf(LogLevel::Info, "i"));
    levain::core::pushLogEntry(ring, entryOf(LogLevel::Debug, "d"));
    levain::core::pushLogEntry(ring, entryOf(LogLevel::Warning, "w"));
    levain::core::pushLogEntry(ring, entryOf(LogLevel::Error, "e"));
    levain::core::pushLogEntry(ring, entryOf(LogLevel::Critical, "c"));

    const levain::core::LogSnapshot snapshot = levain::core::snapshotOf(ring);
    CHECK(snapshot.counts.infos == 2);
    CHECK(snapshot.counts.warnings == 1);
    CHECK(snapshot.counts.errors == 2);
    CHECK(levain::core::countOf(snapshot.counts, LogSeverity::Error) == 2);
    CHECK(snapshot.entries.size() == 5);
}

TEST_CASE("l'anneau garde sa taille, oublie le plus ancien, et ses comptes restent ceux de tout ce "
          "qui est passé")
{
    LogRing ring{3};
    levain::core::pushLogEntry(ring, entryOf(LogLevel::Error, "perdue"));
    for (int i = 0; i < 5; ++i)
    {
        levain::core::pushLogEntry(ring, entryOf(LogLevel::Info, "info " + std::to_string(i)));
    }

    const levain::core::LogSnapshot snapshot = levain::core::snapshotOf(ring);
    REQUIRE(snapshot.entries.size() == 3);
    CHECK(snapshot.entries.front().text == "info 2");
    CHECK(snapshot.entries.back().text == "info 4");
    // L'erreur est sortie de l'anneau, pas du compte : c'est ce qui la fait voir dans la barre
    // d'état.
    CHECK(snapshot.counts.errors == 1);
    CHECK(snapshot.counts.infos == 5);
}

TEST_CASE("effacer vide l'anneau et les comptes, et change la génération que l'interface surveille")
{
    LogRing ring{4};
    levain::core::pushLogEntry(ring, entryOf(LogLevel::Error, "e"));
    const std::uint64_t before = levain::core::generationOf(ring);
    levain::core::pushLogEntry(ring, entryOf(LogLevel::Info, "i"));
    CHECK(levain::core::generationOf(ring) != before);

    const std::uint64_t written = levain::core::generationOf(ring);
    levain::core::clearLogRing(ring);
    const levain::core::LogSnapshot snapshot = levain::core::snapshotOf(ring);
    CHECK(snapshot.entries.empty());
    CHECK(snapshot.counts.errors == 0);
    CHECK(snapshot.counts.infos == 0);
    CHECK(snapshot.generation != written);
}

TEST_CASE("deux fils poussent ensemble dans un anneau qui déborde : aucun compte perdu, aucune "
          "entrée déchirée")
{
    constexpr int PerThread = 20000;
    constexpr std::size_t Capacity = 64;
    LogRing ring{Capacity};
    const auto write = [&ring](int thread)
    {
        for (int i = 0; i < PerThread; ++i)
        {
            levain::core::pushLogEntry(ring,
                                       entryOf(i % 4 == 0 ? LogLevel::Error : LogLevel::Info,
                                               std::to_string(thread) + ":" + std::to_string(i)));
        }
    };
    std::atomic<int> writing = 2;
    const auto writeThenSignal = [&](int thread)
    {
        write(thread);
        --writing;
    };
    std::thread first{writeThenSignal, 0};
    std::thread second{writeThenSignal, 1};
    // L'interface lit pendant qu'on écrit : chaque copie est cohérente (l'anneau déborde, ses
    // comptes la suivent).
    std::uint64_t seen = 0;
    while (writing > 0)
    {
        const levain::core::LogSnapshot partial = levain::core::snapshotOf(ring);
        CHECK(partial.entries.size() <= Capacity);
        CHECK(partial.counts.infos + partial.counts.errors >= partial.entries.size());
        CHECK(partial.generation >= seen);
        seen = partial.generation;
    }
    first.join();
    second.join();

    const levain::core::LogSnapshot snapshot = levain::core::snapshotOf(ring);
    CHECK(snapshot.counts.errors == 2 * (PerThread / 4));
    CHECK(snapshot.counts.infos == 2 * (PerThread - PerThread / 4));
    REQUIRE(snapshot.entries.size() == Capacity);
    std::vector<int> last(2, -1); // le dernier numéro vu de chaque fil : son ordre est gardé
    for (const LogEntry& entry : snapshot.entries)
    {
        const std::size_t colon = entry.text.find(':');
        REQUIRE(colon == 1); // « fil:numéro », jamais deux écritures mêlées
        const auto thread = static_cast<std::size_t>(entry.text.front() - '0');
        const int number = std::stoi(entry.text.substr(colon + 1));
        CHECK(number > last.at(thread));
        last.at(thread) = number;
    }
}
