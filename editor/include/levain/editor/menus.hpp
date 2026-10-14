#pragma once

// La barre de menus de l'éditeur et la fenêtre Aide (ADR-0036, décisions 12 et 13) : **Fichier,
// Édition, Entité, Fenêtre, Aide**. Une action dont le milestone n'est pas là (`availabilityOf`) y
// est grisée : elle montre son raccourci, dit au survol quand elle arrive, et ne fait rien au clic.
// Chaque entrée montre le raccourci de la table (`displayNameOf`, selon la disposition du clavier).
// La fenêtre Aide liste **chaque raccourci de la table** : l'action, son accord tel que la
// disposition d'aujourd'hui l'écrit (« Z » à la place du W sur un AZERTY), son contexte, et la
// touche d'Unreal et d'Unity ; une recherche la filtre. La logique est en fonctions libres dont
// toutes les dépendances sont des paramètres (ADR-0011) : `helpRowsOf` et `helpRowMatches` ne
// touchent pas ImGui. Équivalents : la barre de menus d'Unreal et d'Unity, « Editor Preferences >
// Keyboard Shortcuts » d'Unreal, le Shortcuts Manager d'Unity, l'Aide de Godot.

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "levain/editor/shortcuts.hpp"
#include "levain/editor/window_menu.hpp"
#include "levain/platform/input.hpp"

