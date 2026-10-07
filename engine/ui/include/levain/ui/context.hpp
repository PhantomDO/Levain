#pragma once

// Le contexte d'ImGui (ADR-0032) : global chez ImGui, donc un seul à la fois, comme la fenêtre de
// `platform`. Le créer règle ce que le moteur attend de lui ; le détruire libère ses fenêtres.

#include <memory>

#include <imgui.h>

#include "levain/platform/window.hpp"

namespace levain::ui
{

struct ContextDeleter
{
    void operator()(ImGuiContext* context) const noexcept;
};

using UiContext = std::unique_ptr<ImGuiContext, ContextDeleter>;

/// Crée le contexte d'ImGui, et le règle :
/// - les fenêtres ancrées (branche docking), sans fenêtres hors de la fenêtre principale ;
/// - **sans `imgui.ini`** : ImGui l'écrirait dans le dossier courant, et une session changerait
///   l'image de la suivante ;
/// - notre renderer gère les textures (ImGui 1.92) et les décalages de sommets ;
/// - le presse-papiers du système, par `platform` ;
/// - la police et le style à l'échelle de l'écran (`displayScale`) : sur un téléphone ×3, l'UI
///   serait sinon minuscule.
[[nodiscard]] UiContext createUiContext(float displayScale);

/// Ce qu'ImGui doit savoir avant `NewFrame` : la taille de l'image, en pixels (la souris l'est
/// aussi, `platform::UiEvent`), et le temps écoulé.
void prepareUiFrame(ImGuiIO& io, platform::PixelSize size, double deltaSeconds);

/// Ouvre ou ferme la saisie de texte selon ce que veut ImGui (`WantTextInput`) : le texte tapé
/// n'arrive qu'entre `startTextInput` et `stopTextInput`, et un champ de texte ne recevrait rien
/// sans erreur. `active` garde l'état d'une image à l'autre.
void followTextInput(const platform::Window& window, bool wanted, bool& active);

} // namespace levain::ui
