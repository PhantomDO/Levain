#pragma once

// La console de l'éditeur (ADR-0036, décision 16) : le journal du moteur, filtré par niveau,
// compté, cherché, réduit. Son modèle (ce qu'on montre) est en fonctions libres sur des copies de
// l'anneau de `core` (core/log_ring.hpp), sans ImGui : les tests le lisent. Équivalents : la
// Console d'Unity (filtres, compteurs, recherche, Collapse), l'Output Log d'Unreal, le panneau
// Sortie de Godot.

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "levain/core/log_ring.hpp"

namespace levain::editor
{

/// La clé de catalogue du titre de la fenêtre.
inline constexpr const char* ConsoleWindow = "Console";

/// Combien de messages la fenêtre garde : de quoi remonter un chargement, sans que l'éditeur lise
/// un journal sans fin.
inline constexpr std::size_t ConsoleCapacity = 2000;

/// Ce que la console montre : un niveau coché ou non, et un texte cherché (vide : tout).
struct ConsoleFilter
{
    bool info = true;
    bool warning = true;
    bool error = true;
    std::string_view search;
};

/// Vrai si `needle` est dans `text`, sans tenir compte de la casse des lettres ASCII. Les accents
/// comptent tels quels : plier la casse de l'UTF-8 demanderait une table Unicode, que ni la
/// recherche d'Unity ni ce premier jet n'ont.
[[nodiscard]] bool containsIgnoringAsciiCase(std::string_view text, std::string_view needle);

/// Le niveau est coché, et la recherche est dans la catégorie ou le texte (« gpu » trouve les
/// messages de la catégorie `gpu`, comme le filtre de l'Output Log d'Unreal).
[[nodiscard]] bool isShown(const core::LogEntry& entry, const ConsoleFilter& filter);

/// Une ligne de la console : l'entrée qu'elle montre, et combien de fois d'affilée elle s'est
/// répétée (1 sans réduction).
struct ConsoleRow
{
    std::size_t entry = 0;
    std::size_t repeats = 1;
};

/// Vide `rows`, puis y range une ligne par entrée montrée, de la plus ancienne à la plus récente.
/// Avec `collapse` (Réduire), des entrées identiques *qui se suivent parmi celles qui sont
/// montrées* ne font qu'une ligne et un compte : cacher les infos qui séparaient deux erreurs
/// identiques les réunit. Même niveau, même catégorie, même texte. Sa mémoire ressert d'une image à
/// l'autre.
void listConsoleRows(std::span<const core::LogEntry> entries, const ConsoleFilter& filter,
                     bool collapse, std::vector<ConsoleRow>& rows);

/// Faut-il suivre le bout de la liste : seulement si elle y est déjà (à un pixel près : le
/// défilement d'un écran à l'échelle fractionnaire n'est pas entier). Remontée, la liste ne bouge
/// plus quand un message arrive ; redescendue, elle suit de nouveau.
[[nodiscard]] bool shouldFollowTail(float scrollY, float scrollMax);

/// Les libellés des trois cases, dans l'ordre info, avertissement, erreur, chacun avec son compte
/// (« Erreurs : 3 »). Le compte est celui de tous les messages reçus, pas des lignes de la liste.
[[nodiscard]] std::array<std::string, 3> filterLabels(const core::LogCounts& counts);

/// Un élément de largeur `itemWidth` tient-il sur la ligne où le précédent s'arrête à
/// `previousEnd`, une fois l'espace `spacing` passé, sans dépasser le bord droit `rightEdge` ?
/// Sinon la barre d'outils va à la ligne : dans la colonne étroite où la console s'ancre pour
/// l'instant (24 % de la fenêtre), une barre sur une seule ligne perdrait ses dernières cases hors
/// du cadre.
[[nodiscard]] bool fitsOnLine(float previousEnd, float spacing, float itemWidth, float rightEdge);

/// Le message sur une seule ligne : les retours à la ligne deviennent « | » (une ligne par message,
/// la liste en suppose la hauteur fixe pour ne dessiner que ce qu'on voit).
[[nodiscard]] std::string singleLine(std::string_view text);

/// Le texte d'une ligne : le temps, la catégorie, le message sur une ligne (`singleLine`), et «
/// (×N) » après N répétitions réduites.
[[nodiscard]] std::string lineOf(const core::LogEntry& entry, std::size_t repeats);

/// Où les éléments de la fenêtre se sont dessinés à la dernière image (leur centre, en pixels de
/// l'écran) : les tests y cliquent, sans recalculer la mise en page d'ImGui.
struct ConsoleLayout
{
    ImVec2 info{};
    ImVec2 warning{};
    ImVec2 error{};
    ImVec2 collapse{};
    ImVec2 clear{};
    ImVec2 search{};
    ImVec2 list{};
    /// La largeur du champ de recherche : il garde la sienne dans une colonne étroite, au lieu de
    /// se réduire à un pixel (ImGui lit une largeur négative comme « jusqu'au bord droit »).
    float searchWidth = 0.0f;
};

/// Ce que la fenêtre garde d'une image à l'autre.
struct Console
{
    bool info = true;
    bool warning = true;
    bool error = true;
    bool collapse = false;
    /// Le texte cherché, terminé par un zéro (c'est le tampon du champ d'ImGui).
    std::array<char, 128> search{};
    /// La dernière copie de l'anneau : recopiée seulement quand sa génération change.
    core::LogSnapshot snapshot;
    /// Les lignes montrées à la dernière image.
    std::vector<ConsoleRow> rows;
    /// Les libellés que les trois cases ont portés à la dernière image (`filterLabels`) : ce que
    /// l'utilisateur lit, que les tests comparent aux comptes.
    std::array<std::string, 3> labels;
    /// Le défilement de la liste lu à la dernière image, avant qu'elle ne le règle.
    float scrollY = 0.0f;
    float scrollMax = 0.0f;
    ConsoleLayout layout;
    /// La police des lignes (ProggyForever à chasse fixe, ADR-0036 décision 15) ; nul : celle de la
    /// fenêtre.
    ImFont* font = nullptr;
};

/// La fenêtre « Console », ancrée au premier affichage au nœud `dock`. Le bouton Effacer vide
/// `ring`.
void drawConsole(Console& console, core::LogRing& ring, ImGuiID dock, bool* open = nullptr);

} // namespace levain::editor
