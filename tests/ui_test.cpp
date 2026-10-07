#include <doctest/doctest.h>
#include <imgui.h>

#include "levain/ui/context.hpp"
#include "levain/ui/ui_pass.hpp"

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

TEST_CASE("la découpe d'ImGui est bornée à l'image, et une découpe vide ne dessine rien")
{
    using levain::ui::clampScissorToTarget;
    const auto inside = clampScissorToTarget(ImVec4(10.5f, 20.0f, 30.2f, 40.0f), 100, 50);
    REQUIRE(inside);
    CHECK(inside.value_or(nvrhi::Rect()).minX == 10);
    CHECK(inside.value_or(nvrhi::Rect()).maxX == 31);
    // Négative et trop grande : bornée aux bords.
    const auto outside = clampScissorToTarget(ImVec4(-50.0f, -50.0f, 500.0f, 500.0f), 100, 50);
    REQUIRE(outside);
    CHECK(outside.value_or(nvrhi::Rect()).minX == 0);
    CHECK(outside.value_or(nvrhi::Rect()).maxY == 50);
    // Hors de l'image, ou d'aire nulle : rien.
    CHECK_FALSE(clampScissorToTarget(ImVec4(120.0f, 0.0f, 140.0f, 10.0f), 100, 50));
    CHECK_FALSE(clampScissorToTarget(ImVec4(10.0f, 10.0f, 10.0f, 20.0f), 100, 50));
}

TEST_CASE("les couleurs d'ImGui ne sont linéarisées que sur une cible sRGB")
{
    CHECK(levain::ui::linearOnSrgbTarget(nvrhi::Format::SBGRA8_UNORM));
    CHECK(levain::ui::linearOnSrgbTarget(nvrhi::Format::SRGBA8_UNORM));
    CHECK_FALSE(levain::ui::linearOnSrgbTarget(nvrhi::Format::BGRA8_UNORM));
    CHECK_FALSE(levain::ui::linearOnSrgbTarget(nvrhi::Format::RGBA8_UNORM));
}
