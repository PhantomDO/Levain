#include "levain/ui/context.hpp"

#include <string>

#include "levain/core/assert.hpp"
#include "levain/platform/input.hpp"
#include "levain/ui/flags.hpp"

namespace levain::ui
{

namespace
{

/// ImGui garde le pointeur rendu par la lecture du presse-papiers jusqu'à la suivante : la copie
/// doit vivre jusque-là.
std::string clipboardCopy;

const char* clipboardOf(ImGuiContext*)
{
    clipboardCopy = platform::clipboardText();
    return clipboardCopy.c_str();
}

void setClipboardOf(ImGuiContext*, const char* text)
{
    platform::setClipboardText(text != nullptr ? text : "");
}

} // namespace

void ContextDeleter::operator()(ImGuiContext* context) const noexcept
{
    ImGui::DestroyContext(context);
}

UiContext createUiContext(float displayScale)
{
    IMGUI_CHECKVERSION();
    // Un seul à la fois : avec un contexte déjà courant, `CreateContext` le laisse courant, et la
    // suite réglerait le mauvais.
    LEVAIN_ASSERT(ImGui::GetCurrentContext() == nullptr, "un seul contexte d'ImGui à la fois");
    UiContext context{ImGui::CreateContext()};
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags = withFlags(io.ConfigFlags, ImGuiConfigFlags_DockingEnable);
    io.BackendRendererName = "levain::ui";
    io.BackendPlatformName = "levain::platform";
    io.BackendFlags = withFlags(io.BackendFlags, ImGuiBackendFlags_RendererHasTextures);
    io.BackendFlags = withFlags(io.BackendFlags, ImGuiBackendFlags_RendererHasVtxOffset);
    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    platformIo.Platform_GetClipboardTextFn = clipboardOf;
    platformIo.Platform_SetClipboardTextFn = setClipboardOf;
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(displayScale);
    style.FontScaleDpi = displayScale;
    // Fonds opaques. Les 94 % de l'alpha par défaut d'ImGui sont pensés pour un mélange en espace
    // sRGB. Avant le morceau 17d, notre interface se mélangeait en linéaire sur une cible sRGB, où
    // 94 % se lisait autour de 73 % : un menu laissait voir ce qui est dessous. Elle se mélange
    // maintenant en gamma (cible UNORM, ADR-0036 décision 15), mais le choix reste : il protège
    // aussi une future cible sRGB. ChildBg reste transparent : un enfant se pose sur le fond de sa
    // fenêtre.
    for (const ImGuiCol color : {ImGuiCol_WindowBg, ImGuiCol_PopupBg, ImGuiCol_MenuBarBg})
    {
        style.Colors[color].w = 1.0f;
    }
    return context;
}

void prepareUiFrame(ImGuiIO& io, platform::PixelSize size, double deltaSeconds)
{
    io.DisplaySize = ImVec2(static_cast<float>(size.width), static_cast<float>(size.height));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    // ImGui refuse un pas nul : la première image, ou deux images dans la même microseconde.
    io.DeltaTime = deltaSeconds > 0.0 ? static_cast<float>(deltaSeconds) : 1.0f / 60.0f;
}

void followTextInput(const platform::Window& window, bool wanted, bool& active)
{
    if (wanted == active)
    {
        return;
    }
    if (wanted)
    {
        platform::startTextInput(window);
    }
    else
    {
        platform::stopTextInput(window);
    }
    active = wanted;
}

} // namespace levain::ui
