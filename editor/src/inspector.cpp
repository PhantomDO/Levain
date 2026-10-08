#include "levain/editor/inspector.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string_view>

#include <glm/glm.hpp>

#include "levain/editor/editor.hpp"
#include "levain/editor/hierarchy.hpp"
#include "levain/scene/reflection.hpp"

namespace levain::editor
{

namespace
{

constexpr const char* InspectorWindow = "Inspecteur";

/// Ce qu'un pixel de glissé ajoute à un nombre ; Ctrl+clic pour le taper.
constexpr float DragSpeed = 0.01f;

/// Les nombres d'un tableau, quatre par ligne : une colonne de `glm::mat4` par ligne.
constexpr std::int32_t NumbersPerRow = 4;

/// La place du libellé, à droite du widget, en hauteurs de police.
constexpr float LabelWidth = 10.0f;

/// Le type d'ImGui des nombres de flecs, dans l'ordre de `ecs_primitive_kind_t`
/// (flecs/addons/meta.h), qui commence à 1 ; -1 pour ce qui n'est pas un nombre.
// clang-format off
constexpr std::array<ImGuiDataType, EcsPrimitiveKindLast + 1> NumberTypes = {
    -1, -1, ImGuiDataType_S8, ImGuiDataType_U8,                                // Bool, Char, Byte
    ImGuiDataType_U8, ImGuiDataType_U16, ImGuiDataType_U32, ImGuiDataType_U64, // U8 à U64
    ImGuiDataType_S8, ImGuiDataType_S16, ImGuiDataType_S32, ImGuiDataType_S64, // I8 à I64
    ImGuiDataType_Float, ImGuiDataType_Double,                                 // F32, F64
    ImGuiDataType_U64, ImGuiDataType_S64,                                      // UPtr, IPtr
    -1, -1, -1};                                                               // String, Entity, Id
// clang-format on
static_assert(EcsU8 == 4 && EcsF64 == 13 && EcsPrimitiveKindLast == 18, "flecs a changé l'ordre");

/// Le parcours d'un composant : ce qu'il lit, et les champs qu'il a dessinés.
struct Walk
{
    const flecs::world& world;
    const Inspector& inspector;
    int fields = 0;
};

void* fieldAt(void* value, std::int32_t offset)
{
    return static_cast<std::byte*>(value) + offset;
}

std::optional<ImGuiDataType> numberTypeOf(const flecs::world& world, flecs::entity_t type)
{
    const auto* primitive = ecs_get(world, type, EcsPrimitive);
    const ImGuiDataType number = primitive != nullptr ? NumberTypes.at(primitive->kind) : -1;
    return number >= 0 ? std::optional{number} : std::nullopt;
}

void drawValue(Walk& walk, const char* label, flecs::entity_t type, void* value);

/// Un tableau, en ligne (`float w[4]`, le `count` d'un membre) ou de flecs (`glm::mat4`) : quatre
/// nombres par ligne, sinon un élément par ligne.
void drawElements(Walk& walk, const char* label, flecs::entity_t type, std::int32_t count,
                  void* value)
{
    if (!ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }
    const std::int32_t size = ecs_get_type_info(walk.world, type)->size;
    const std::optional<ImGuiDataType> number = numberTypeOf(walk.world, type);
    for (std::int32_t first = 0; first < count;)
    {
        const std::int32_t inRow = number.has_value() ? std::min(NumbersPerRow, count - first) : 1;
        const std::string row = inRow == 1 ? std::format("[{}]", first)
                                           : std::format("[{}-{}]", first, first + inRow - 1);
        if (number.has_value())
        {
            ImGui::DragScalarN(row.c_str(), *number, fieldAt(value, first * size), inRow,
                               DragSpeed);
            ++walk.fields;
        }
        else
        {
            drawValue(walk, row.c_str(), type, fieldAt(value, first * size));
        }
        first += inRow;
    }
    ImGui::TreePop();
}

void drawMembers(Walk& walk, const EcsStruct& description, void* value)
{
    const std::span members{static_cast<const ecs_member_t*>(description.members.array),
                            static_cast<std::size_t>(description.members.count)};
    for (const ecs_member_t& member : members)
    {
        // `count` : 0 pour un scalaire, N pour un tableau en ligne (flecs/addons/meta.h,
        // `ecs_member_t::count`).
        if (member.count > 0)
        {
            drawElements(walk, member.name, member.type, member.count,
                         fieldAt(value, member.offset));
        }
        else
        {
            drawValue(walk, member.name, member.type, fieldAt(value, member.offset));
        }
    }
}

/// Un champ, selon son type ; un agrégat, ses champs sous un nœud.
void drawValue(Walk& walk, const char* label, flecs::entity_t type, void* value)
{
    const flecs::world& world = walk.world;
    const bool glmLeaf = type == walk.inspector.vec2 || type == walk.inspector.vec3;
    if (const auto* description = ecs_get(world, type, EcsStruct);
        description != nullptr && !glmLeaf)
    {
        if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen))
        {
            drawMembers(walk, *description, value);
            ImGui::TreePop();
        }
        return;
    }
    if (const auto* array = ecs_get(world, type, EcsArray))
    {
        drawElements(walk, label, array->type, array->count, value);
        return;
    }
    ++walk.fields;
    const auto* primitive = ecs_get(world, type, EcsPrimitive);
    if (glmLeaf) // ses flottants côte à côte, comme une position s'écrit
    {
        ImGui::DragScalarN(label, ImGuiDataType_Float, value, type == walk.inspector.vec2 ? 2 : 3,
                           DragSpeed);
    }
    else if (const std::optional<ImGuiDataType> number = numberTypeOf(world, type))
    {
        ImGui::DragScalar(label, *number, value, DragSpeed);
    }
    else if (primitive != nullptr && primitive->kind == EcsBool)
    {
        ImGui::Checkbox(label, static_cast<bool*>(value));
    }
    else // une enum, un texte, une entité, un type opaque : le nom du type, jamais ses octets
    {
        ImGui::LabelText(label, "%s", flecs::entity(world, type).path(".", "").c_str());
    }
}

