#include <string_view>

#include <doctest/doctest.h>
#include <imgui.h>
// La fenêtre en cours et les nœuds de la disposition : l'API interne d'ImGui, comme DockBuilder.
#include <imgui_internal.h>

#include "panels.hpp"

#include "levain/core/i18n.hpp"
#include "levain/ui/context.hpp"
#include "levain/ui/tr.hpp"

namespace
{

/// Un anglais de test, installé le temps d'un cas et vidé ensuite, même si un REQUIRE l'interrompt.
struct ScopedEnglish
{
    ScopedEnglish()
    {
        levain::core::activeCatalog().entries = {{"Image", "Picture"},
                                                 {"Passes", "Calls"},
                                                 {"Scène", "Scene"},
                                                 {"{} images/s", "{} frames/s"}};
    }

    ~ScopedEnglish() { levain::core::activeCatalog().entries.clear(); }
};

} // namespace

TEST_CASE("les façades de l'interface lisent le catalogue du processus, le français sans lui")
{
    using namespace levain::ui;
    CHECK(std::string_view{tr("Image")} == "Image");
    CHECK(labelOf("Image") == "Image###Image");
    const ScopedEnglish english;
    CHECK(std::string_view{tr("Image")} == "Picture");
    CHECK(trf("{} images/s", 60) == "60 frames/s");
    CHECK(labelOf("Image") == "Picture###Image");
}

TEST_CASE("une disposition faite en français retrouve ses fenêtres sous un titre anglais")
{
    using namespace levain::app;
    const levain::ui::UiContext context = levain::ui::createUiContext(1.0f);
    levain::ui::prepareUiFrame(ImGui::GetIO(), {.width = 640, .height = 480}, 1.0 / 60.0);
    ImGui::NewFrame();
    const DockNodes nodes = buildLayout(ImHashStr("test"), {640.0f, 480.0f}, true); // le français
    const ScopedEnglish english; // la langue change : les titres, pas les identifiants
    // Ouvrir une fenêtre comme les panneaux du moteur, et lire où elle s'est posée.
    const auto dockOf = [](const char* key)
    {
        ImGui::Begin(levain::ui::labelOf(key).c_str());
        const ImGuiID dock = ImGui::GetCurrentWindow()->DockId;
        ImGui::End();
        return dock;
    };
    const ImGuiID image = dockOf(ImageWindow);
    const ImGuiID passes = dockOf(PassesWindow);
    const ImGuiID scene = dockOf(SceneWindow);
    ImGui::EndFrame();
    // Les trois en onglets du nœud du bas, aucune flottante ni sur un autre nœud.
    CHECK((image != 0 && image == nodes.bottom));
    CHECK(passes == nodes.bottom);
    CHECK(scene == nodes.bottom);
}

TEST_CASE("le programme sans éditeur garde la disposition d'avant : Image et Passes à gauche, "
          "Scène à droite")
{
    using namespace levain::app;
    const levain::ui::UiContext context = levain::ui::createUiContext(1.0f);
    levain::ui::prepareUiFrame(ImGui::GetIO(), {.width = 640, .height = 480}, 1.0 / 60.0);
    ImGui::NewFrame();
    const DockNodes nodes = buildLayout(ImHashStr("plain"), {640.0f, 480.0f}, false);
    const auto dockOf = [](const char* key)
    {
        ImGui::Begin(levain::ui::labelOf(key).c_str());
        const ImGuiID dock = ImGui::GetCurrentWindow()->DockId;
        ImGui::End();
        return dock;
    };
    const ImGuiID image = dockOf(ImageWindow);
    const ImGuiID passes = dockOf(PassesWindow);
    const ImGuiID scene = dockOf(SceneWindow);
    ImGui::EndFrame();
    CHECK((image != 0 && image == nodes.left));
    CHECK((passes != 0 && passes != image));
    CHECK((scene != 0 && scene != image && scene != passes));
    // Le nœud de l'inspecteur, sous Scène, n'a aucune fenêtre ; il n'y a pas de nœud du bas.
    CHECK((nodes.inspector != 0 && nodes.inspector != scene));
    CHECK(nodes.bottom == 0);
}
