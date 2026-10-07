#pragma once

// L'input de `platform` traduit pour ImGui (ADR-0032). Ce module n'inclut pas SDL : les codes de
// touche sont des nombres, ceux que `platform` donne (`UiEvent`).

#include <array>
#include <cstdint>

#include <imgui.h>

#include "levain/platform/window.hpp"

namespace levain::ui
{

/// La touche d'ImGui pour une touche du clavier. **ImGui attend des touches traduites** :
/// `ImGuiKey_A` est la touche qui tape « A », selon la disposition du clavier. La traduction part
/// donc de `keycode`, comme le backend SDL3 d'ImGui (`imgui_impl_sdl3.cpp`, MIT) dont cette table
/// est reprise : sur un AZERTY, Ctrl+Z est bien la touche marquée Z. Le pavé numérique, que SDL ne
/// distingue pas par keycode, et quelques signes de ponctuation partent du scancode.
[[nodiscard]] ImGuiKey imguiKeyOf(std::uint32_t keycode, std::uint16_t scancode);

/// La touche d'ImGui envoyée à l'appui de chaque scancode, pour envoyer la même au relâchement.
/// Dans le navigateur, SDL donne le keycode **modifié** (`KeyboardEvent.key`) : appuyer sur A, puis
/// Maj, puis relâcher A donnerait « a » à l'appui et « A » au relâchement, et `ImGuiKey_A`
/// resterait enfoncée jusqu'à la perte du focus (`releaseAsPressed`).
using PressedKeys = std::array<ImGuiKey, platform::KeyCodeCount>;

/// Donne à ImGui les événements de l'image : la souris, la molette, les touches avec leurs
/// modificateurs, le texte tapé, la sortie de la fenêtre et le focus. ImGui les met en file ; c'est
/// `ImGui::NewFrame` qui les traite. Le texte arrive après les touches de l'image : taper « a »,
/// Retour arrière puis « b » dans la même image donnerait « ab ». À 60 images/s, c'est trop rapide
/// pour une main ; à régler si un champ de texte le montre.
void feedInput(ImGuiIO& io, const platform::Events& events, PressedKeys& pressed);

} // namespace levain::ui
