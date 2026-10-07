// Le contrôle de l'ADR-0034 (« Les contrôles ») : tous les modules du moteur importés sans fenêtre
// ni GPU, chaque composant décrit vérifié un par un, puis l'inventaire de ce qui est décrit ou non.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>
#include <flecs.h>

#include "levain/assets/asset_ref.hpp"
#include "levain/character/walk.hpp"
#include "levain/physics/character.hpp"
#include "levain/physics/components.hpp"
#include "levain/physics/physics.hpp"
#include "levain/scene/camera_control.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/reflection.hpp"
#include "levain/scene/scene.hpp"
#ifndef __EMSCRIPTEN__ // app n'est pas lié aux tests du navigateur : il tire le GPU
#include "levain/app/app.hpp"
#include "levain/app/camera.hpp"
#endif

using namespace levain;

namespace
{

enum class Edit : std::uint8_t
{
    Authored,
    ReadOnly,
};

struct Range
{
    std::string_view field;
    double min = 0.0;
    double max = 0.0;
};

/// Les composants vérifiés, par clé : l'inventaire les compare à ce que le monde déclare.
using Checked = std::map<std::string, Edit, std::less<>>;

/// Un composant décrit : sa clé, `Authored`, ses bornes (exactement celles déclarées), un seul
/// `OnSet` par `setComponentValue`, un JSON non nul, relu à l'identique. Rend l'entité qui porte
/// `value`.
template <class T>
flecs::entity checkDescribed(flecs::world& world, Checked& checked, std::string_view key, Edit edit,
                             std::initializer_list<Range> ranges, const T& value = T{})
{
    CAPTURE(key);
    checked.emplace(key, edit);
    const flecs::entity_t component = world.id<T>();
    CHECK(scene::stableKeyOf(world, component) == key);
    CHECK(scene::componentOfKey(world, key) == component);
    CHECK(world.entity(component).has<scene::Authored>() == (edit == Edit::Authored));

    REQUIRE(world.entity(component).has<flecs::Struct>()); // une description oubliée, nommée
    const EcsStruct& description = world.entity(component).get<flecs::Struct>();
    const auto* members = static_cast<const ecs_member_t*>(description.members.array);
    std::size_t bounded = 0;
    for (std::int32_t i = 0; i < description.members.count; ++i)
    {
        CAPTURE(members[i].name);
        const auto declared =
            std::ranges::find(ranges, std::string_view{members[i].name}, &Range::field);
        const auto range = scene::rangeOf(members[i]);
        CHECK(range.has_value() == (declared != ranges.end()));
        if (range.has_value() && declared != ranges.end())
        {
            ++bounded;
            CHECK(range->min == declared->min);
            CHECK(range->max == declared->max);
        }
    }
    CHECK(bounded == ranges.size()); // un nom de champ mal écrit dans le test

    int sets = 0;
    const flecs::observer counter = world.observer()
                                        .with(component)
                                        .event(flecs::OnSet)
                                        .each([&sets](flecs::entity) { ++sets; });
    const flecs::entity entity = world.entity();
    scene::setComponentValue(world, entity, component, &value);
    CHECK(sets == 1);
    counter.destruct();

    const std::string path = world.entity(component).path(".", "").c_str();
    const std::string entityJson = entity.to_json().c_str();
    CHECK(entityJson.contains(std::format("\"{}\":", path)));
    CHECK_FALSE(entityJson.contains(std::format("\"{}\":null", path)));
    // Relu dans des octets qui ne valent rien : un champ que le JSON oublierait resterait faux.
    const void* stored = ecs_get_id(world, entity, component);
    const flecs::string json = world.to_json(component, stored);
    alignas(T) std::array<std::byte, sizeof(T)> parsed{};
    parsed.fill(std::byte{0xff});
    CHECK(world.from_json(component, parsed.data(), json.c_str()) != nullptr);
    CHECK(scene::sameValue(world, component, stored, parsed.data()));
    return entity;
}

std::string joined(const std::vector<std::string>& keys)
{
    std::string text;
    for (const std::string& key : keys)
    {
        text += (text.empty() ? "" : ", ") + key;
    }
    return text;
}

} // namespace

