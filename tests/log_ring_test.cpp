#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
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

TEST_CASE(
    "lastEntryOf rend le plus récent, rien si l'anneau est vide, et les comptes sont ceux de tout")
{
    LogRing ring{2};
    CHECK_FALSE(levain::core::lastEntryOf(ring).has_value());
    levain::core::pushLogEntry(ring, entryOf(LogLevel::Error, "a"));
    levain::core::pushLogEntry(ring, entryOf(LogLevel::Info, "b"));
    levain::core::pushLogEntry(ring, entryOf(LogLevel::Info, "c")); // « a » est oublié
    const auto last = levain::core::lastEntryOf(ring);
    CHECK(last.has_value());
    CHECK(last.value_or(LogEntry{}).text == "c");
    CHECK(levain::core::countsOf(ring).errors == 1); // l'erreur oubliée reste comptée
    CHECK(levain::core::countsOf(ring).infos == 2);
    levain::core::clearLogRing(ring);
    CHECK_FALSE(levain::core::lastEntryOf(ring).has_value());
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

TEST_CASE("une capture reçoit le journal du processus, et libérée, plus rien ne l'atteint (ASan)")
{
    levain::core::setLogLevel("test.capture", LogLevel::Info);
    {
        levain::core::LogCapture capture = levain::core::captureLog(16);
        levain::core::log("test.capture", LogLevel::Warning, "valeur {}", 42);
        levain::core::log("test.capture", LogLevel::Error, "panne");
        const levain::core::LogSnapshot snapshot = levain::core::snapshotOf(*capture.ring);
        REQUIRE(snapshot.entries.size() == 2);
        CHECK(snapshot.entries[0].category == "test.capture");
        CHECK(snapshot.entries[0].text == "valeur 42");
        CHECK(snapshot.entries[1].level == LogLevel::Error);
        CHECK(snapshot.counts.warnings == 1);
        CHECK(snapshot.counts.errors == 1);
    }
    // L'anneau est libéré avec sa capture : ce message ne doit toucher rien (ASan le verrait).
    levain::core::log("test.capture", LogLevel::Error, "personne ne l'écoute");
}

TEST_CASE("deux fils écrivent ensemble dans une capture : ni entrée perdue, ni texte mélangé, ni "
          "compte faux")
{
    levain::core::setLogLevel("test.anneau-fils", LogLevel::Info);
    constexpr int PerThread = 400;
    const levain::core::LogCapture capture =
        levain::core::captureLog(2 * static_cast<std::size_t>(PerThread));
    const auto write = [](int thread)
    {
        for (int i = 0; i < PerThread; ++i)
        {
            // Une erreur sur trois : les comptes par niveau passent aussi par les deux fils.
            levain::core::log("test.anneau-fils", i % 3 == 0 ? LogLevel::Error : LogLevel::Info,
                              "fil {} numéro {}", thread, i);
        }
    };
    std::thread first{write, 0};
    std::thread second{write, 1};
    first.join();
    second.join();

    const levain::core::LogSnapshot snapshot = levain::core::snapshotOf(*capture.ring);
    REQUIRE(snapshot.entries.size() == 2 * PerThread);
    std::vector<int> next(2, 0);
    for (const LogEntry& entry : snapshot.entries)
    {
        const int thread = entry.text.starts_with("fil 0 ") ? 0 : 1;
        CHECK(entry.text == "fil " + std::to_string(thread) + " numéro " +
                                std::to_string(next[static_cast<std::size_t>(thread)]++));
    }
    CHECK(next == std::vector<int>{PerThread, PerThread});
    CHECK(snapshot.counts.errors == 2 * ((PerThread + 2) / 3));
    CHECK(snapshot.counts.infos ==
          static_cast<std::uint64_t>(2 * PerThread) - snapshot.counts.errors);
}

// Le piège de `LogCapture`, nommé : remplacer une capture par une autre ne se fait pas par
// affectation (elle libérerait l'ancien anneau pendant que l'ancien récepteur est encore inscrit).
static_assert(!std::is_move_assignable_v<levain::core::LogCapture>);
static_assert(!std::is_copy_constructible_v<levain::core::LogCapture>);
static_assert(std::is_nothrow_move_constructible_v<levain::core::LogCapture>);

TEST_CASE("des captures créées et détruites pendant qu'un fil journalise ne reçoivent rien "
          "après leur fin (ASan)")
{
    levain::core::setLogLevel("test.capture-course", LogLevel::Info);
    std::atomic<bool> stop = false;
    std::thread writer{[&stop]
                       {
                           while (!stop)
                           {
                               levain::core::log("test.capture-course", LogLevel::Info, "course");
                           }
                       }};
    std::optional<levain::core::LogCapture> held;
    for (int round = 0; round < 200; ++round)
    {
        // `emplace` détruit l'ancienne capture avant de construire la neuve : son anneau est libéré
        // pendant que le fil écrit, ce que seul un récepteur déjà retiré supporte. Le déplacement
        // (la capture rendue par `captureLog`, puis celle de `emplace`) garde le récepteur vivant.
        held.emplace(levain::core::captureLog(8));
        std::this_thread::yield();
        if (round % 20 == 19)
        {
            held.reset(); // une pause sans capture du tout
            std::this_thread::yield();
        }
    }
    stop = true;
    writer.join();

    // Une capture déplacée reçoit toujours : l'anneau n'a pas changé d'adresse.
    levain::core::LogCapture first = levain::core::captureLog(8);
    const levain::core::LogCapture second = std::move(first);
    // L'état d'une capture déplacée est défini : ni anneau, ni récepteur.
    // NOLINTBEGIN(bugprone-use-after-move)
    CHECK(first.ring == nullptr);
    CHECK_FALSE(first.sink.active());
    // NOLINTEND(bugprone-use-after-move)
    levain::core::log("test.capture-course", LogLevel::Warning, "après le déplacement");
    REQUIRE(second.ring != nullptr);
    CHECK(levain::core::snapshotOf(*second.ring).counts.warnings == 1);
}
