#include "levain/editor/inspector.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui_internal.h> // GetActiveID : l'élément actif, pour ranger le geste

#include "levain/assets/asset_id.hpp"
#include "levain/assets/registry.hpp"
#include "levain/core/assert.hpp"
#include "levain/editor/editor.hpp"
#include "levain/editor/hierarchy.hpp"
#include "levain/scene/reflection.hpp"
#include "levain/ui/flags.hpp"
#include "levain/ui/style.hpp"
#include "levain/ui/tr.hpp"

namespace levain::editor
{

namespace
{

/// Ce qu'un pixel de glissé ajoute à un nombre ; Ctrl+clic pour le taper.
constexpr float DragSpeed = 0.01f;

/// Ce qu'un pixel de glissé ajoute à un angle, en degrés.
constexpr float RotationSpeed = 0.5f;

/// Les nombres d'un tableau, quatre par ligne : une colonne de `glm::mat4` par ligne.
constexpr std::int32_t NumbersPerRow = 4;

/// La place voulue du libellé, à droite du widget, en hauteurs de police ; `labelColumnFor` la
/// rogne quand la fenêtre est étroite ou l'échelle grande.
constexpr float LabelWidth = 10.0f;

/// Ce qu'il en reste au moins, en hauteurs de police : la valeur passe avant le libellé, qui se
/// coupe.
constexpr float MinLabelWidth = 2.0f;

/// Le plus large des nombres qu'un cadre doit contenir : un négatif de deux chiffres, au « %.3f »
/// par défaut.
constexpr const char* WidestNumber = "-00.000";

/// **X rouge, Y vert, Z bleu** devant chaque composante, comme les champs de position d'Unreal
/// (`ImGuiSliderFlags_ColorMarkers`, imgui.h). ImGui choisit les couleurs lui-même, dans l'ordre
/// (`GDefaultRgbaColorMarkers`, imgui_widgets.cpp) et taille le repère par `ColorMarkerSize`. Pour
/// un vecteur ou des angles seulement : sur un nombre seul, ce serait un repère rouge sans axe, et
/// sur une ligne de tableau ou de matrice, quatre nombres qui ne sont pas des axes.
constexpr int AxisMarkers = ImGuiSliderFlags_ColorMarkers;

/// Les nombres que montre un vecteur, au plus.
constexpr int VectorComponents = 3;

/// Le parcours d'un composant : ce qu'il lit, les champs qu'il a dessinés, et si un widget a changé
/// la copie.
struct Walk
{
    const flecs::world& world;
    Inspector& inspector;
    int fields = 0;
    bool changed = false;
    GestureEdges edges{}; ///< les bords des widgets numériques de ce composant, à cette image
};

/// Les bornes d'un champ, si `.range` lui en a posé (`scene::rangeOf`).
using Range = std::optional<ecs_member_value_range_t>;

void* fieldAt(void* value, std::int32_t offset)
{
    return static_cast<std::byte*>(value) + offset;
}

/// La borne dans le type du champ, saturée à ses limites : convertir un double qui en sort est un
/// comportement indéfini. Un entier ne se borne que vers l'intérieur : un plancher de 0,5 est 1, le
/// tronquer à 0 laisserait passer 0 (`minimumAs`, `maximumAs`).
template <class N> N saturate(double bound)
{
    constexpr double Lowest = static_cast<double>(std::numeric_limits<N>::lowest());
    constexpr double Highest = static_cast<double>(std::numeric_limits<N>::max());
    if (bound <= Lowest)
    {
        return std::numeric_limits<N>::lowest();
    }
    return bound >= Highest ? std::numeric_limits<N>::max() : static_cast<N>(bound);
}

template <class N> N minimumAs(double bound)
{
    return saturate<N>(std::is_integral_v<N> ? std::ceil(bound) : bound);
}

template <class N> N maximumAs(double bound)
{
    return saturate<N>(std::is_integral_v<N> ? std::floor(bound) : bound);
}

/// Un widget de `count` nombres, qui ne change rien s'il en rend un qui n'est pas fini. ImGui
/// laisse taper « nan », « inf », ou « 1e39 » qui déborde : `sscanf` sans filtre de caractères
/// (`TempInputScalar`, imgui_widgets.cpp), et `DataTypeClamp` compare par `<` et `>`, que NaN ne
/// satisfait pas. Un NaN dans une position ferait téléporter un corps de Jolt à NaN : un nombre
/// non fini ne s'écrit jamais, et le champ reste ce qu'il était.
template <class N, class Widget> bool rejectNonFinite(N* numbers, int count, const Widget& widget)
{
    if constexpr (std::is_floating_point_v<N>)
    {
        LEVAIN_ASSERT(count <= NumbersPerRow, "rejectNonFinite : une ligne de nombres au plus");
        std::array<N, NumbersPerRow> before{};
        std::copy_n(numbers, count, before.begin());
        const bool changed = widget();
        if (changed && !std::all_of(numbers, numbers + count, [](N n) { return std::isfinite(n); }))
        {
            std::copy_n(before.begin(), count, numbers);
            return false;
        }
        return changed;
    }
    else
    {
        return widget();
    }
}

/// `count` nombres de type N côte à côte, glissables. Une borne se pose avec `AlwaysClamp` : sans
/// lui, Ctrl+clic tape au-delà (imgui.h, « DragFloat »). Un entier glisse d'une unité par pixel.
/// `markers` : `AxisMarkers` pour un vecteur, 0 sinon.
template <class N>
bool dragAs(ImGuiDataType type, const char* label, void* value, int count, const Range& range,
            int markers = 0)
{
    constexpr float Speed = std::is_integral_v<N> ? 1.0f : DragSpeed;
    const N low = range ? minimumAs<N>(range->min) : N{};
    const N high = range ? maximumAs<N>(range->max) : N{};
    return rejectNonFinite(
        static_cast<N*>(value), count,
        [&]
        {
            return ImGui::DragScalarN(
                label, type, value, count, Speed, range ? &low : nullptr, range ? &high : nullptr,
                nullptr,
                ui::withFlags(range ? static_cast<int>(ImGuiSliderFlags_AlwaysClamp) : 0, markers));
        });
}

static_assert(sizeof(std::uintptr_t) == sizeof(std::uint64_t), "UPtr et IPtr se lisent en 64 bits");

/// Des nombres du genre `kind` de flecs (`numberKindOf`) ; vrai si l'un a changé.
bool dragNumbers(ecs_primitive_kind_t kind, const char* label, void* value, int count,
                 const Range& range)
{
    switch (kind)
    {
    case EcsU8:
    case EcsByte:
        return dragAs<std::uint8_t>(ImGuiDataType_U8, label, value, count, range);
    case EcsU16:
        return dragAs<std::uint16_t>(ImGuiDataType_U16, label, value, count, range);
    case EcsU32:
        return dragAs<std::uint32_t>(ImGuiDataType_U32, label, value, count, range);
    case EcsU64:
    case EcsUPtr:
        return dragAs<std::uint64_t>(ImGuiDataType_U64, label, value, count, range);
    case EcsI8:
    case EcsChar:
        return dragAs<std::int8_t>(ImGuiDataType_S8, label, value, count, range);
    case EcsI16:
        return dragAs<std::int16_t>(ImGuiDataType_S16, label, value, count, range);
    case EcsI32:
        return dragAs<std::int32_t>(ImGuiDataType_S32, label, value, count, range);
    case EcsI64:
    case EcsIPtr:
        return dragAs<std::int64_t>(ImGuiDataType_S64, label, value, count, range);
    case EcsF32:
        return dragAs<float>(ImGuiDataType_Float, label, value, count, range);
    case EcsF64:
        return dragAs<double>(ImGuiDataType_Double, label, value, count, range);
    default:
        return false;
    }
}

/// Le genre de nombre d'un type, ou rien : un booléen, un texte, une entité ou un identifiant n'en
/// sont pas.
std::optional<ecs_primitive_kind_t> numberKindOf(const flecs::world& world, flecs::entity_t type)
{
    const auto* primitive = ecs_get(world, type, EcsPrimitive);
    if (primitive == nullptr || primitive->kind == EcsBool || primitive->kind == EcsString ||
        primitive->kind == EcsEntity || primitive->kind == EcsId)
    {
        return std::nullopt;
    }
    return primitive->kind;
}

/// Les constantes d'une enum, et de quoi lire leur valeur : flecs la range dans le type
/// sous-jacent, en paire `(Constant, type)` sur la constante, et ne remplit
/// `ecs_enum_constant_t::value` que pour un type signé (`value_unsigned` sinon,
/// flecs/addons/meta.h). Les octets de la paire se comparent à ceux du champ et s'y copient, sans
/// regarder le signe.
struct EnumConstants
{
    std::span<const ecs_enum_constant_t> all;
    ecs_id_t pair = 0;
    std::size_t size = 0;
};

std::optional<EnumConstants> constantsOf(const flecs::world& world, flecs::entity_t type)
{
    const auto* description = ecs_get(world, type, EcsEnum);
    const auto* constants = ecs_get(world, type, EcsConstants);
    if (description == nullptr || constants == nullptr)
    {
        return std::nullopt;
    }
    const auto count = static_cast<std::size_t>(ecs_vec_count(&constants->ordered_constants));
    return EnumConstants{
        .all = {ecs_vec_first_t(&constants->ordered_constants, ecs_enum_constant_t), count},
        .pair = ecs_pair(EcsConstant, description->underlying_type),
        .size =
            static_cast<std::size_t>(ecs_get_type_info(world, description->underlying_type)->size)};
}

const void* valueOf(const flecs::world& world, const EnumConstants& constants,
                    const ecs_enum_constant_t& constant)
{
    return ecs_get_id(world, constant.constant, constants.pair);
}

/// Une liste déroulante des constantes ; vrai si l'une a été choisie, sa valeur copiée dans le
/// champ.
bool comboEnum(const flecs::world& world, const char* label, flecs::entity_t type, void* value)
{
    const std::optional<EnumConstants> constants = constantsOf(world, type);
    const char* current = enumNameOf(world, type, value);
    bool chosen = false;
    if (constants && ImGui::BeginCombo(label, current != nullptr ? current : "?"))
    {
        for (const ecs_enum_constant_t& constant : constants->all)
        {
            const void* bytes = valueOf(world, *constants, constant);
            const bool selected = current != nullptr && std::string_view{current} == constant.name;
            const int accent = ui::pushSelectedRowColors(selected);
            if (ImGui::Selectable(constant.name, selected) && bytes != nullptr)
            {
                std::memcpy(value, bytes, constants->size);
                chosen = true;
            }
            ImGui::PopStyleColor(accent);
        }
        ImGui::EndCombo();
    }
    return chosen;
}

void drawValue(Walk& walk, const char* label, flecs::entity_t type, void* value,
               const Range& range);

bool isAuthored(const flecs::world& world, flecs::entity_t component)
{
    return flecs::entity(world, component).has<scene::Authored>();
}

/// Relève où cliquer pour saisir le premier cadre du widget qui vient d'être dessiné : ce que lit
/// le banc (`Inspector::fieldCenters`), les coordonnées d'un champ ne se devinent pas.
void noteCenter(Walk& walk, float frameWidth)
{
    const ImVec2 first = ImGui::GetItemRectMin();
    const ImVec2 last = ImGui::GetItemRectMax();
    walk.inspector.fieldCenters.push_back(
        {first.x + (frameWidth * 0.5f), (first.y + last.y) * 0.5f});
}

/// Relève la largeur du cadre d'un des `count` nombres qui viennent d'être dessinés côte à côte :
/// la largeur du champ (`CalcItemWidth`) est partagée entre eux par `DragScalarN`, moins l'espace
/// entre eux. (Le rectangle de l'item comprendrait aussi le libellé.)
void noteFrames(Walk& walk, int count)
{
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float frame =
        std::floor((ImGui::CalcItemWidth() - (spacing * static_cast<float>(count - 1))) /
                   static_cast<float>(count));
    walk.inspector.narrowestFrame = std::min(walk.inspector.narrowestFrame, frame);
    noteCenter(walk, frame);
}

/// Un tableau, en ligne (`float w[4]`, le `count` d'un membre) ou de flecs (`glm::mat4`) : autant
/// de nombres par ligne que la largeur du champ en tient (quatre au plus), sinon un élément par
/// ligne.
void drawElements(Walk& walk, const char* label, flecs::entity_t type, std::int32_t count,
                  void* value)
{
    if (!ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen))
    {
        return;
    }
    const std::int32_t size = ecs_get_type_info(walk.world, type)->size;
    const std::optional<ecs_primitive_kind_t> number = numberKindOf(walk.world, type);
    const std::int32_t perRow = numbersPerRow(ImGui::CalcItemWidth(), numberCellWidth(),
                                              ImGui::GetStyle().ItemInnerSpacing.x);
    for (std::int32_t first = 0; first < count;)
    {
        const std::int32_t inRow = number.has_value() ? std::min(perRow, count - first) : 1;
        const std::string row = inRow == 1 ? std::format("[{}]", first)
                                           : std::format("[{}-{}]", first, first + inRow - 1);
        if (number.has_value())
        {
            walk.changed |=
                dragNumbers(*number, row.c_str(), fieldAt(value, first * size), inRow, {});
            walk.edges |= edgesOfLastItem();
            noteFrames(walk, inRow);
            ++walk.fields;
        }
        else
        {
            drawValue(walk, row.c_str(), type, fieldAt(value, first * size), {});
        }
        first += inRow;
    }
    ImGui::TreePop();
}

/// Le quaternion en angles (`eulerHint`). L'identifiant du widget range les angles tapés : ils ne
/// valent que tant qu'ils donnent encore la rotation du composant.
bool drawRotation(Walk& walk, const char* label, void* value)
{
    auto& rotation = *static_cast<glm::quat*>(value);
    const ImGuiID id = ImGui::GetID(label);
    const auto typed = walk.inspector.typedAngles.find(id);
    glm::vec3 degrees =
        eulerHint(rotation, typed != walk.inspector.typedAngles.end() ? std::optional{typed->second}
                                                                      : std::nullopt);
    const bool dragged =
        rejectNonFinite(&degrees.x, 3,
                        [&]
                        {
                            return ImGui::DragFloat3(label, &degrees.x, RotationSpeed, 0.0f, 0.0f,
                                                     "%.3f", AxisMarkers);
                        });
    walk.edges |= edgesOfLastItem();
    if (!dragged)
    {
        return false;
    }
    walk.inspector.typedAngles[id] = degrees;
    rotation = rotationFromEuler(degrees);
    return true;
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
            drawValue(walk, member.name, member.type, fieldAt(value, member.offset),
                      scene::rangeOf(member));
        }
    }
}

