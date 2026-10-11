#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
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

TEST_CASE(
    "retirer un récepteur attend l'appel qu'un autre fil y fait, ensuite personne n'y entre (ASan)")
{
    setLogLevel("test.attente", LogLevel::Info);
    std::atomic<bool> entered = false;
    std::atomic<bool> release = false;
    std::atomic<bool> resetReturned = false;
    LogSinkHandle sink = addLogSink(
        [&](const LogRecord&)
        {
            entered = true;
            while (!release)
            {
                std::this_thread::yield();
            }
        });

    std::thread writer{[] { levain::core::logMessage("test.attente", LogLevel::Info, "bloqué"); }};
    while (!entered)
    {
        std::this_thread::yield();
    }
    std::thread remover{[&]
                        {
                            sink.reset();
                            resetReturned = true;
                        }};
    // Le récepteur est dans l'appel : un retrait qui ne l'attendrait pas rendrait la main bien
    // avant.
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    CHECK_FALSE(resetReturned);
    release = true;
    writer.join();
    remover.join();
    CHECK(resetReturned);
}

TEST_CASE("un récepteur retiré pendant que des fils journalisent ne laisse aucun appel derrière "
          "lui (ASan)")
{
    setLogLevel("test.course", LogLevel::Info);
    std::atomic<bool> stop = false;
    std::vector<std::thread> writers;
    writers.reserve(2);
    for (int t = 0; t < 2; ++t)
    {
        writers.emplace_back(
            [&stop]
            {
                // Jusqu'à l'arrêt : à chaque tour, au moins un fil est dans un appel de récepteur.
                while (!stop)
                {
                    levain::core::logMessage("test.course", LogLevel::Info, "course");
                }
            });
    }
    int staleCalls = 0;
    for (int round = 0; round < 40; ++round)
    {
        // Ce que le récepteur vise : libéré juste après le retrait. Sous ASan, un appel qui le
        // touche encore est une utilisation après libération ; sans ASan, `inside` ne vaut plus 0
        // s'il traîne dans l'appel.
        auto inside = std::make_unique<std::atomic<int>>(0);
        LogSinkHandle sink = addLogSink(
            [target = inside.get()](const LogRecord&)
            {
                target->fetch_add(1);
                for (int spin = 0; spin < 2000; ++spin)
                {
                    std::this_thread::yield();
                }
                target->fetch_sub(1);
            });
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
        sink.reset();
        staleCalls += inside->load() != 0 ? 1 : 0;
        inside.reset();
    }
    stop = true;
    for (std::thread& writer : writers)
    {
        writer.join();
    }
    CHECK(staleCalls == 0);
}

TEST_CASE("deux fils journalisent ensemble : aucun message perdu, aucun mélangé")
{
    setLogLevel("test.deux-fils", LogLevel::Info);
    Received received;
    const LogSinkHandle sink = addLogSink(collectInto(received, "test.deux-fils"));
    constexpr int PerThread = 300;
    const auto write = [](int thread)
    {
        for (int i = 0; i < PerThread; ++i)
        {
            levain::core::logMessage("test.deux-fils", LogLevel::Info,
                                     "fil " + std::to_string(thread) + " message " +
                                         std::to_string(i));
        }
    };
    std::thread first{write, 0};
    std::thread second{write, 1};
    first.join();
    second.join();

    const std::scoped_lock lock{received.mutex};
    REQUIRE(received.texts.size() == 2 * PerThread);
    std::vector<int> next(2, 0); // le prochain numéro attendu de chaque fil : son ordre est gardé
    for (const std::string& text : received.texts)
    {
        const int thread = text.contains("fil 0 ") ? 0 : 1;
        CHECK(text == "test.deux-fils|fil " + std::to_string(thread) + " message " +
                          std::to_string(next[static_cast<std::size_t>(thread)]++));
    }
    CHECK(next == std::vector<int>{PerThread, PerThread});
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

TEST_CASE("deux récepteurs qui journalisent, appelés par deux fils, ne s'attendent pas l'un "
          "l'autre")
{
    setLogLevel("test.blocage", LogLevel::Info);
    setLogLevel("test.blocage-ecrit", LogLevel::Info);
    std::atomic<int> firstCalls = 0;
    std::atomic<int> secondCalls = 0;
    const auto talkative = [](std::atomic<int>& calls)
    {
        return [&calls](const LogRecord& record)
        {
            if (record.category != "test.blocage")
            {
                return;
            }
            ++calls;
            // Un récepteur qui reste un instant dans son appel, puis journalise : sans la coupure
            // de la ré-entrée, le message écrit irait à l'autre récepteur, sous le verrou du
            // premier.
            std::this_thread::yield();
            levain::core::logMessage("test.blocage-ecrit", LogLevel::Info, "écho");
        };
    };
    const LogSinkHandle first = addLogSink(talkative(firstCalls));
    const LogSinkHandle second = addLogSink(talkative(secondCalls));
    constexpr int PerThread = 300;
    std::promise<void> finished;
    std::future<void> done = finished.get_future();
    std::thread writers{[&finished]
                        {
                            std::vector<std::thread> threads;
                            threads.reserve(2);
                            for (int t = 0; t < 2; ++t)
                            {
                                threads.emplace_back(
                                    []
                                    {
                                        for (int i = 0; i < PerThread; ++i)
                                        {
                                            levain::core::logMessage("test.blocage", LogLevel::Info,
                                                                     "message");
                                        }
                                    });
                            }
                            for (std::thread& thread : threads)
                            {
                                thread.join();
                            }
                            finished.set_value();
                        }};
    if (done.wait_for(std::chrono::seconds{30}) != std::future_status::ready)
    {
        // Un blocage mutuel ne se rattrape pas : les fils, les verrous et la poignée sont perdus.
        // Échouer fort, ici, plutôt que de figer toute la suite.
        std::fputs("blocage mutuel : deux récepteurs qui journalisent s'attendent\n", stderr);
        std::_Exit(1);
    }
    writers.join();
    CHECK(firstCalls == 2 * PerThread);
    CHECK(secondCalls == 2 * PerThread);
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
