#pragma once

#include <cstdint>
#include <format>
#include <functional>
#include <string_view>
#include <utility>

namespace levain::core
{

enum class LogLevel : std::uint8_t
{
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical,
};

/// Le niveau en dessous duquel une catégorie ne journalise plus rien.
void setLogLevel(std::string_view category, LogLevel level);

/// Vrai si un message de ce niveau serait effectivement écrit.
[[nodiscard]] bool isLogEnabled(std::string_view category, LogLevel level);

/// Écrit un message déjà formaté. Point d'entrée non générique : c'est lui qui permet de
/// garder les en-têtes de spdlog hors de `core` (voir l'invariant n°1 du README du module).
void logMessage(std::string_view category, LogLevel level, std::string_view message);

/// Un message tel que le voit un récepteur (`addLogSink`). Les vues ne valent que pendant l'appel :
/// un récepteur qui garde le message en fait une copie.
struct LogRecord
{
    LogLevel level = LogLevel::Info;
    std::string_view category;
    std::string_view message;
    /// Secondes depuis le premier message du processus, sur l'horloge monotone : un ordre et des
    /// durées, pas une heure du jour (`localtime` demanderait un fuseau, que la console n'affiche
    /// pas).
    double seconds = 0.0;
};

/// Ce qui reçoit les messages en plus de la console du processus : la fenêtre « Console » de
/// l'éditeur (ADR-0036, décision 16). Appelé depuis le fil qui journalise, quel qu'il soit : les
/// traces et assertions de Jolt partent de ses fils de calcul, le chargeur d'assets du sien.
///
/// **La règle des verrous.** `core` n'en tient aucun pendant l'appel : un récepteur peut donc
/// ajouter un récepteur. Chaque récepteur est appelé sous son propre verrou, récursif, que
/// `LogSinkHandle::reset` prend aussi : une fois `reset` rendu, aucun fil n'est dans le récepteur
/// et aucun n'y entrera, le propriétaire peut détruire ce que le récepteur vise. D'où deux
/// interdits : un récepteur ne retire jamais un *autre* récepteur (deux fils qui se retirent l'un
/// l'autre s'attendraient), et on ne retire jamais un récepteur en tenant un verrou qu'il prend
/// lui-même.
///
/// **Un récepteur qui journalise** écrit à la console du processus et à elle seule : un message
/// parti d'un récepteur ne passe par aucun récepteur, le sien compris. Sans cette coupure, il se
/// rappellerait sans fin, et deux récepteurs qui journalisent, sur deux fils, s'attendraient l'un
/// l'autre (chacun tient son verrou en attendant celui de l'autre).
using LogSink = std::function<void(const LogRecord&)>;

/// L'inscription d'un récepteur, retirée à la destruction : un récepteur qui vise un objet ne
/// survit pas à son propriétaire, tant que le propriétaire déclare la poignée **après** l'objet
/// (destruction en sens inverse), ou la retire lui-même en premier dans son destructeur, comme
/// `LogCapture` (log_ring.hpp), que l'ordre de ses membres ne peut plus piéger.
class LogSinkHandle
{
public:
    LogSinkHandle() = default;

    LogSinkHandle(const LogSinkHandle&) = delete;
    LogSinkHandle& operator=(const LogSinkHandle&) = delete;

    LogSinkHandle(LogSinkHandle&& other) noexcept : m_id(std::exchange(other.m_id, 0)) {}

    LogSinkHandle& operator=(LogSinkHandle&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            m_id = std::exchange(other.m_id, 0);
        }
        return *this;
    }

    ~LogSinkHandle() { reset(); }

    /// Retire le récepteur, et attend la fin de l'appel qu'un autre fil y fait peut-être. Sans
    /// effet sur une poignée vide ou déjà retirée.
    void reset() noexcept;

    [[nodiscard]] bool active() const { return m_id != 0; }

private:
    /// Seul `addLogSink` en fait une : un numéro forgé retirerait le récepteur d'un autre.
    friend LogSinkHandle addLogSink(LogSink sink);

    explicit LogSinkHandle(std::uint64_t id) : m_id(id) {}

    std::uint64_t m_id = 0;
};

/// Inscrit un récepteur de plus, qui reçoit les messages que la console du processus écrirait (un
/// message sous le niveau de sa catégorie n'arrive à personne). Un récepteur vide est un bug.
[[nodiscard]] LogSinkHandle addLogSink(LogSink sink);

/// Journalise dans une catégorie. Une catégorie par module : on peut faire taire `assets`
/// sans rien changer à `gpu`. Équivalent des `DECLARE_LOG_CATEGORY_EXTERN` d'Unreal et des
/// canaux de `print_verbose` de Godot.
///
/// Le test de niveau précède le formatage : sans lui, un `LEVAIN_LOG(Trace, …)` dans une
/// boucle de rendu paierait le `std::format` à chaque frame pour un message jeté ensuite.
template <typename... Args>
void log(std::string_view category, LogLevel level, std::format_string<Args...> fmt, Args&&... args)
{
    if (!isLogEnabled(category, level))
    {
        return;
    }

    logMessage(category, level, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace levain::core
