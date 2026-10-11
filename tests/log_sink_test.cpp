#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#include <doctest/doctest.h>

#include "levain/core/log.hpp"

using levain::core::addLogSink;
using levain::core::LogLevel;
using levain::core::LogRecord;
using levain::core::LogSinkHandle;
using levain::core::setLogLevel;

namespace
{

/// Ce qu'un récepteur de test a reçu, sous verrou : il est appelé de plusieurs fils.
struct Received
{
    std::mutex mutex;
    std::vector<std::string> texts;
    std::vector<LogRecord>
        records; ///< Les vues sont périmées : seuls le niveau et le temps servent.
};

/// Ne garde que les messages de `category` : le registre est celui du processus, un message d'un
/// autre fil (ou d'un autre test) ne doit pas fausser les comptes exacts.
levain::core::LogSink collectInto(Received& received, std::string_view category)
{
    return [&received, category = std::string{category}](const LogRecord& record)
    {
        if (record.category != category)
        {
            return;
        }
        const std::scoped_lock lock{received.mutex};
        received.texts.emplace_back(record.category);
        received.texts.back() += '|';
        received.texts.back() += record.message;
        received.records.push_back(record);
    };
}

} // namespace

// Une poignée ne s'obtient que de `addLogSink` : un numéro forgé retirerait le récepteur d'un
// autre.
static_assert(!std::is_constructible_v<LogSinkHandle, std::uint64_t>);
static_assert(std::is_nothrow_move_constructible_v<LogSinkHandle>);

TEST_CASE(
    "un récepteur reçoit ce que la console écrirait, avec sa catégorie, son niveau et son temps")
{
    Received received;
    const LogSinkHandle sink = addLogSink(collectInto(received, "test.recepteur"));
    setLogLevel("test.recepteur", LogLevel::Warning);

    levain::core::logMessage("test.recepteur", LogLevel::Info,
                             "trop bas"); // sous le niveau : personne
    levain::core::logMessage("test.recepteur", LogLevel::Warning, "un");
    // L'horloge est monotone et avance : deux messages à 2 ms l'un de l'autre n'ont pas le même
    // temps (un temps resté à zéro, ou recopié, ne l'aurait pas).
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
    levain::core::logMessage("test.recepteur", LogLevel::Error, "deux");

    const std::scoped_lock lock{received.mutex};
    REQUIRE(received.texts.size() == 2);
    CHECK(received.texts[0] == "test.recepteur|un");
    CHECK(received.texts[1] == "test.recepteur|deux");
    CHECK(received.records[1].level == LogLevel::Error);
    CHECK(received.records[0].seconds >= 0.0);
    CHECK(received.records[1].seconds > received.records[0].seconds);
}

TEST_CASE("un récepteur retiré n'est plus jamais appelé, et une poignée déplacée ne le retire pas")
{
    Received received;
    LogSinkHandle sink = addLogSink(collectInto(received, "test.retrait"));
    setLogLevel("test.retrait", LogLevel::Info);
    levain::core::logMessage("test.retrait", LogLevel::Info, "avant");

    LogSinkHandle moved = std::move(sink);
    CHECK_FALSE(sink.active()); // NOLINT(bugprone-use-after-move) : l'état d'une poignée déplacée
                                // est défini
    CHECK(moved.active());
    sink.reset(); // la poignée vide ne retire rien
    levain::core::logMessage("test.retrait", LogLevel::Info, "après le déplacement");

    moved.reset();
    moved.reset(); // un second retrait est sans effet
    CHECK_FALSE(moved.active());
    levain::core::logMessage("test.retrait", LogLevel::Info, "après le retrait");

    const std::scoped_lock lock{received.mutex};
    CHECK(received.texts ==
          std::vector<std::string>{"test.retrait|avant", "test.retrait|après le déplacement"});
}

TEST_CASE("un récepteur peut se retirer lui-même, et ce qu'il journalise ne repasse par aucun "
          "récepteur")
{
    setLogLevel("test.reentrant", LogLevel::Info);
    setLogLevel("test.reentrant-ecrit", LogLevel::Info);
    LogSinkHandle sink;
    int calls = 0;
    sink = addLogSink(
        [&](const LogRecord& record)
        {
            if (record.category != "test.reentrant")
            {
                return;
            }
            ++calls;
            // Journalise depuis le récepteur : à la console seule, il n'est pas rappelé pour cet
            // écho.
            levain::core::logMessage("test.reentrant-ecrit", LogLevel::Info,
                                     "écho de " + std::string{record.message});
            sink.reset();
        });
    Received spectator; // un autre récepteur : il ne voit pas non plus l'écho du premier
    const LogSinkHandle other = addLogSink(
        [&spectator](const LogRecord& record)
        {
            const std::scoped_lock lock{spectator.mutex};
            spectator.texts.emplace_back(record.category);
        });
    levain::core::logMessage("test.reentrant", LogLevel::Info, "départ");
    CHECK(calls == 1);
    CHECK_FALSE(sink.active());
    levain::core::logMessage("test.reentrant", LogLevel::Info, "après");
    CHECK(calls == 1);
    // Le spectateur a vu les deux messages du test, jamais l'écho (on ignore ce que d'autres fils
    // ont écrit ailleurs).
    const std::scoped_lock lock{spectator.mutex};
    CHECK(std::ranges::count(spectator.texts, std::string{"test.reentrant"}) == 2);
    CHECK(std::ranges::count(spectator.texts, std::string{"test.reentrant-ecrit"}) == 0);
}

TEST_CASE("un récepteur que le précédent retire pendant la distribution n'est pas appelé ensuite")
{
    setLogLevel("test.distribution", LogLevel::Info);
    int secondCalls = 0;
    LogSinkHandle second;
    // Le premier est dans la copie de la liste que le message parcourt, le second aussi : seul le
    // drapeau `removed` empêche l'appel, la liste n'y peut plus rien. (Un seul fil : retirer un
    // autre récepteur depuis un récepteur reste interdit entre fils, voir LogSink.)
    const LogSinkHandle first = addLogSink([&second](const LogRecord&) { second.reset(); });
    second = addLogSink([&secondCalls](const LogRecord&) { ++secondCalls; });
    levain::core::logMessage("test.distribution", LogLevel::Info, "un seul message");
    CHECK(secondCalls == 0);
}
