#pragma once

// Fermer l'éditeur (ADR-0036, décision 11) : la croix de la fenêtre ne ferme pas, elle ouvre la
// modale « Quitter l'éditeur ? » (Quitter / Annuler ; M7.3 y ajoutera « Enregistrer »). La logique
// est en fonctions libres dont les dépendances sont des paramètres (ADR-0011) ; `withEditor` n'en a
// que la glu. Un signal (Ctrl+C, SIGTERM) quitte sans demander, et une exécution scriptée se ferme
// tout de suite : l'un comme l'autre sont décidés par `app`, avant d'arriver ici
// (`FrameHooks::closeRequested`). Équivalents : la boîte « Enregistrer les modifications ? »
// d'Unity et d'Unreal ; Godot ouvre la même question sur sa fenêtre principale.

#include <cstdint>

#include "levain/app/ui_layer.hpp"

namespace levain::editor
{

struct QuitState
{
    /// La croix a été pressée et la modale n'est pas encore ouverte : `ui` l'ouvre.
    bool asked = false;
    /// La modale est ouverte.
    bool open = false;
    /// Combien de fois elle s'est ouverte : un test y lit « une seule, jamais empilée ».
    int timesOpened = 0;
    /// Les boutons tels que la dernière image les a dessinés (vides modale fermée), pour que le
    /// banc d'essai les clique à la souris scriptée.
    app::ScreenRect quitButton;
    app::ScreenRect cancelButton;
};

enum class QuitAnswer : std::uint8_t
{
    Pending, ///< la modale est fermée, ou attend encore
    Quit,
    Cancel,
};

/// **Piège : une deuxième croix pendant que la modale est ouverte** n'en empile pas une autre, et
/// n'en prépare pas une pour après sa fermeture. Seule une croix reçue modale fermée la demande.
void askToQuit(QuitState& state);

/// La modale doit s'ouvrir à cette image : demandée, et pas déjà ouverte. Une seule par image, par
/// construction (un seul état, une seule demande consommée).
[[nodiscard]] constexpr bool shouldOpenQuitModal(const QuitState& state)
{
    return state.asked && !state.open;
}

/// **Piège : la modale est au centre de la fenêtre, donc dans la scène.** Tant qu'elle est ouverte,
/// la route est celle de l'UI, quelle que soit celle du mode : sans cela un clic sur Quitter ou
/// Annuler, et le clavier, atteindraient aussi le jeu.
[[nodiscard]] constexpr app::InputRoute inputRouteWhileQuit(const QuitState& state,
                                                            app::InputRoute route)
{
    return state.open ? app::InputRoute::Ui : route;
}

/// **Piège : un raccourci global (Alt+P) ne doit pas jouer sous la modale.** ImGui le laisse passer
/// (`ImGuiInputFlags_RouteGlobal`), la modale ne le bloque pas.
[[nodiscard]] constexpr bool globalChordsAllowed(const QuitState& state)
{
    return !state.open;
}

/// Dessine la modale quand elle est demandée ou ouverte, dans `FrameHooks::ui`. Rend la réponse de
/// l'image : `Quit` pour quitter, `Cancel` pour garder l'éditeur (le bouton, ou Échap). À appeler
/// **avant** la route et les raccourcis de l'image, qui lisent `open`.
[[nodiscard]] QuitAnswer drawQuitModal(QuitState& state);

} // namespace levain::editor
