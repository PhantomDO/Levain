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

/// **Qui reçoit l'input** (ADR-0036, décision 4). Le programme qui n'est pas un éditeur garde `Ui`.
/// L'éditeur la décide à la fin de l'image N pour l'image N+1 (le survol et le focus ne se savent
/// que dans `FrameHooks::ui`), et la boucle la lit au début de la suivante (`devicesTakenBy`).
enum class InputRoute : std::uint8_t
{
    /// Le filtre de M7.1 : l'UI garde ce que veut ImGui (`WantCaptureMouse` et `…Keyboard`).
    Ui,
    /// Le jeu joue : la souris lui va dans le rectangle de la scène (`App::sceneRect`) de l'image
    /// d'avant, ou capturée ; le clavier lui va, et ImGui n'en reçoit que les relâchements et F1
    /// (`uiEventsOf`) : Ctrl+S ne part pas en jouant.
    Game,
    /// L'éditeur travaille : rien n'arrive au jeu, et ce qu'il tenait est relâché.
    Editor,
};

/// Un rectangle de l'image, en pixels depuis son coin haut gauche : le repère de la souris d'ImGui.
struct ScreenRect
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

/// Le point est dans le rectangle ; le bord haut gauche en est, le bas droit non, comme un pixel.
[[nodiscard]] constexpr bool rectContains(const ScreenRect& rect, float pointX, float pointY)
{
    return pointX >= rect.x && pointX < rect.x + rect.width && pointY >= rect.y &&
           pointY < rect.y + rect.height;
}

/// Les appareils que l'UI garde pour elle à cette image : le jeu n'en voit ni appui ni mouvement.
struct DevicesTaken
{
    bool mouse = false;
    bool keyboard = false;
    bool gamepad = false;
};

/// Ce que `devicesTakenBy` lit de l'image : ce que veut ImGui, et où est la souris.
struct RouteFacts
{
    bool imguiWantsMouse = false;
    bool imguiWantsKeyboard = false;
    bool mouseInScene = false; ///< Le curseur est dans `App::sceneRect`.
    bool mouseCaptured = false;
};

/// Les appareils que l'UI prend selon la route. *Jeu* ne regarde pas `imguiWantsMouse` : la Vue
/// sera une fenêtre d'ImGui, que le survol met à `WantCaptureMouse`, et le jeu ne verrait plus un
/// clic (ADR-0036, option 3B, rejetée pour cela).
[[nodiscard]] constexpr DevicesTaken devicesTakenBy(InputRoute route, const RouteFacts& facts)
{
    switch (route)
    {
    case InputRoute::Game:
        return {.mouse = !facts.mouseInScene && !facts.mouseCaptured,
                .keyboard = false,
                .gamepad = false};
    case InputRoute::Editor:
        return {.mouse = true, .keyboard = true, .gamepad = true};
    case InputRoute::Ui:
        break;
    }
    return {.mouse = facts.imguiWantsMouse, .keyboard = facts.imguiWantsKeyboard, .gamepad = false};
}

/// L'input que voit le jeu quand l'UI prend un appareil (`gameInputOf`) : sans ça, un clic dans une
/// fenêtre tirerait aussi dans la scène, et taper un nombre ferait marcher le renard.
/// - les appuis et les mouvements que l'UI garde sont retirés, **jamais un relâchement** : l'input
///   garde l'état des touches tenues, et un relâchement perdu laisserait une touche enfoncée ;
/// - ce qui était tenu de l'appareil que l'UI prend est relâché ;
/// - F1 (`PanelsKey`) est toujours retirée : c'est la touche de l'UI.
[[nodiscard]] std::vector<platform::InputEvent>
gameInputOf(std::span<const platform::InputEvent> events, const input::RawInput& held,
            DevicesTaken taken);

/// Ce qu'ImGui reçoit des événements de l'image, quand ce n'est pas tous. Sous la route *jeu*, du
/// clavier il n'a que les relâchements et F1 (un relâchement perdu laisserait une touche enfoncée
/// pour lui aussi), et pas le texte tapé : une touche du jeu n'ouvre pas un menu, un Ctrl+S ne
/// part pas. Sous toute autre route, **rien** (`nullopt`) : ImGui reçoit `events` tels quels, sans
/// copie, ce que le sandbox et *Rando* font à chaque image.
[[nodiscard]] std::optional<platform::Events> uiEventsOf(InputRoute route,
                                                         const platform::Events& events);

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