/// Un champ, selon son type ; un agrégat, ses champs sous un nœud.
void drawValue(Walk& walk, const char* label, flecs::entity_t type, void* value, const Range& range)
{
    const flecs::world& world = walk.world;
    const Inspector& inspector = walk.inspector;
    // Des agrégats que l'inspecteur dessine d'un bloc, sans leurs champs.
    const bool whole = type == inspector.vec2 || type == inspector.vec3 || type == inspector.quat ||
                       type == inspector.assetRef;
    if (const auto* description = ecs_get(world, type, EcsStruct); description != nullptr && !whole)
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
    if (type == inspector.vec2 || type == inspector.vec3) // ses flottants côte à côte
    {
        walk.changed |= dragAs<float>(ImGuiDataType_Float, label, value,
                                      type == inspector.vec2 ? 2 : 3, {}, AxisMarkers);
        walk.edges |= edgesOfLastItem();
        noteFrames(walk, type == inspector.vec2 ? 2 : 3);
    }
    else if (type == inspector.quat)
    {
        walk.changed |= drawRotation(walk, label, value);
        noteFrames(walk, 3);
    }
    else if (type == inspector.assetRef) // en lecture seule : un asset se choisit par son GUID
    {
        ImGui::LabelText(
            label, "%s",
            assetNameOf(inspector.registry, *static_cast<const assets::AssetRef*>(value)).c_str());
    }
    else if (type == inspector.entity)
    {
        ImGui::LabelText(label, "%s", entityLabelOf(entityFieldOf(world, value)).c_str());
    }
    else if (primitive != nullptr && primitive->kind == EcsEntity) // `flecs::Parent`, en lecture
    {
        const auto id = *static_cast<const flecs::entity_t*>(value);
        ImGui::LabelText(label, "%s", entityLabelOf(flecs::entity{world, id}).c_str());
    }
    else if (const std::optional<ecs_primitive_kind_t> number = numberKindOf(world, type))
    {
        walk.changed |= dragNumbers(*number, label, value, 1, range);
        walk.edges |= edgesOfLastItem();
        noteFrames(walk, 1);
    }
    else if (primitive != nullptr && primitive->kind == EcsBool)
    {
        walk.changed |= ImGui::Checkbox(label, static_cast<bool*>(value));
    }
    else if (ecs_has(world, type, EcsEnum))
    {
        walk.changed |= comboEnum(world, label, type, value);
        noteCenter(walk, ImGui::CalcItemWidth());
    }
    else // un texte, un type opaque : le nom du type, jamais ses octets
    {
        ImGui::LabelText(label, "%s", flecs::entity(world, type).path(".", "").c_str());
    }
}

