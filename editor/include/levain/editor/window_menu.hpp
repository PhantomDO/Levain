#pragma once

// Le menu « Fenêtre » de la barre de menus principale (ADR-0036, décision 10) : une case par
// fenêtre, cochée tant qu'elle est ouverte, qui la ferme ou la rouvre. La barre complète
// (Fichier, Édition, Entité, Fenêtre, Aide) est `drawMenuBar` (menus.hpp, morceau 12). Équivalents
// : Window d'Unity et d'Unreal, le menu Éditeur > Disposition de Godot.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <imgui.h>

namespace levain::editor
{

/// Une fenêtre du menu : sa clé de catalogue (`ui::tr`) et le drapeau d'ouverture qu'elle partage
/// avec sa croix.
struct WindowEntry
{
    const char* french = "";
    bool* open = nullptr;
};

/// Le plus de fenêtres que le menu relève : Hiérarchie, Inspecteur, Console, Vue, Image, Passes,
/// Scène.
inline constexpr std::size_t MaxWindowEntries = 8;

/// Où le menu a été dessiné à la dernière image, pour que les tests le cliquent à la souris
/// scriptée. Les cases ne valent que menu ouvert (`itemsShown`).
struct WindowMenuLayout
{
    ImVec2 title{};
    bool itemsShown = false;
    std::array<ImVec2, MaxWindowEntries> items{};
    ImVec2 reset{}; ///< « Réinitialiser la disposition », valable `itemsShown`.
};

/// Le centre du dernier widget dessiné : l'endroit où cliquer.
[[nodiscard]] ImVec2 centerOfLastItem();

/// Où la bulle « Arrive en M7.x » se pose par rapport à l'entrée grisée : à droite d'une entrée de
/// menu (dessous, elle couvrirait l'entrée suivante : « Quitter l'éditeur » sous « Enregistrer »),
/// sous un bouton de la barre d'outils (à droite, elle couvrirait le bouton suivant).
enum class SoonSide : std::uint8_t
{
    Right,
    Below,
};

/// Au survol de la dernière entrée (grisée comprise), dit dans quel milestone elle arrive : sans
/// elle, un grisé ne dirait pas pourquoi ni quand il s'éteint.
void saySoon(const char* milestone, SoonSide side);

/// Une entrée grisée, **dessinée comme un bouton grisé** (`disabledButton`) : libellé et raccourci
/// en `TextDisabled` plein. `MenuItem` ouvre un `BeginDisabled` qui multiplie le libellé (`Text`)
/// par `DisabledAlpha` : sur une cible qui mélange en linéaire (la 4070), le libellé grisé gardait
/// 196 contre 215 au texte, et l'entrée ne semblait pas grisée. Le `DisabledAlpha` est lu par
/// `BeginDisabled`, à l'appel (`ImGuiStyleVar_DisabledAlpha`).
void greyedMenuItem(const char* label, const char* shortcut);

/// Le menu « Fenêtre » seul, **dans une barre de menus déjà ouverte** (`BeginMainMenuBar`) : la
/// barre complète (`drawMenuBar`) y met ses cinq menus. Une case cliquée inverse le drapeau de sa
/// fenêtre ; `layout` retient où elle est. Après les cases, « Réinitialiser la disposition » : vrai
/// à l'image où on le clique (le travail est celui de l'appelant, `resetLayout`).
[[nodiscard]] bool drawWindowMenuItems(std::span<const WindowEntry> entries,
                                       WindowMenuLayout& layout);

} // namespace levain::editor
