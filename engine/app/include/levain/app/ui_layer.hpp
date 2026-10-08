#pragma once

// La couche d'interface d'un programme (ADR-0032) : ImGui, ses panneaux de debug, et ce que l'UI
// garde pour elle de l'input. Le module `ui` fait le rendu et la traduction ; ici, la boucle.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "levain/input/state.hpp"
#include "levain/platform/input.hpp"
#include "levain/render/gpu_timer.hpp"
#include "levain/ui/context.hpp"
#include "levain/ui/input.hpp"
#include "levain/ui/ui_pass.hpp"

namespace levain::app
{

/// La touche qui montre et cache les panneaux de debug : F1, par sa position (scancode 58). L'UI la
/// consomme : le jeu ne la voit pas.
inline constexpr std::uint16_t PanelsKey = 58;

/// Ce que coûte l'UI, en millisecondes (le critère de M7.1, ADR-0032). Le CPU est la somme de ses
/// tranches (l'input, `NewFrame`, les fenêtres, `Render`, l'enregistrement de sa passe), pas
/// l'image entière ; le GPU, son minuteur, absent sous WebGPU (« non mesuré », jamais 0).
struct UiCost
{
    double cpuTotalMs = 0.0;
    double cpuMaxMs = 0.0;
    int cpuSamples = 0;
    render::GpuTimeAverage gpu{};
    double gpuMaxMs = 0.0;
};

/// Les dernières images, pour les courbes du panneau « Image ».
inline constexpr std::size_t HistoryLength = 240;

struct FrameHistory
{
    std::array<float, HistoryLength> frameMs{};
    std::array<float, HistoryLength> gpuMs{};
    std::size_t next = 0;
};

/// Les nœuds de la disposition des panneaux (`drawEnginePanels`), où l'éditeur ancre ses fenêtres
/// (ADR-0034) : à gauche celui d'« Image », à droite, sous « Scène », celui de l'inspecteur : à lui
/// seul, il est toujours visible. Nuls tant que les panneaux n'ont jamais été ouverts.
struct DockNodes
{
    ImGuiID left = 0;
    ImGuiID inspector = 0;
};

/// L'UI d'un programme. Elle tient des ressources du GPU : `App` la déclare avant ses points
/// d'accroche, et la détruit avant le device (ADR-0029).
struct UiLayer
{
    // Dans cet ordre : la passe et ses textures partent avant le contexte d'ImGui.
    ui::UiContext context{};
    ui::UiPass pass{};
    render::GpuTimer timer{};
    /// Les panneaux de debug du moteur, ouverts par F1 ou `--ui on`.
    bool panelsOpen = false;
    DockNodes dock{};
    bool textInputActive = false;
    /// Les touches d'ImGui envoyées à l'appui, à relâcher à l'identique (`feedInput`).
    ui::PressedKeys pressedKeys{};
    /// ImGui a fini une image (`ImGui::Render`), que le rendu doit dessiner.
    bool frameReady = false;
    UiCost cost{};
    /// Le temps CPU de l'UI dans l'image en cours, tranche par tranche.
    double frameCpuMs = 0.0;
    ui::UiDrawStats lastDraw{};
    std::uint64_t totalDraws = 0;
    FrameHistory history{};
};

/// L'input que voit le jeu quand l'UI prend la souris ou le clavier (`gameInputOf`) : sans ça, un
/// clic dans une fenêtre tirerait aussi dans la scène, et taper un nombre ferait marcher le renard.
/// - les appuis et les mouvements que l'UI garde sont retirés, **jamais un relâchement** : l'input
///   garde l'état des touches tenues, et un relâchement perdu laisserait une touche enfoncée ;
/// - ce qui était tenu de l'appareil que l'UI prend est relâché ;
/// - F1 (`PanelsKey`) est toujours retirée : c'est la touche de l'UI.
[[nodiscard]] std::vector<platform::InputEvent>
gameInputOf(std::span<const platform::InputEvent> events, const input::RawInput& held,
            bool uiTakesMouse, bool uiTakesKeyboard);

/// La souris capturée : le programme la veut, et les panneaux ne sont pas ouverts. `App` possède
/// la capture (ADR-0032) ; le programme pose `mouseCaptureWanted` et lit `mouseCaptured`.
[[nodiscard]] constexpr bool mouseShouldBeCaptured(bool wanted, bool panelsOpen)
{
    return wanted && !panelsOpen;
}

/// Ajoute une image à l'historique des courbes. Une image sans mesure GPU (pas encore relue, ou
/// sous WebGPU) garde la précédente : la courbe ne tombe pas à 0, qui serait une mesure.
void recordHistory(FrameHistory& history, float frameMs, std::optional<float> gpuMs);

/// Ajoute le temps CPU d'une image d'UI à sa moyenne et à son pire.
void recordUiCpu(UiCost& cost, double milliseconds);

} // namespace levain::app