/// Un composant décrit : son en-tête et ses champs. Sinon, une ligne à son nom : une étiquette,
/// une paire, un composant non décrit, jamais ses octets.
int drawComponent(flecs::world& world, Inspector& inspector, flecs::entity entity,
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

/// Les angles d'une rotation, en degrés : tangage (X), lacet (Y), roulis (Z), du produit
/// R = Ry·Rx·Rz, lacet d'abord comme `applyFpsInput`. Le lacet couvre ±180° et le tangage ±90°, ce
/// que fait un personnage ou une caméra ; `glm::eulerAngles` (Rz·Ry·Rx) limite le lacet à ±90° et
/// montre -95° de lacet comme (180°, -85°, 180°). Le tangage par `atan2`, et le roulis relu après
/// le lacet : à ±90° de tangage, où lacet et roulis se confondent, les angles rendus donnent encore
/// la rotation. Les colonnes de `mat3_cast` sont `m[colonne][ligne]`.
glm::vec3 pitchYawRollOf(const glm::quat& rotation)
{
    const glm::mat3 m = glm::mat3_cast(rotation);
    const float yaw = std::atan2(m[2][0], m[2][2]);
    const float pitch = std::atan2(-m[2][1], std::hypot(m[0][1], m[1][1]));
    const float roll = std::atan2(std::sin(yaw) * m[1][2] - std::cos(yaw) * m[1][0],
                                  std::cos(yaw) * m[0][0] - std::sin(yaw) * m[0][2]);
    return glm::degrees(glm::vec3{pitch, yaw, roll});
}

/// Les angles que l'écran écrit à trois décimales : un angle qui s'y arrondit à zéro vaut 0, jamais
/// -0 ni un « -0.000 » de plus petit que la moitié du dernier chiffre.
glm::vec3 zeroIfTiny(glm::vec3 degrees)
{
    constexpr float HalfLastDigit = 0.0005f;
    return glm::mix(degrees, glm::vec3{0.0f},
                    glm::lessThan(glm::abs(degrees), glm::vec3{HalfLastDigit}));
}

} // namespace

