#pragma once

// Le menu « Fenêtre » de la barre de menus principale (ADR-0036, décision 10) : une case par
// fenêtre, cochée tant qu'elle est ouverte, qui la ferme ou la rouvre. Les menus complets (Fichier,
// Édition, Entité, Aide) sont du morceau 12 ; la barre n'a pour l'instant que celui-ci. Équivalents
// : Window d'Unity et d'Unreal, le menu Éditeur > Disposition de Godot.

#include <array>
#include <cstddef>
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

/// Dessine la barre de menus et son menu « Fenêtre ». Une case cliquée inverse le drapeau de sa
/// fenêtre. Panneaux ouverts seulement : la barre retire sa hauteur à l'espace d'ancrage, de
/// l'image suivante.
void drawWindowMenu(std::span<const WindowEntry> entries, WindowMenuLayout& layout);

} // namespace levain::editor
