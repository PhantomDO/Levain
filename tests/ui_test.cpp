#include <string_view>

#include <doctest/doctest.h>
#include <imgui.h>

#include "levain/ui/context.hpp"
#include "levain/ui/input.hpp"
#include "levain/ui/ui_pass.hpp"

using levain::platform::UiEvent;
using levain::platform::UiEventType;

namespace
{

/// Les `SDLK_` d'une touche qui ne tape rien : son scancode, bit 30 posé.
constexpr std::uint32_t special(std::uint32_t scancode)
{
    return scancode | (1U << 30U);
}

/// Un drapeau d'ImGui posé : ses drapeaux sont des `int`, lus en non signé.
bool hasFlag(int flags, int flag)
{
    return (static_cast<unsigned>(flags) & static_cast<unsigned>(flag)) != 0;
}

/// Des images d'ImGui, juste pour qu'il traite les événements en file. ImGui les étale sur
/// plusieurs images quand il en arrive beaucoup à la fois (`ConfigInputTrickleEventQueue`) : un
/// clic rapide reste visible au moins une image. Quatre suffisent ici.
void framesWith(const levain::platform::Events& events, levain::ui::PressedKeys& pressed)
{
    ImGuiIO& io = ImGui::GetIO();
    levain::ui::feedInput(io, events, pressed);
    for (int frame = 0; frame < 4; ++frame)
    {
        levain::ui::prepareUiFrame(io, {.width = 640, .height = 480}, 1.0 / 60.0);
        ImGui::NewFrame();
        ImGui::EndFrame();
    }
}

} // namespace

TEST_CASE("ImGui reçoit la touche selon la disposition du clavier, pas sa position")
{
    // Sur un AZERTY, la touche marquée A est à la place du Q d'un QWERTY (scancode 20) : c'est A.
    CHECK(levain::ui::imguiKeyOf('a', 20) == ImGuiKey_A);
    CHECK(levain::ui::imguiKeyOf('z', 26) == ImGuiKey_Z);
    // Les touches qui ne tapent rien : leur scancode, bit 30 posé.
    CHECK(levain::ui::imguiKeyOf(special(58), 58) == ImGuiKey_F1);
    CHECK(levain::ui::imguiKeyOf(special(115), 115) == ImGuiKey_F24);
    CHECK(levain::ui::imguiKeyOf(special(80), 80) == ImGuiKey_LeftArrow);
    CHECK(levain::ui::imguiKeyOf('\r', 40) == ImGuiKey_Enter);
    // Le pavé numérique, par position : son keycode est celui du chiffre.
    CHECK(levain::ui::imguiKeyOf('1', 89) == ImGuiKey_Keypad1);
    // Une touche inconnue : rien, plutôt qu'une touche au hasard.
    CHECK(levain::ui::imguiKeyOf(special(300), 300) == ImGuiKey_None);
}

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