Inspector createInspector(const flecs::world& world, const assets::AssetRegistry* registry)
{
    return {.vec2 = world.id<glm::vec2>(),
            .vec3 = world.id<glm::vec3>(),
            .quat = world.id<glm::quat>(),
            .entity = world.id<flecs::entity>(),
            .assetRef = world.id<assets::AssetRef>(),
            .registry = registry,
            .typedAngles = {},
            .fieldsDrawn = 0,
            .wrote = false};
}

glm::vec3 eulerHint(const glm::quat& rotation, const std::optional<glm::vec3>& typed)
{
    // Aussi près qu'un flottant le permet : les angles tapés ont été écrits par
    // `rotationFromEuler`, le même calcul. -q est la même rotation.
    constexpr float Same = 1.0e-6f;
    if (typed)
    {
        const glm::quat made = rotationFromEuler(*typed);
        if (glm::all(glm::equal(made, rotation, Same)) ||
            glm::all(glm::equal(made, -rotation, Same)))
        {
            return *typed;
        }
    }
    return zeroIfTiny(pitchYawRollOf(rotation));
}

glm::quat rotationFromEuler(glm::vec3 degrees)
{
    const glm::vec3 radians = glm::radians(degrees);
    return glm::normalize(glm::angleAxis(radians.y, glm::vec3{0.0f, 1.0f, 0.0f}) *
                          glm::angleAxis(radians.x, glm::vec3{1.0f, 0.0f, 0.0f}) *
                          glm::angleAxis(radians.z, glm::vec3{0.0f, 0.0f, 1.0f}));
}