/// Un composant décrit : son en-tête et ses champs. Sinon, une ligne à son nom : une étiquette,
/// une paire, un composant non décrit, jamais ses octets.
int drawComponent(flecs::world& world, const Inspector& inspector, flecs::entity entity,
                  flecs::id component)
{
    const std::string label = componentLabelOf(world, component);
    int fields = 0;
    pushEntityId(component);
    if (component.is_entity() && ecs_has(world, component, EcsStruct))
    {
        if (ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            fields = inspectComponent(world, inspector, entity, component);
        }
    }
    else
    {
        ImGui::TextUnformatted(label.c_str());
    }
    ImGui::PopID();
    return fields;
}

/// Le nom de l'entité s'affiche en tête de l'inspecteur : `(Identifier, Name)` n'est pas un
/// composant pour qui l'inspecte.
bool isEntityName(flecs::id component)
{
    return component.is_pair() && component.first().id() == ecs_id(EcsIdentifier);
}

} // namespace

Inspector createInspector(const flecs::world& world)
{
    return {.vec2 = world.id<glm::vec2>(), .vec3 = world.id<glm::vec3>(), .fieldsDrawn = 0};
}

std::string componentLabelOf(const flecs::world& world, flecs::id component)
{
    // Une paire, ou un identifiant à drapeaux (`flecs::Parent` apporte `(ParentDepth,@1)`) : pas
    // une entité.
    if (!component.is_entity())
    {
        return component.str().c_str();
    }
    const std::string_view key = scene::stableKeyOf(world, component);
    return key.empty() ? std::string{flecs::entity(world, component).path(".", "").c_str()}
                       : std::string{key};
}

int inspectComponent(flecs::world& world, const Inspector& inspector, flecs::entity entity,
                     flecs::entity_t component)
{
    const auto* description = ecs_get(world, component, EcsStruct);
    const void* current = ecs_get_id(world, entity, component);
    if (description == nullptr || current == nullptr)
    {
        return 0;
    }
    // Les widgets dessinent une copie, jamais la table, où un pointeur gardé pendrait : un
    // observateur qui ajoute un composant déplace l'entité dans une autre table. Grisés : la copie
    // est jetée. `ecs_value_*` passe par les crochets du type (flecs.h, « Values »).
    void* copy = ecs_value_new(world, component);
    ecs_value_copy(world, component, copy, current);
    Walk walk{.world = world, .inspector = inspector};
    ImGui::BeginDisabled();
    drawMembers(walk, *description, copy);
    ImGui::EndDisabled();
    ecs_value_free(world, component, copy);
    return walk.fields;
}

void drawInspector(flecs::world& world, Inspector& inspector, flecs::entity_t selected,
                   ImGuiID dock)
{
    inspector.fieldsDrawn = 0;
    ImGui::SetNextWindowDockID(dock, ImGuiCond_FirstUseEver);
    if (ImGui::Begin(InspectorWindow))
    {
        const flecs::entity entity = selectedIfAlive(world, selected);
        if (!entity)
        {
            ImGui::TextDisabled("aucune entité choisie");
        }
        else
        {
            ImGui::TextUnformatted(entity.path("::", "").c_str());
            // Négative, la largeur laisse cette place au libellé, à droite (imgui.h,
            // `PushItemWidth`).
            ImGui::PushItemWidth(ImGui::GetFontSize() * -LabelWidth);
            entity.each(
                [&](flecs::id component)
                {
                    if (!isEntityName(component))
                    {
                        inspector.fieldsDrawn += drawComponent(world, inspector, entity, component);
                    }
                });
            ImGui::PopItemWidth();
        }
    }
    ImGui::End();
}

} // namespace levain::editor