TEST_CASE("les composants du moteur, décrits : clé, JSON, bornes, Authored, un seul OnSet, et "
          "leur inventaire")
{
    flecs::world world;
    world.import<assets::AssetsModule>();
    world.import<character::WalkModule>(); // la physique et la scène avec elle
    Checked checked;
    const auto authored = Edit::Authored;
    const auto readOnly = Edit::ReadOnly;

    checkDescribed<scene::Transform>(world, checked, "levain.scene.Transform", authored, {});
    checkDescribed<scene::Velocity>(world, checked, "levain.scene.Velocity", authored, {});
    checkDescribed<scene::FpsController>(world, checked, "levain.scene.FpsController", authored,
                                         {{"minPitchDegrees", -90.0, 90.0},
                                          {"maxPitchDegrees", -90.0, 90.0},
                                          {"pitchDegrees", -90.0, 90.0}});
    checkDescribed<scene::WorldTransform>(world, checked, "levain.scene.WorldTransform", readOnly,
                                          {});
    checkDescribed<scene::PreviousTransform>(world, checked, "levain.scene.PreviousTransform",
                                             readOnly, {});
    checkDescribed<scene::RenderAlpha>(world, checked, "levain.scene.RenderAlpha", readOnly, {});
    checkDescribed<scene::FpsInput>(world, checked, "levain.scene.FpsInput", readOnly, {});

    checkDescribed<physics::RigidBody>(
        world, checked, "levain.physics.RigidBody", authored,
        {{"mass", 0.001, 1.0e6}, {"friction", 0.0, 1.0}, {"restitution", 0.0, 1.0}});
    checkDescribed<physics::CharacterController>(
        world, checked, "levain.physics.CharacterController", authored,
        {{"maxSlopeDegrees", 1.0, 89.0}, {"mass", 0.001, 1.0e6}});
    checkDescribed<physics::Capsule>(world, checked, "levain.physics.Capsule", readOnly,
                                     {{"halfHeight", 0.01, 10.0}, {"radius", 0.01, 10.0}});
    checkDescribed<physics::CharacterState>(world, checked, "levain.physics.CharacterState",
                                            readOnly, {});
    checkDescribed<physics::CharacterVelocity>(world, checked, "levain.physics.CharacterVelocity",
                                               readOnly, {});

    checkDescribed<character::Walker>(world, checked, "levain.character.Walker", authored,
                                      {{"acceleration", 0.0, 1000.0},
                                       {"airControl", 0.0, 1.0},
                                       {"turnDegreesPerSecond", 0.0, 3600.0}});
    checkDescribed<character::WalkInput>(world, checked, "levain.character.WalkInput", readOnly,
                                         {});

    // MeshRef, par son hook on_replace : un GUID au-delà de 2^53, relu exactement, et un compte de
    // références qui ne dérive pas quand l'éditeur réécrit la même valeur.
    const assets::AssetId guid{.high = 0x9e3779b97f4a7c15u, .low = 0xfedcba9876543211u};
    const assets::MeshRef mesh{.mesh = {.asset = guid, .sub = 2}};
    const flecs::entity meshEntity = checkDescribed<assets::MeshRef>(
        world, checked, "levain.assets.MeshRef", readOnly, {}, mesh);
    CHECK(assets::referenceCount(world, guid) == 1);
    scene::setComponentValue(world, meshEntity, world.id<assets::MeshRef>(), &mesh);
    CHECK(assets::referenceCount(world, guid) == 1);
    meshEntity.destruct();
    CHECK(assets::referenceCount(world, guid) == 0);

#ifndef __EMSCRIPTEN__
    app::prepareAppWorld(world); // le monde que prépare createApp, PlayerInput compris
    checkDescribed<app::CameraLens>(
        world, checked, "levain.app.CameraLens", authored,
        {{"verticalFovDegrees", 1.0, 179.0}, {"nearPlane", 0.01, 100.0}});
#endif

    // L'inventaire (règle n°6) : les composants à champs du moteur, décrits ou non. `Declared`,
    // interne à reflection.cpp, distingue une déclaration d'un type décrit par récursion.
    const flecs::entity_t declaredTag =
        scene::componentOfKey(world, "levain.scene.detail.Declared");
    REQUIRE(declaredTag != 0);
    std::map<Edit, std::vector<std::string>> described;
    std::vector<std::string> fieldTypes;
    std::vector<std::string> undescribed;
    world.query_builder<const flecs::Component>().build().each(
        [&](flecs::entity type, const flecs::Component& info)
        {
            const std::string key{scene::stableKeyOf(world, type)};
            if (!key.starts_with("levain.") || type.has(flecs::Module))
            {
                return;
            }
            CAPTURE(key);
            const auto found = checked.find(key);
            CHECK(type.has(declaredTag) == (found != checked.end())); // chaque déclaration testée
            CHECK(type.has<scene::Authored>() ==
                  (found != checked.end() && found->second == Edit::Authored));
            if (found != checked.end())
            {
                described[found->second].push_back(key);
            }
            else if (info.size > 0) // une étiquette, une phase : pas de champ
            {
                // Un agrégat ou une enum d'un champ décrit : sa réflexion, sans déclaration.
                (type.has<flecs::Type>() ? fieldTypes : undescribed).push_back(key);
            }
        });
    // La boucle a vu chaque composant vérifié : sans quoi les deux CHECK plus haut ne disent rien.
    CHECK(described[authored].size() + described[readOnly].size() == checked.size());
    for (std::vector<std::string>* keys :
         {&described[authored], &described[readOnly], &fieldTypes, &undescribed})
    {
        std::ranges::sort(*keys);
    }
    MESSAGE(std::format("inventaire des composants à champs du moteur (ADR-0034) : {} décrits, "
                        "{} d'auteur ({}) et {} en lecture seule ({}) ; {} types de leurs champs "
                        "({}) ; {} non décrits ({})",
                        described[authored].size() + described[readOnly].size(),
                        described[authored].size(), joined(described[authored]),
                        described[readOnly].size(), joined(described[readOnly]), fieldTypes.size(),
                        joined(fieldTypes), undescribed.size(), joined(undescribed)));
}

namespace described_components_test
{

/// Un module de jeu qui reprend les types de la marche sans WalkModule (le TraversalModule de
/// *Rando*, ADR-0031), importé avant tout module du moteur.
struct BareGameModule
{
    explicit BareGameModule(flecs::world& world)
    {
        world.module<BareGameModule>();
        character::describeWalkComponents(world);
    }
};

} // namespace described_components_test

TEST_CASE("describeWalkComponents avant tout import du moteur : l'ordre des imports ne change rien")
{
    flecs::world world;
    // Sans l'import de SceneModule dans describeWalkComponents, le programme s'arrête ici : le
    // glm::vec2 de WalkInput n'aurait pas de réflexion.
    world.import<described_components_test::BareGameModule>();
    CHECK(world.entity(world.id<character::WalkInput>()).has<flecs::Struct>());
}
