#pragma once

#include <cstdint>
#include <vector>

#include "levain/platform/input.hpp"
#include "levain/platform/window.hpp"

// En-tête privé du module : il passe des types SDL entre `window.cpp`, qui pompe les événements,
// et `input.cpp`, qui les traduit. Aucun en-tête SDL ne sort de `platform/` (ADR-0003).
union SDL_Event;

namespace levain::platform
{

/// Ajoute à `events` ce que cet événement SDL dit du clavier, de la souris ou d'une manette, et
/// ouvre ou referme les manettes branchées à chaud. Ignore tout le reste.
void appendInputEvent(std::vector<InputEvent>& events, const SDL_Event& event);

/// Ajoute à `events` ce que cet événement SDL dit à une interface (ADR-0032) : la position de la
/// souris, ses boutons et sa molette, les touches avec leur code de disposition, le texte tapé, la
/// sortie de la fenêtre et le focus. Ignore tout le reste. Seuls la sortie et le focus sont filtrés
/// sur `windowId` : la souris et le clavier n'ont qu'une fenêtre, celle de `platform`.
void appendUiEvent(Events& events, const SDL_Event& event, std::uint32_t windowId);

/// Referme la manette d'identifiant `instanceId`, si elle était ouverte.
void closeGamepad(std::uint32_t instanceId);

/// Referme toutes les manettes ouvertes, avant `SDL_Quit`.
void closeAllGamepads();

} // namespace levain::platform