flecs::entity entityFieldOf(const flecs::world& world, const void* field)
{
    return flecs::entity{world, static_cast<const flecs::entity*>(field)->id()};
}

std::string entityLabelOf(flecs::entity entity)
{
    if (entity.id() == 0)
    {
        return ui::tr("aucune");
    }
    return entity.is_alive() ? std::string{entity.path("::", "").c_str()} : ui::tr("(détruite)");
}

std::string assetNameOf(const assets::AssetRegistry* registry, const assets::AssetRef& ref)
{
    if (!ref.isSet())
    {
        return ui::tr("aucun");
    }
    std::string name = assets::toString(ref.asset);
    if (registry != nullptr)
    {
        if (const auto path = assets::pathOf(*registry, ref.asset))
        {
            name = path->filename().string();
        }
    }
    return std::format("{} #{}", name, ref.sub);
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

const char* enumNameOf(const flecs::world& world, flecs::entity_t type, const void* value)
{
    const std::optional<EnumConstants> constants = constantsOf(world, type);
    if (!constants)
    {
        return nullptr;
    }
    for (const ecs_enum_constant_t& constant : constants->all)
    {
        const void* mine = valueOf(world, *constants, constant);
        if (mine != nullptr && std::memcmp(mine, value, constants->size) == 0)
        {
            return constant.name;
        }
    }
    return nullptr;
}

bool commitEdit(flecs::world& world, flecs::entity entity, flecs::entity_t component,
                const void* before, const void* edited)
{
    if (!isAuthored(world, component) || scene::sameValue(world, component, before, edited))
    {
        return false;
    }
    scene::setComponentValue(world, entity, component, edited);
    return true;
}

int inspectComponent(flecs::world& world, Inspector& inspector, flecs::entity entity,
                     flecs::entity_t component)
{
    const auto* description = ecs_get(world, component, EcsStruct);
    const void* current = ecs_get_id(world, entity, component);
    if (description == nullptr || current == nullptr)
    {
        return 0;
    }
    // Les widgets éditent une copie, jamais la table, où un pointeur gardé pendrait : un
    // observateur qui ajoute un composant déplace l'entité dans une autre table. Sans `Authored`,
    // grisés. `ecs_value_*` passe par les crochets du type (flecs.h, « Values »).
    void* copy = ecs_value_new(world, component);
    ecs_value_copy(world, component, copy, current);
    Walk walk{.world = world, .inspector = inspector};
    ImGui::BeginDisabled(!isAuthored(world, component));
    drawMembers(walk, *description, copy);
    ImGui::EndDisabled();
    // **L'avant se copie avant d'écrire** (ADR-0038, décision 2) : `flecs_defer_set` écrit tout de
    // suite un composant que l'entité a déjà, et `current` pointe la table. C'est la valeur du
    // monde au début de l'image, celle qu'un geste qui s'ouvre garde.
    ComponentFrame frame;
    const bool tracked = inspector.history != nullptr && inspector.mode == Mode::Edit;
    if (tracked && (walk.changed || walk.edges.activated))
    {
        frame.before.emplace(world, ecs_get_typeid(world, component), current);
    }
    const bool wrote = walk.changed && commitEdit(world, entity, component, current, copy);
    if (wrote)
    {
        inspector.wrote = true;
    }
    if (tracked &&
        (wrote || walk.edges.activated || walk.edges.deactivatedAfterEdit || walk.edges.cancelled))
    {
        frame.entity = entity;
        frame.component = component;
        frame.edges = walk.edges;
        frame.wrote = wrote;
        inspector.frames.push_back(std::move(frame));
    }
    ecs_value_free(world, component, copy);
    return walk.fields;
}

GestureEdges edgesOfLastItem()
{
    return {.activated = ImGui::IsItemActivated(),
            .deactivatedAfterEdit = ImGui::IsItemDeactivatedAfterEdit(),
            .cancelled = ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)};
}