TEST_CASE("la souris, les touches et le texte arrivent à ImGui")
{
    const levain::ui::UiContext context = levain::ui::createUiContext(1.0f);
    levain::ui::PressedKeys pressed{};
    levain::platform::Events events;
    events.ui.push_back({.type = UiEventType::MouseMoved, .x = 120.0f, .y = 40.0f});
    events.ui.push_back({.type = UiEventType::MouseButton, .button = 3, .down = true});
    events.ui.push_back({.type = UiEventType::Key,
                         .keycode = 'z',
                         .scancode = 26,
                         .down = true,
                         .modifiers = {.ctrl = true}});
    framesWith(events, pressed);
    const ImGuiIO& io = ImGui::GetIO();
    CHECK(io.MousePos.x == 120.0f);
    CHECK(io.MousePos.y == 40.0f);
    CHECK(ImGui::IsMouseDown(ImGuiMouseButton_Right)); // le bouton 3 de SDL
    CHECK(ImGui::IsKeyDown(ImGuiKey_Z));
    CHECK(io.KeyCtrl);
    // Le texte tapé, en UTF-8 : ImGui le donne à l'image suivante, et le vide à sa fin.
    levain::platform::Events typed;
    typed.text = "é";
    levain::ui::feedInput(ImGui::GetIO(), typed, pressed);
    levain::ui::prepareUiFrame(ImGui::GetIO(), {.width = 640, .height = 480}, 1.0 / 60.0);
    ImGui::NewFrame();
    REQUIRE(io.InputQueueCharacters.Size == 1);
    CHECK(io.InputQueueCharacters[0] == 0xE9);
    ImGui::EndFrame();
    // La souris quitte la fenêtre : plus rien n'est survolé.
    levain::platform::Events left;
    left.ui.push_back({.type = UiEventType::MouseLeft});
    framesWith(left, pressed);
    CHECK_FALSE(ImGui::IsMousePosValid());
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

TEST_CASE("une touche relâchée sous Maj, dans le navigateur, est bien relâchée pour ImGui")
{
    // Le navigateur donne le keycode modifié : « a » à l'appui, « A » au relâchement si Maj est
    // venue entre les deux. ImGui doit voir A appuyée, puis relâchée, et non enfoncée pour
    // toujours.
    const levain::ui::UiContext context = levain::ui::createUiContext(1.0f);
    levain::ui::PressedKeys pressed{};
    levain::platform::Events down;
    down.ui.push_back({.type = UiEventType::Key, .keycode = 'a', .scancode = 4, .down = true});
    framesWith(down, pressed);
    CHECK(ImGui::IsKeyDown(ImGuiKey_A));
    levain::platform::Events up;
    up.ui.push_back({.type = UiEventType::Key,
                     .keycode = 'A',
                     .scancode = 4,
                     .down = false,
                     .modifiers = {.shift = true}});
    framesWith(up, pressed);
    CHECK_FALSE(ImGui::IsKeyDown(ImGuiKey_A));
    // Et une majuscule seule, sous Maj : la touche A.
    CHECK(levain::ui::imguiKeyOf('A', 4) == ImGuiKey_A);
}

TEST_CASE("l'UI ne montre qu'une texture 2D, mono-échantillon, de couleur")
{
    // Les refus de `registerUiTexture` (ui_pass.hpp) : son shader lit un Texture2D de flottants.
    // Le test GPU (levain_ui_gpu) montre les textures acceptées.
    const auto colorTexture = nvrhi::TextureDesc().setFormat(nvrhi::Format::SRGBA8_UNORM);
    CHECK(levain::ui::uiTextureRefusal(colorTexture).empty());
    CHECK(levain::ui::uiTextureRefusal(
              nvrhi::TextureDesc(colorTexture).setFormat(nvrhi::Format::RGBA16_FLOAT))
              .empty());
    CHECK_FALSE(
        levain::ui::uiTextureRefusal(
            nvrhi::TextureDesc(colorTexture).setDimension(nvrhi::TextureDimension::TextureCube))
            .empty());
    // Une texture multi-échantillon de NVRHI est une `Texture2DMS` : c'est le nombre
    // d'échantillons qui la refuse, avec son propre message.
    const auto multisampled = nvrhi::TextureDesc(colorTexture)
                                  .setDimension(nvrhi::TextureDimension::Texture2DMS)
                                  .setSampleCount(4);
    CHECK(levain::ui::uiTextureRefusal(multisampled).find("multi-échantillon") !=
          std::string_view::npos);
    CHECK_FALSE(
        levain::ui::uiTextureRefusal(nvrhi::TextureDesc(colorTexture).setFormat(nvrhi::Format::D32))
            .empty());
    CHECK_FALSE(levain::ui::uiTextureRefusal(
                    nvrhi::TextureDesc(colorTexture).setFormat(nvrhi::Format::D24S8))
                    .empty());
    // Un format entier ne se lit pas par un `Texture2D<float4>` ; une texture sans usage de shader
    // n'a ni échantillonnage ni SRV ; un flottant sur 32 bits ne se filtre pas sous WebGPU.
    for (const nvrhi::Format format :
         {nvrhi::Format::RGBA8_UINT, nvrhi::Format::R32_UINT, nvrhi::Format::RGBA8_SINT,
          nvrhi::Format::R32_FLOAT, nvrhi::Format::RG32_FLOAT, nvrhi::Format::RGB32_FLOAT,
          nvrhi::Format::RGBA32_FLOAT})
    {
        CAPTURE(nvrhi::getFormatInfo(format).name);
        CHECK_FALSE(levain::ui::uiTextureRefusal(nvrhi::TextureDesc(colorTexture).setFormat(format))
                        .empty());
    }
    nvrhi::TextureDesc unreadable = colorTexture;
    unreadable.isShaderResource = false; // NVRHI n'a pas de setter pour ce champ
    CHECK_FALSE(levain::ui::uiTextureRefusal(unreadable).empty());
    // Les normalisés, eux, se filtrent partout.
    CHECK(levain::ui::uiTextureRefusal(
              nvrhi::TextureDesc(colorTexture).setFormat(nvrhi::Format::RGBA8_UNORM))
              .empty());
}
