#include "levain/editor/edit_gesture.hpp"

#include <utility>

#include "levain/scene/reflection.hpp"

namespace levain::editor
{

namespace
{

/// La valeur du composant dans le monde maintenant ; rien si l'entité ou le composant n'y est plus.
std::optional<scene::ValueBox> worldValueOf(const flecs::world& world, flecs::entity_t entity,
                                            flecs::entity_t component)
{
    if (!ecs_is_alive(world, entity) || !ecs_has_id(world, entity, component))
    {
        return std::nullopt;
    }
    const flecs::entity_t type = ecs_get_typeid(world, component);
    const void* value = ecs_get_id(world, entity, component);
    if (type == 0 || value == nullptr)
    {
        return std::nullopt;
    }
    return scene::ValueBox{world, type, value};
}

/// Pose la commande `before` → valeur du monde ; 1 si elle est posée, 0 si l'entité est partie
/// ou si avant et après sont égaux (`recordValueChange`).
int postChange(UndoStack& stack, flecs::world& world, Mode mode, flecs::entity_t entity,
               flecs::entity_t component, scene::ValueBox before)
{
    auto after = worldValueOf(world, entity, component);
    if (!after)
    {
        return 0; // l'entité est détruite : une commande sur elle ne s'appliquerait plus
    }
    return recordValueChange(stack, world, mode, entity, component, std::move(before),
                             std::move(*after))
               ? 1
               : 0;
}

bool isOpenOn(const EditGesture& gesture, const ComponentFrame& frame)
{
    return gesture.open && gesture.open->entity == frame.entity &&
           gesture.open->component == frame.component;
}

/// Ferme le geste ouvert et pose sa commande. Si le glissé continue (Play refusé, entité
/// changée), ses écritures suivantes rouvrent un geste (étape 4 de `settleGestures`) : rien n'est
/// perdu, le glissé fait deux commandes.
int closeGesture(EditGesture& gesture, UndoStack& stack, flecs::world& world, Mode mode)
{
    if (!gesture.open)
    {
        return 0;
    }
    OpenGesture closing = std::move(*gesture.open);
    gesture.open.reset();
    return postChange(stack, world, mode, closing.entity, closing.component,
                      std::move(closing.before));
}

/// Une écriture que personne n'a ouverte alors qu'un élément est actif : la suite d'un glissé dont
/// le geste a été fermé de force. L'avant est celui du début de cette image.
bool reopensGesture(const EditGesture& gesture, const ComponentFrame& frame, std::uint32_t activeId)
{
    return activeId != 0 && frame.wrote && !frame.settled && !frame.edges.activated &&
           frame.before.has_value() && !isOpenOn(gesture, frame);
}

/// Annule le geste ouvert : Échap. ImGui a rejoué le texte qu'il avait formaté à l'activation
/// (« 0.123 » pour 0.12345678f) et l'a appliqué comme un changement ; le monde n'est donc pas
/// revenu à l'avant. On y remet **l'avant exact** du geste, et aucune commande ne se pose.
void cancelGesture(EditGesture& gesture, flecs::world& world)
{
    if (!gesture.open)
    {
        return;
    }
    OpenGesture cancelled = std::move(*gesture.open);
    gesture.open.reset();
    if (ecs_is_alive(world, cancelled.entity) &&
        ecs_has_id(world, cancelled.entity, cancelled.component))
    {
        scene::setComponentValue(world, cancelled.entity, cancelled.component,
                                 cancelled.before.data());
    }
}

/// Marque comme prises en compte les images de ce composant (le geste vient de se fermer).
void settleFramesOf(std::vector<ComponentFrame>& frames, flecs::entity_t entity,
                    flecs::entity_t component)
{
    for (ComponentFrame& frame : frames)
    {
        if (frame.entity == entity && frame.component == component)
        {
            frame.settled = true;
        }
    }
}

} // namespace

int closeOpenGesture(EditGesture& gesture, UndoStack& stack, flecs::world& world, Mode mode)
{
    return closeGesture(gesture, stack, world, mode);
}

int settleGestures(EditGesture& gesture, UndoStack& stack, flecs::world& world, Mode mode,
                   std::vector<ComponentFrame>& framesIn, flecs::entity_t inspected,
                   std::uint32_t activeId)
{
    std::vector<ComponentFrame> frames = std::exchange(framesIn, {});
    if (mode != Mode::Edit)
    {
        gesture.open.reset(); // en jeu, rien n'entre dans la pile
        return 0;
    }
    int posted = 0;
    // 1. Fermer ce que les bords ferment : l'écriture de l'image est déjà dans le monde.
    for (const ComponentFrame& frame : frames)
    {
        if ((frame.edges.deactivatedAfterEdit || frame.edges.cancelled) && isOpenOn(gesture, frame))
        {
            const flecs::entity_t entity = frame.entity;
            const flecs::entity_t component = frame.component;
            if (frame.edges.cancelled)
            {
                cancelGesture(gesture, world); // Échap : l'avant exact, zéro commande
            }
            else
            {
                posted += closeGesture(gesture, stack, world, mode);
            }
            settleFramesOf(frames, entity, component);
            break;
        }
    }
    // 2. Fermer ce dont l'entité a changé ou disparu, ou dont l'élément n'est plus l'actif
    //    (relâché sans changement, fenêtre fermée, Échap) : la fin d'image est le dernier filet.
    if (gesture.open)
    {
        const bool entityKept =
            gesture.open->entity == inspected && ecs_is_alive(world, gesture.open->entity);
        const bool itemKept = activeId == gesture.open->activeId;
        if (!entityKept || !itemKept)
        {
            const flecs::entity_t entity = gesture.open->entity;
            const flecs::entity_t component = gesture.open->component;
            posted += closeGesture(gesture, stack, world, mode);
            settleFramesOf(frames, entity, component);
        }
    }
    // 3. Une écriture sans geste ouvert est une commande d'une image : l'avant est celui du début
    //    de l'image, copié avant `commitEdit`. **Sauf si un élément est actif** : un glissé dont le
    //    geste vient d'être fermé de force (Play refusé, entité changée) continue d'écrire, et
    //    cette écriture rouvre un geste à l'étape 4 plutôt que de se perdre. Une liste déroulante
    //    ou une case écrit avec `activeId == 0` : elle reste une commande d'une image.
    for (ComponentFrame& frame : frames)
    {
        if (!frame.wrote || frame.settled || reopensGesture(gesture, frame, activeId) ||
            (frame.edges.activated && activeId != 0) || isOpenOn(gesture, frame) || !frame.before)
        {
            continue;
        }
        posted +=
            postChange(stack, world, mode, frame.entity, frame.component, std::move(*frame.before));
        frame.settled = true;
    }
    // 4. Ouvrir, après avoir fermé : la fin de x et le début de y d'un Tab sont deux commandes.
    //    Le bord d'activation passe avant une réouverture : un seul élément est actif à la fois.
    ComponentFrame* target = nullptr;
    for (ComponentFrame& frame : frames)
    {
        if (frame.before && activeId != 0 && frame.edges.activated)
        {
            target = &frame;
            break;
        }
    }
    for (ComponentFrame& frame : frames)
    {
        if (target == nullptr && reopensGesture(gesture, frame, activeId))
        {
            target = &frame;
        }
    }
    if (target != nullptr)
    {
        ComponentFrame& frame = *target;
        posted += closeGesture(gesture, stack, world, mode);
        // x et y d'un même composant : l'avant de y est le monde après l'écriture de x, sinon
        // annuler y annulerait aussi x.
        std::optional<scene::ValueBox> start;
        if (frame.settled)
        {
            start = worldValueOf(world, frame.entity, frame.component);
        }
        else
        {
            start = std::move(frame.before);
        }
        if (start)
        {
            gesture.open = OpenGesture{.entity = frame.entity,
                                       .component = frame.component,
                                       .before = std::move(*start),
                                       .activeId = activeId};
        }
    }
    return posted;
}

} // namespace levain::editor