float numberCellWidth()
{
    return ImGui::CalcTextSize(WidestNumber).x + (2.0f * ImGui::GetStyle().FramePadding.x) +
           ImGui::GetStyle().ColorMarkerSize;
}

int numbersPerRow(float fieldWidth, float cell, float innerSpacing)
{
    const float fitting = std::floor((fieldWidth + innerSpacing) / (cell + innerSpacing));
    return std::clamp(static_cast<int>(fitting), 1, static_cast<int>(NumbersPerRow));
}

float labelColumnFor(float available, float wanted, float minimum, float cell, float innerSpacing)
{
    const float room =
        available - (VectorComponents * cell) - ((VectorComponents - 1) * innerSpacing);
    return std::clamp(room, minimum, wanted);
}

namespace
{

/// La fenêtre elle-même ; rend l'entité dessinée (0 si aucune : fenêtre fermée ou repliée, pas de
/// sélection), ce que `settleGestures` compare à l'entité du geste ouvert.
flecs::entity_t drawInspectorWindow(flecs::world& world, Inspector& inspector,
                                    flecs::entity_t selected, ImGuiID dock, bool* open)
{
    flecs::entity_t inspected = 0;
    if (open != nullptr && !*open)
    {
        return inspected;
    }
    ImGui::SetNextWindowDockID(dock, ImGuiCond_FirstUseEver);
    if (ImGui::Begin(ui::labelOf(InspectorWindow).c_str(), open))
    {
        const flecs::entity entity = selectedIfAlive(world, selected);
        if (!entity)
        {
            ImGui::TextDisabled("%s", ui::tr("aucune entité choisie"));
        }
        else
        {
            inspected = entity;
            ImGui::TextUnformatted(entity.path("::", "").c_str());
            // Négative, la largeur laisse cette place au libellé, à droite (imgui.h,
            // `PushItemWidth`). Mesurée : à 125 % et 150 % la colonne fixe de 10 em prenait la
            // moitié de la fenêtre et coupait les nombres.
            const float fontSize = ImGui::GetFontSize();
            const float innerSpacing = ImGui::GetStyle().ItemInnerSpacing.x;
            ImGui::PushItemWidth(-labelColumnFor(ImGui::GetContentRegionAvail().x,
                                                 fontSize * LabelWidth, fontSize * MinLabelWidth,
                                                 numberCellWidth(), innerSpacing));
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
    return inspected;
}

} // namespace

void drawInspector(flecs::world& world, Inspector& inspector, flecs::entity_t selected,
                   ImGuiID dock, bool* open)
{
    LEVAIN_ASSERT(world.is_deferred(), "drawInspector : le monde doit être différé (defer_begin)");
    inspector.fieldsDrawn = 0;
    inspector.wrote = false;
    inspector.narrowestFrame = std::numeric_limits<float>::max();
    inspector.frames.clear();
    inspector.fieldCenters.clear();
    const flecs::entity_t inspected = drawInspectorWindow(world, inspector, selected, dock, open);
    // **Même fenêtre fermée ou repliée** : un glissé dont l'élément n'est plus l'actif se ferme
    // à la fin de l'image, quoi que l'inspecteur ait dessiné.
    if (inspector.history != nullptr)
    {
        // Échap a rendu l'avant au monde : un angle tapé avant ne doit pas le recouvrir.
        const bool cancelled = std::ranges::any_of(inspector.frames, [](const ComponentFrame& frame)
                                                   { return frame.edges.cancelled; });
        if (cancelled)
        {
            inspector.typedAngles.clear();
        }
        settleGestures(inspector.gesture, *inspector.history, world, inspector.mode,
                       inspector.frames, inspected, ImGui::GetActiveID());
    }
}

} // namespace levain::editor
