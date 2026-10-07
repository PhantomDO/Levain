#include "levain/ui/context.hpp"

#include <string>

#include "levain/core/assert.hpp"
#include "levain/platform/input.hpp"

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

/// Les drapeaux d'ImGui sont des `int` : les combiner en non signé, comme des bits qu'ils sont.
int withFlags(int flags, int added)
{
    return static_cast<int>(static_cast<unsigned>(flags) | static_cast<unsigned>(added));
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
