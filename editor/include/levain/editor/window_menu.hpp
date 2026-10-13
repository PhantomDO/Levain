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

/// Le menu « Fenêtre » seul, **dans une barre de menus déjà ouverte** (`BeginMainMenuBar`) : la
/// barre complète (`drawMenuBar`) y met ses cinq menus. Une case cliquée inverse le drapeau de sa
/// fenêtre ; `layout` retient où elle est.
void drawWindowMenuItems(std::span<const WindowEntry> entries, WindowMenuLayout& layout);

/// Dessine une barre de menus qui n'a que le menu « Fenêtre » (`drawWindowMenuItems`) : celle que
/// `withEditor` dessine tant qu'il n'a pas la barre complète (`drawMenuBar`). Panneaux ouverts
/// seulement : la barre retire sa hauteur à l'espace d'ancrage, de l'image suivante.
void drawWindowMenu(std::span<const WindowEntry> entries, WindowMenuLayout& layout);

} // namespace levain::editor
