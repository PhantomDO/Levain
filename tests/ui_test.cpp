#include <doctest/doctest.h>
#include <imgui.h>

#include "levain/ui/context.hpp"

namespace
{

/// Un drapeau d'ImGui posé : ses drapeaux sont des `int`, lus en non signé.
bool hasFlag(int flags, int flag)
{
    return (static_cast<unsigned>(flags) & static_cast<unsigned>(flag)) != 0;
}

} // namespace

TEST_CASE("le contexte est réglé pour le moteur : ancrage, pas d'imgui.ini, textures, échelle")
{
    const levain::ui::UiContext context = levain::ui::createUiContext(2.0f);
    const ImGuiIO& io = ImGui::GetIO();
    CHECK(io.IniFilename == nullptr);
    CHECK(hasFlag(io.ConfigFlags, ImGuiConfigFlags_DockingEnable));
    CHECK(hasFlag(io.BackendFlags, ImGuiBackendFlags_RendererHasTextures));
    CHECK(hasFlag(io.BackendFlags, ImGuiBackendFlags_RendererHasVtxOffset));
    CHECK(ImGui::GetStyle().FontScaleDpi == 2.0f);
}