namespace levain::editor
{

/// La clé de catalogue du titre de la fenêtre Aide.
inline constexpr const char* HelpWindow = "Aide";

/// La touche que fait chaque moteur pour la même chose : la colonne qui apprend l'éditeur à qui
/// vient d'Unreal ou d'Unity. Le texte vide : pas de raccourci chez lui. Le tableau des gestes
/// (editor/GESTES.md) en est la source, « documenté » ou « supposé » à côté de chaque ligne.
struct Equivalents
{
    const char* unreal = "";
    const char* unity = "";
};

[[nodiscard]] constexpr Equivalents equivalentsOf(Action action)
{
    switch (action)
    {
    case Action::MoveForward:
    case Action::MoveBackward:
    case Action::MoveLeft:
    case Action::MoveRight:
        return {.unreal = "W A S D", .unity = "W A S D"};
    case Action::MoveUp:
    case Action::MoveDown:
        return {.unreal = "E Q", .unity = "E Q"};
    case Action::FrameSelection:
        return {.unreal = "F", .unity = "F"};
    case Action::GameView:
        return {.unreal = "G", .unity = ""};
    case Action::RealTime:
        return {.unreal = "Ctrl+R", .unity = ""};
    case Action::ToolMove:
        return {.unreal = "W", .unity = "W"};
    case Action::ToolRotate:
        return {.unreal = "E", .unity = "E"};
    case Action::ToolScale:
        return {.unreal = "R", .unity = "R"};
    case Action::NextTool:
        return {.unreal = "Espace", .unity = ""};
    case Action::Save:
        return {.unreal = "Ctrl+S", .unity = "Ctrl+S"};
    case Action::Undo:
        return {.unreal = "Ctrl+Z", .unity = "Ctrl+Z"};
    case Action::Redo:
        return {.unreal = "Ctrl+Y", .unity = "Ctrl+Y"};
    case Action::Play:
        return {.unreal = "Alt+P", .unity = "Ctrl+P"};
    case Action::Simulate:
        return {.unreal = "Alt+S", .unity = ""};
    case Action::Stop:
        return {.unreal = "Échap", .unity = "Ctrl+P"};
    case Action::GiveMouseBack:
        return {.unreal = "Maj+F1", .unity = "Échap"};
    case Action::Count:
        break;
    }
    return {};
}

/// Le nom du contexte d'un accord, clé du catalogue : où il vaut.
[[nodiscard]] constexpr const char* contextNameOf(Context context)
{
    switch (context)
    {
    case Context::Flight:
        return "Vol (clic droit tenu)";
    case Context::View:
        return "Vue";
    case Context::Global:
        return "Partout";
    case Context::Game:
        return "Jeu";
    }
    return "";
}

/// Une ligne de la fenêtre Aide : tout ce qu'elle écrit, déjà traduit et selon la disposition du
/// clavier.
struct HelpRow
{
    Action action = Action::Count;
    std::string name;    ///< Le nom de l'action.
    std::string chords;  ///< Ses accords, « Ctrl+Y / Ctrl+Maj+Z ».
    std::string context; ///< Où il vaut.
    std::string unreal;
    std::string unity;
};

/// **Une ligne par entrée de la table**, dans son ordre : la fenêtre Aide n'oublie aucun
/// raccourci. Les accords de position s'écrivent selon `keyAtPosition` (« Z » pour Avancer sur un
/// AZERTY).
[[nodiscard]] std::vector<HelpRow> helpRowsOf(const ShortcutTable& table,
                                              const platform::KeyAtPosition& keyAtPosition);

/// La ligne répond à la recherche : le texte est dans une de ses colonnes, sans tenir compte de la
/// casse des lettres ASCII (comme la console). Une recherche vide les garde toutes.
[[nodiscard]] bool helpRowMatches(const HelpRow& row, std::string_view search);

/// Où la barre a été dessinée à la dernière image (le centre de chaque titre, en pixels de
/// l'écran), pour que les tests la cliquent à la souris scriptée.
struct MenuBarLayout
{
    ImVec2 file{};
    ImVec2 edit{};
    ImVec2 entity{};
    ImVec2 help{};
    ImVec2 save{};           ///< « Enregistrer », valable `fileOpen`.
    ImVec2 undo{};           ///< « Annuler », valable `editOpen`.
    ImVec2 create{};         ///< « Créer… », valable `entityOpen`.
    ImVec2 preferences{};    ///< « Préférences », valable `editOpen`.
    ImVec2 shortcutsEntry{}; ///< « Raccourcis… », valable `preferencesOpen`.
    bool editOpen = false;
    bool preferencesOpen = false;
    bool entityOpen = false;
    ImVec2 quit{};          ///< « Quitter l'éditeur », valable `fileOpen`.
    ImVec2 helpShortcuts{}; ///< « Raccourcis », valable `helpOpen`.
    bool fileOpen = false;
    bool helpOpen = false;
};

/// La fenêtre Aide, ce qu'elle garde et ce qu'elle a montré.
struct HelpState
{
    bool open = false;
    /// Le texte cherché, terminé par un zéro (le tampon du champ d'ImGui).
    std::array<char, 128> search{};
    /// Les lignes montrées à la dernière image (`helpRowMatches`) : ce que les tests lisent.
    std::vector<HelpRow> shown;
    std::size_t total = 0; ///< Toutes les lignes, filtre ou non.
    ImVec2 searchAt{};
};

/// Ce que la barre demande à l'image où l'on clique : les actions des entrées cliquées, Quitter, et
/// « Réinitialiser la disposition » du menu Fenêtre.
struct MenuRequests
{
    std::vector<Action> actions;
    bool quit = false;
    bool resetLayout = false;
    bool openShortcuts = false; ///< « Préférences > Raccourcis… » (morceau 16).
};

/// Ce dont la barre a besoin, tout en paramètres.
struct MenuBarInputs
{
    const ShortcutTable& table;
    const platform::KeyAtPosition& keyAtPosition;
    std::span<const WindowEntry> windows;
};

/// Dessine la barre de menus principale : Fichier, Édition, Entité, Fenêtre (`windowLayout`), Aide.
/// Panneaux ouverts seulement : la barre retire sa hauteur à l'espace d'ancrage de l'image
/// suivante.
/// **Une entrée grisée ne rend rien au clic.**
[[nodiscard]] MenuRequests drawMenuBar(const MenuBarInputs& inputs, WindowMenuLayout& windowLayout,
                                       MenuBarLayout& layout, HelpState& help);

/// La fenêtre Aide, si elle est ouverte : la recherche et le tableau des raccourcis.
void drawHelp(HelpState& help, const MenuBarInputs& inputs);

} // namespace levain::editor
