#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>
#include <flecs.h>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include "levain/assets/asset_id.hpp"
#include "levain/assets/asset_ref.hpp"
#include "levain/assets/registry.hpp"
#include "levain/core/i18n.hpp"
#include "levain/core/log_ring.hpp"
#include "levain/editor/editor.hpp"
#include "levain/editor/hierarchy.hpp"
#include "levain/editor/inspector.hpp"
#include "levain/editor/shortcut_edit.hpp"
#include "levain/platform/input_script.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/reflection.hpp"
#include "levain/scene/scene.hpp"
#include "levain/ui/context.hpp"
#include "levain/ui/style.hpp"

namespace
{

std::vector<std::string_view> viewsOf(const std::vector<char*>& arguments)
{
    return {arguments.begin(), arguments.end()};
}

} // namespace

TEST_CASE("les options de l'éditeur sont retirées, le reste garde son ordre")
{
    std::array<char, 16> program{"levain_sandbox"};
    std::array<char, 16> view{"--view"};
    std::array<char, 16> physics{"physics"};
    std::array<char, 16> select{"--select"};
    std::array<char, 16> camera{"camera"};
    std::array<char*, 5> arguments{program.data(), select.data(), camera.data(), view.data(),
                                   physics.data()};
    levain::editor::EditorOptions options;
    const std::vector<char*> rest = levain::editor::takeEditorOptions(arguments, options);
    CHECK(viewsOf(rest) == std::vector<std::string_view>{"levain_sandbox", "--view", "physics"});
    CHECK(options.select.value_or("") == "camera");

    // Une option est un nom et une valeur : « --select » en valeur d'une autre n'est pas pris, et
    // sans valeur il reste, pour que le programme le refuse.
    std::array<char*, 4> valueAndAlone{program.data(), view.data(), select.data(), select.data()};
    levain::editor::EditorOptions untouched;
    CHECK(viewsOf(levain::editor::takeEditorOptions(valueAndAlone, untouched)) ==
          std::vector<std::string_view>{"levain_sandbox", "--view", "--select", "--select"});
    CHECK_FALSE(untouched.select.has_value());
}

TEST_CASE("l'éditeur refuse WebGPU, en nommant l'option, et accepte les autres backends")
{
    const auto refused = levain::editor::requireEditorBackend(nvrhi::GraphicsAPI::WEBGPU);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message.find("--gpu webgpu") != std::string::npos);
    CHECK(levain::editor::requireEditorBackend(nvrhi::GraphicsAPI::VULKAN).has_value());
    CHECK(levain::editor::requireEditorBackend(nvrhi::GraphicsAPI::D3D12).has_value());
}

TEST_CASE("une sélection survit à la destruction de son entité, sans désigner celle qui recycle "
          "son index")
{
    flecs::world world;
    const flecs::entity chosen = world.entity("choisie");
    CHECK(levain::editor::selectedIfAlive(world, chosen) == chosen);
    CHECK_FALSE(levain::editor::selectedIfAlive(world, 0));

    const flecs::entity_t selected = chosen;
    chosen.destruct();
    CHECK_FALSE(levain::editor::selectedIfAlive(world, selected));
    // flecs recycle l'index, avec une autre génération : la nouvelle n'est pas la sélection.
    const flecs::entity recycled = world.entity();
    CHECK(static_cast<std::uint32_t>(recycled.id()) == static_cast<std::uint32_t>(selected));
    CHECK_FALSE(levain::editor::selectedIfAlive(world, selected));
}

namespace
{

using levain::editor::HierarchyRow;
using levain::scene::Transform;

/// Les lignes, par nom et profondeur : « a », « .b » pour un enfant de « a ».
std::vector<std::string> rowsOf(const flecs::world& world,
                                const levain::editor::Hierarchy& hierarchy)
{
    std::vector<HierarchyRow> rows;
    levain::editor::listHierarchyRows(hierarchy, rows);
    std::vector<std::string> names;
    names.reserve(rows.size());
    for (const HierarchyRow& row : rows)
    {
        names.push_back(std::string(static_cast<std::size_t>(row.depth), '.') +
                        world.entity(row.entity).name().c_str());
    }
    return names;
}

} // namespace

TEST_CASE("la hiérarchie liste les racines, et les enfants des seuls nœuds ouverts")
{
    flecs::world world;
    const flecs::entity a = world.entity("a").set(Transform{});
    world.entity(flecs::Parent{a}, "b").set(Transform{});
    const flecs::entity c = world.entity(flecs::Parent{a}, "c").set(Transform{});
    const flecs::entity d = world.entity(flecs::Parent{c}, "d").set(Transform{});
    world.entity(flecs::Parent{a}, "sans_transform");
    world.entity("e").set(Transform{});
    // Créée par son chemin, elle est rangée par `ChildOf`, sans `flecs::Parent` : une racine, et
    // une seule ligne, même sous « a » ouvert.
    world.entity("a::par_chemin").set(Transform{});
    // Placée sous un parent qui ne l'est pas : ni racine, ni enfant d'une ligne (README).
    world.entity(flecs::Parent{world.entity("nu")}, "orpheline").set(Transform{});
    levain::editor::Hierarchy hierarchy = levain::editor::createHierarchy(world);

    CHECK(rowsOf(world, hierarchy) == std::vector<std::string>{"a", "e", "par_chemin"});
    hierarchy.open.insert(a);
    CHECK(rowsOf(world, hierarchy) == std::vector<std::string>{"a", ".b", ".c", "e", "par_chemin"});
    // --select d : ses ancêtres s'ouvrent, elle se voit.
    hierarchy.open.clear();
    levain::editor::revealInHierarchy(hierarchy, d);
    CHECK(rowsOf(world, hierarchy) ==
          std::vector<std::string>{"a", ".b", ".c", "..d", "e", "par_chemin"});

    // La flèche suit la liste : « e » n'a qu'un enfant par `ChildOf`, « b » aucun.
    CHECK(levain::editor::hasShownChildren(world, a));
    world.entity("e::sous_e").set(Transform{});
    CHECK_FALSE(levain::editor::hasShownChildren(world, world.lookup("e")));
    CHECK_FALSE(levain::editor::hasShownChildren(world, world.lookup("a::b")));
}

namespace
{
struct Detour
{
};
} // namespace

TEST_CASE(
    "la hiérarchie range ses racines par index, non par tables, et une réveillée reprend sa place")
{
    flecs::world world;
    const flecs::entity first = world.entity("première").set(Transform{});
    const flecs::entity second = world.entity("seconde").set(Transform{});
    world.entity("troisième").set(Transform{});
    const levain::editor::Hierarchy hierarchy = levain::editor::createHierarchy(world);
    REQUIRE(rowsOf(world, hierarchy) ==
            std::vector<std::string>{"première", "seconde", "troisième"});

    // Un détour par une autre table renvoie « seconde » en fin de sa table d'origine : l'ordre des
    // tables n'est plus celui de la création, celui des index l'est toujours.
    second.add<Detour>();
    second.remove<Detour>();
    CHECK(rowsOf(world, hierarchy) == std::vector<std::string>{"première", "seconde", "troisième"});

    // Détruite puis réveillée avec sa génération (ce que fait Stop) : son identifiant entier est
    // plus grand que tous les autres, son index non.
    const flecs::entity_t id = first.id();
    first.destruct();
    ecs_make_alive(world, id);
    world.entity(id).set_name("première").set(Transform{});
    CHECK(rowsOf(world, hierarchy) == std::vector<std::string>{"première", "seconde", "troisième"});
}

TEST_CASE("la hiérarchie tait les entités de flecs, et un singleton va dans « Singletons »")
{
    flecs::world world;
    // flecs garde l'instance d'un module sur l'entité du module, et `Component` se décrit
    // lui-même : deux composants qui portent leur valeur, sans être des singletons de la scène.
    const flecs::entity scene = world.import<levain::scene::SceneModule>();
    world.entity("placée").set(Transform{});
    // Un singleton est rangé sur l'entité de son composant : sans `scene::isEngineInternal`, le
    // composant Transform passerait pour une racine de la scène, comme un système placé.
    world.set(Transform{});
    world.system("système").run([](flecs::iter&) {}).set(Transform{});
    world.component<levain::scene::Velocity>();
    const levain::editor::Hierarchy hierarchy = levain::editor::createHierarchy(world);

    CHECK(rowsOf(world, hierarchy) == std::vector<std::string>{"placée"});
    const std::vector<flecs::entity_t> singletons = levain::editor::singletonsOf(world);
    CHECK(std::ranges::count(singletons, world.component<Transform>().id()) == 1);
    CHECK(std::ranges::count(singletons, world.component<levain::scene::Velocity>().id()) == 0);
    CHECK(std::ranges::count(singletons, scene.id()) == 0);
    CHECK(std::ranges::count(singletons, world.component<flecs::Component>().id()) == 0);
}

namespace editor_test
{

enum class Mode : std::uint8_t
{
    Calm,
    Wild,
};

struct Inner
{
    float a = 0.0f;
    float b = 0.0f;
};

/// Un champ de chaque sorte : nombres, booléen, enum, agrégat imbriqué, feuille glm.
struct Tuning
{
    float gain = 0.5f;
    std::int32_t count = 3;
    bool enabled = true;
    Mode mode = Mode::Wild;
    Inner inner{};
    glm::vec3 offset{0.0f};
};

struct Weights // un tableau en ligne, décrit à la main
{
    std::array<float, 6> w{};
};

struct Undescribed
{
    int hidden = 0;
};

struct Tag
{
};

struct Dial // une donnée d'auteur bornée
{
    float level = 0.5f;
};

struct Gauge // ce que le jeu réécrit : en lecture seule
{
    float level = 0.5f;
};

struct Tally // un entier de bornes fractionnaires
{
    std::int32_t n = 5;
};

struct Facing // une rotation : le dessinateur d'angles
{
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

struct Pan // un vecteur à deux composantes : X et Y seulement
{
    glm::vec2 uv{0.0f};
};

/// Un composant neuf, tel qu'un jeu l'écrirait : le critère de M7.2 (« éditable en une seule
/// déclaration »). `intensity` est le dernier champ : le test tape dans le dernier widget dessiné.
struct Beacon
{
    glm::quat facing{1.0f, 0.0f, 0.0f, 0.0f};
    flecs::entity target{};
    Mode mode = Mode::Calm;
    float intensity = 0.5f;
};

} // namespace editor_test

namespace
{

using editor_test::Tuning;

void describeTestComponents(flecs::world& world)
{
    world.import<levain::scene::SceneModule>();
    levain::scene::describeAuthored<Tuning>(world).range(&Tuning::gain, 0.0, 1.0);
    world.component<editor_test::Weights>().member<float>("w", 6);
    world.component<editor_test::Undescribed>();
    levain::scene::describeAuthored<editor_test::Dial>(world).range(&editor_test::Dial::level, 0.0,
                                                                    1.0);
    levain::scene::describe<editor_test::Gauge>(world);
    levain::scene::describeAuthored<editor_test::Facing>(world);
    levain::scene::describeAuthored<editor_test::Pan>(world);
}

/// Une image d'ImGui, sans GPU, le monde différé comme l'éditeur le fait pour ses panneaux.
template <class Draw> void uiFrame(flecs::world& world, const Draw& draw)
{
    levain::ui::prepareUiFrame(ImGui::GetIO(), {.width = 640, .height = 480}, 1.0 / 60.0);
    ImGui::NewFrame();
    world.defer_begin();
    draw();
    world.defer_end();
    ImGui::EndFrame();
}

} // namespace

TEST_CASE("l'inspecteur dessine un widget par champ, en suivant la description du composant")
{
    const levain::ui::UiContext context = levain::ui::createUiContext(1.0f);
    flecs::world world;
    describeTestComponents(world);
    levain::editor::Inspector inspector = levain::editor::createInspector(world);
    const flecs::entity entity = world.entity("réglée")
                                     .set(Tuning{})
                                     .set(levain::scene::WorldTransform{})
                                     .set(editor_test::Weights{})
                                     .set(editor_test::Undescribed{})
                                     .add<editor_test::Tag>();

    // Large : quatre nombres par ligne de matrice (la largeur décide, `numbersPerRow`).
    const auto drawWide = [&]
    {
        ImGui::SetNextWindowSize({900.0f, 400.0f});
        levain::editor::drawInspector(world, inspector, entity, 0);
    };
    uiFrame(world, drawWide);
    uiFrame(world, drawWide);
    // Tuning : gain, count, enabled, mode, inner.a, inner.b, offset. WorldTransform : les quatre
    // colonnes de sa matrice. Weights : [0-3] et [4-5]. Le composant non décrit, l'étiquette et le
    // nom : une ligne chacun, aucun champ.
    CHECK(inspector.fieldsDrawn == 7 + 4 + 2);

    // Sans sélection, ou une entité détruite : aucun champ.
    entity.destruct();
    uiFrame(world, [&] { levain::editor::drawInspector(world, inspector, entity, 0); });
    CHECK(inspector.fieldsDrawn == 0);

    // `wrote` est celui de l'image : une image qui n'écrit rien le remet à faux.
    inspector.wrote = true;
    uiFrame(world, [&] { levain::editor::drawInspector(world, inspector, entity, 0); });
    CHECK_FALSE(inspector.wrote);
}

TEST_CASE("l'inspecteur, à 125 % et 150 % dans une fenêtre de 395 px : chaque valeur tient dans "
          "son cadre")
{
    // 395 px : la largeur de l'inspecteur dans la disposition par défaut à 125 % (captures de la
    // revue du morceau 17b). Une colonne de libellé fixe de 10 em y laissait 0,98 coupé en « 0.0( »
    // et le moins de -2.098 sous le repère rouge.
    for (const float scale : {1.0f, 1.25f, 1.5f})
    {
        INFO("échelle " << scale);
        const levain::ui::UiContext context = levain::ui::createUiContext(scale);
        flecs::world world;
        describeTestComponents(world);
        levain::editor::Inspector inspector = levain::editor::createInspector(world);
        const flecs::entity entity =
            world.entity("réglée")
                .set(Tuning{.offset = {-2.098f, 0.98f, -12.345f}})
                .set(levain::scene::WorldTransform{.matrix = glm::mat4{-2.098f}})
                .set(editor_test::Facing{});
        float widestValue = 0.0f;
        for (int frame = 0; frame < 3; ++frame)
        {
            uiFrame(world,
                    [&]
                    {
                        ImGui::SetNextWindowPos({0.0f, 0.0f});
                        ImGui::SetNextWindowSize({395.0f, 460.0f});
                        levain::editor::drawInspector(world, inspector, entity, 0);
                        widestValue = ImGui::CalcTextSize("-12.345").x;
                    });
        }
        REQUIRE(inspector.narrowestFrame < std::numeric_limits<float>::max());
        const ImGuiStyle& style = ImGui::GetStyle();
        // Le texte, le rembourrage des deux côtés et le repère d'axe, dessiné dans le cadre.
        const float needed = widestValue + (2.0f * style.FramePadding.x) + style.ColorMarkerSize;
        CHECK(inspector.narrowestFrame >= needed);
    }
}

TEST_CASE("numbersPerRow et labelColumnFor : le champ garde sa place avant le libellé")
{
    constexpr float Cell = 70.0f;
    constexpr float Gap = 6.0f;
    // Quatre cellules et leurs trois espaces : 298 ; un pixel de moins, trois.
    CHECK(levain::editor::numbersPerRow((4.0f * Cell) + (3.0f * Gap), Cell, Gap) == 4);
    CHECK(levain::editor::numbersPerRow((4.0f * Cell) + (3.0f * Gap) - 1.0f, Cell, Gap) == 3);
    CHECK(levain::editor::numbersPerRow(1000.0f, Cell, Gap) == 4); // jamais plus de quatre
    CHECK(levain::editor::numbersPerRow(10.0f, Cell, Gap) == 1);   // au moins un
    // Large : la colonne voulue. Étroit : elle rend la place au champ, sans passer sous son
    // minimum.
    CHECK(levain::editor::labelColumnFor(800.0f, 150.0f, 60.0f, Cell, Gap) == 150.0f);
    CHECK(levain::editor::labelColumnFor(300.0f, 150.0f, 60.0f, Cell, Gap) ==
          doctest::Approx(300.0f - (3.0f * Cell) - (2.0f * Gap)));
    CHECK(levain::editor::labelColumnFor(100.0f, 150.0f, 60.0f, Cell, Gap) == 60.0f);
}

TEST_CASE("un composant non décrit, une étiquette, une paire : une ligne à leur nom")
{
    flecs::world world;
    describeTestComponents(world);
    levain::editor::Inspector inspector = levain::editor::createInspector(world);
    const flecs::entity entity = world.entity("nommée").set(editor_test::Undescribed{});

    // La clé de sauvegarde, le nom C++ : jamais les octets de la valeur.
    CHECK(levain::editor::componentLabelOf(world, world.id<editor_test::Undescribed>()) ==
          "editor_test.Undescribed");
    CHECK(levain::editor::componentLabelOf(world, world.pair<flecs::Identifier>(flecs::Name)) ==
          "(Identifier,Name)");
    CHECK(levain::editor::inspectComponent(world, inspector, entity,
                                           world.id<editor_test::Undescribed>()) == 0);
}

TEST_CASE("une enum se lit par le nom de sa constante, un entier hors des constantes par aucun")
{
    flecs::world world;
    describeTestComponents(world);
    const flecs::entity_t mode = world.id<editor_test::Mode>();
    editor_test::Mode value = editor_test::Mode::Wild;
    CHECK(std::string_view{levain::editor::enumNameOf(world, mode, &value)} == "Wild");
    value = editor_test::Mode::Calm;
    CHECK(std::string_view{levain::editor::enumNameOf(world, mode, &value)} == "Calm");
    value = static_cast<editor_test::Mode>(7);
    CHECK(levain::editor::enumNameOf(world, mode, &value) == nullptr);
    CHECK(levain::editor::enumNameOf(world, world.id<float>(), &value) == nullptr); // pas une enum
}

TEST_CASE("commitEdit n'écrit qu'une donnée d'auteur qui a changé, en un seul OnSet")
{
    flecs::world world;
    describeTestComponents(world);
    const flecs::entity entity =
        world.entity("écrite").set(editor_test::Dial{}).set(editor_test::Gauge{});
    int dialSets = 0;
    int gaugeSets = 0;
    world.observer<editor_test::Dial>()
        .event(flecs::OnSet)
        .each([&](editor_test::Dial&) { ++dialSets; });
    world.observer<editor_test::Gauge>()
        .event(flecs::OnSet)
        .each([&](editor_test::Gauge&) { ++gaugeSets; });

    const editor_test::Dial before{};
    editor_test::Dial edited{.level = 0.75f};
    CHECK(
        levain::editor::commitEdit(world, entity, world.id<editor_test::Dial>(), &before, &edited));
    CHECK(dialSets == 1);
    CHECK(entity.get<editor_test::Dial>().level == 0.75f);
    // Rien n'a changé : pas d'écriture, donc pas d'OnSet.
    CHECK_FALSE(
        levain::editor::commitEdit(world, entity, world.id<editor_test::Dial>(), &edited, &edited));
    CHECK(dialSets == 1);

    // Une donnée en lecture seule n'est jamais écrite, quoi que la copie contienne.
    const editor_test::Gauge gaugeBefore{};
    editor_test::Gauge gaugeEdited{.level = 0.75f};
    CHECK_FALSE(levain::editor::commitEdit(world, entity, world.id<editor_test::Gauge>(),
                                           &gaugeBefore, &gaugeEdited));
    CHECK(gaugeSets == 0);
    CHECK(entity.get<editor_test::Gauge>().level == 0.5f);
}

namespace
{

/// Ce que fait une personne pour taper un nombre dans un champ glissable : Ctrl+clic, le texte,
/// Entrée. ImGui étale les événements sur plusieurs images (`ConfigInputTrickleEventQueue`) :
/// quatre images après chacun, le temps qu'il passe.
void typeInto(ImVec2 field, const char* text, const std::function<void()>& frame)
{
    ImGuiIO& io = ImGui::GetIO();
    const auto settle = [&]
    {
        for (int i = 0; i < 4; ++i)
        {
            frame();
        }
    };
    io.AddMousePosEvent(field.x, field.y);
    settle();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddMouseButtonEvent(0, true);
    settle();
    io.AddMouseButtonEvent(0, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    settle();
    io.AddInputCharactersUTF8(text);
    settle();
    io.AddKeyEvent(ImGuiKey_Enter, true);
    settle();
    io.AddKeyEvent(ImGuiKey_Enter, false);
    settle();
}

/// Un monde de test aux composants décrits, son inspecteur, et les images où l'on tape dans le
/// dernier champ d'un composant : `field` en est le centre, `fields` le nombre de champs dessinés,
/// `active` si l'un a été actif (Ctrl+clic, glissé : un champ grisé ne l'est jamais), `sets` les
/// `OnSet` de `entityWith`.
struct Typing
{
    const levain::ui::UiContext context = levain::ui::createUiContext(1.0f);
    flecs::world world;
    levain::editor::Inspector inspector;
    ImVec2 field{};
    int fields = 0;
    bool active = false;
    int sets = 0;

    Typing()
    {
        describeTestComponents(world);
        inspector = levain::editor::createInspector(world);
    }

    /// Une entité à `value`, dont les `OnSet` suivants sont comptés.
    template <class T> flecs::entity entityWith(const char* name, const T& value)
    {
        const flecs::entity entity = world.entity(name).set(value);
        world.observer<T>().event(flecs::OnSet).each([this](T&) { ++sets; });
        return entity;
    }

    /// Les champs de `T` dans une fenêtre à une place connue.
    template <class T> void frame(flecs::entity entity)
    {
        uiFrame(world,
                [&]
                {
                    ImGui::SetNextWindowPos({0.0f, 0.0f});
                    ImGui::SetNextWindowSize({400.0f, 200.0f});
                    ImGui::Begin("champ");
                    fields =
                        levain::editor::inspectComponent(world, inspector, entity, world.id<T>());
                    active = active || ImGui::IsItemActive();
                    const ImVec2 low = ImGui::GetItemRectMin();
                    const ImVec2 high = ImGui::GetItemRectMax();
                    field = {low.x + 10.0f, (low.y + high.y) / 2.0f};
                    ImGui::End();
                });
    }

    /// Taper `text` dans le champ, `across` pixels à droite de son bord : le suivant d'une ligne.
    template <class T> void type(flecs::entity entity, const char* text, float across = 0.0f)
    {
        frame<T>(entity); // le champ se place
        typeInto({field.x + across, field.y}, text, [&] { frame<T>(entity); });
    }
};

} // namespace

TEST_CASE("un nombre tapé dans le champ d'une donnée d'auteur est borné, et part en un seul OnSet")
{
    Typing typing;
    const flecs::entity entity = typing.entityWith("cadran", editor_test::Dial{});
    const auto level = [&] { return entity.get<editor_test::Dial>().level; };

    typing.frame<editor_test::Dial>(entity); // au repos, rien n'est écrit
    CHECK(typing.sets == 0);
    CHECK_FALSE(typing.inspector.wrote);
    typing.type<editor_test::Dial>(entity, "0.25");
    CHECK(level() == doctest::Approx(0.25f));
    CHECK(typing.sets == 1);
    CHECK(typing.inspector.wrote); // l'éditeur en demande la recomposition de l'image
    CHECK(typing.active); // le champ se tape : le pendant du test de la donnée en lecture seule
    // 5 dépasse la borne [0, 1] : Ctrl+clic tape au-delà sans AlwaysClamp.
    typing.type<editor_test::Dial>(entity, "5");
    CHECK(level() == 1.0f);
    CHECK(typing.sets == 2);
}

TEST_CASE("le champ d'une donnée en lecture seule est grisé : il ne se tape pas, rien n'est écrit")
{
    Typing typing;
    const flecs::entity entity = typing.entityWith("jauge", editor_test::Gauge{});

    typing.type<editor_test::Gauge>(entity, "0.9");
    CHECK_FALSE(typing.active); // le grisé, que la garde de `commitEdit` ne suffit pas à prouver
    CHECK(entity.get<editor_test::Gauge>().level == 0.5f);
    CHECK(typing.sets == 0);
    CHECK_FALSE(typing.inspector.wrote); // rien d'écrit, rien à recomposer
}

namespace
{

/// L'angle entre deux rotations, en degrés (-q est la même rotation). Par `atan2` : l'`acos` d'un
/// produit scalaire près de 1 perd en flottant de quoi voir un dixième de degré.
float degreesBetween(const glm::quat& left, const glm::quat& right)
{
    const glm::quat delta = glm::inverse(left) * right;
    return glm::degrees(
        2.0f * std::atan2(glm::length(glm::vec3{delta.x, delta.y, delta.z}), std::abs(delta.w)));
}

bool sameRotation(const glm::quat& left, const glm::quat& right)
{
    return degreesBetween(left, right) < 0.001f;
}

} // namespace

TEST_CASE("la borne d'un entier se lit vers l'intérieur : un plancher de 0,5 est 1")
{
    Typing typing;
    levain::scene::describeAuthored<editor_test::Tally>(typing.world)
        .range(&editor_test::Tally::n, 0.5, 10.5);
    const flecs::entity entity = typing.entityWith("compte", editor_test::Tally{});

    typing.type<editor_test::Tally>(entity, "0"); // tronquée, la borne 0,5 laisserait passer 0
    CHECK(entity.get<editor_test::Tally>().n == 1);
    typing.type<editor_test::Tally>(entity, "99");
    CHECK(entity.get<editor_test::Tally>().n == 10);
}

TEST_CASE("ImGui laisse taper « nan » et « inf » : l'inspecteur n'écrit jamais un nombre non fini")
{
    // `sscanf` sans filtre de caractères, et un NaN passe la borne (`DataTypeClamp` compare par <
    // et >). Écrit dans une position, il téléporte un corps de Jolt à NaN.
    SUBCASE("un champ borné")
    {
        Typing typing;
        const flecs::entity entity = typing.entityWith("cadran", editor_test::Dial{});
        typing.type<editor_test::Dial>(entity, "nan");
        CHECK(entity.get<editor_test::Dial>().level == 0.5f);
        CHECK(typing.sets == 0);
    }
    SUBCASE("un champ sans borne, par un nombre qui déborde en inf")
    {
        Typing typing;
        const flecs::entity entity = typing.entityWith("réglée", Tuning{});
        typing.type<Tuning>(entity, "1e39"); // le dernier champ de Tuning, `offset`, un vec3
        CHECK(entity.get<Tuning>().offset == glm::vec3{0.0f});
        CHECK(typing.sets == 0);
    }
    SUBCASE("un angle")
    {
        Typing typing;
        const flecs::entity entity = typing.entityWith("phare", editor_test::Facing{});
        typing.type<editor_test::Facing>(entity, "nan");
        CHECK(sameRotation(entity.get<editor_test::Facing>().rotation, glm::quat{1, 0, 0, 0}));
        CHECK(typing.sets == 0);
    }
}

TEST_CASE(
    "eulerHint lit le lacet sur ±180°, garde les angles tapés tant qu'ils donnent la rotation")
{
    using levain::editor::eulerHint;
    using levain::editor::rotationFromEuler;
    // Ce que le jeu écrit tourne le lacet d'abord (`applyFpsInput`, `turnTowards`) : -95° et 150°
    // de lacet se lisent tels quels, pas en (180°, -85°, 180°) comme `glm::eulerAngles`.
    const glm::vec3 turned =
        eulerHint(glm::angleAxis(glm::radians(-95.0f), glm::vec3{0, 1, 0}), {});
    CHECK(turned.x == doctest::Approx(0.0f));
    CHECK(turned.y == doctest::Approx(-95.0f));
    CHECK(turned.z == doctest::Approx(0.0f));
    const glm::quat camera = glm::angleAxis(glm::radians(150.0f), glm::vec3{0, 1, 0}) *
                             glm::angleAxis(glm::radians(-30.0f), glm::vec3{1, 0, 0});
    const glm::vec3 looking = eulerHint(camera, {});
    CHECK(looking.x == doctest::Approx(-30.0f));
    CHECK(looking.y == doctest::Approx(150.0f));

    // Un tangage de 100° dépasse les ±90° : le quaternion se relit en 80°, le lacet et le roulis
    // retournés. Les angles tapés restent tant qu'ils donnent encore la rotation.
    const glm::vec3 typed{100.0f, 0.0f, 0.0f};
    const glm::quat rotation = rotationFromEuler(typed);
    const glm::vec3 reread = eulerHint(rotation, std::nullopt);
    CHECK(reread.x == doctest::Approx(80.0f));
    CHECK(sameRotation(rotationFromEuler(reread), rotation));
    CHECK(eulerHint(rotation, typed) == typed);

    // À ±90° de tangage le lacet et le roulis se confondent : les angles lus donnent la rotation.
    const glm::quat locked = rotationFromEuler({90.0f, 20.0f, 30.0f});
    CHECK(sameRotation(rotationFromEuler(eulerHint(locked, {})), locked));

    // Quelqu'un d'autre a tourné l'entité : les angles tapés ne valent plus, la rotation se relit.
    const glm::vec3 now = eulerHint(rotationFromEuler({10.0f, 20.0f, 30.0f}), typed);
    CHECK(now.x == doctest::Approx(10.0f));
    CHECK(now.y == doctest::Approx(20.0f));
    CHECK(now.z == doctest::Approx(30.0f));

    // Une rotation nulle, ou à peine tournée, ne se lit jamais « -0.000 » à l'écran.
    const glm::vec3 none = eulerHint(glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, {});
    const glm::vec3 tiny = eulerHint(rotationFromEuler({0.0f, 0.0f, -0.0001f}), {});
    CHECK_FALSE(std::signbit(none.y));
    CHECK_FALSE(std::signbit(tiny.z));

    // Ce qui s'écrit est normalisé, quels que soient les angles.
    CHECK(glm::length(rotationFromEuler({1000.0f, -300.0f, 45.0f})) == doctest::Approx(1.0f));
}

TEST_CASE("les angles tapés restent affichés d'une modification à l'autre, la rotation écrite à "
          "chacune")
{
    Typing typing;
    const flecs::entity entity = typing.entityWith("phare", editor_test::Facing{});
    const auto rotation = [&] { return entity.get<editor_test::Facing>().rotation; };

    typing.type<editor_test::Facing>(entity, "200"); // le tangage, premier des trois angles
    CHECK(sameRotation(rotation(), levain::editor::rotationFromEuler({200.0f, 0.0f, 0.0f})));
    // Le lacet est le deuxième. 200° de tangage dépasse les ±90° que relit l'inspecteur : relu, le
    // quaternion donnerait -20° et deux autres angles, et le lacet tapé en tournerait un autre.
    // L'inspecteur garde les angles qui ont été tapés.
    typing.type<editor_test::Facing>(entity, "100", 90.0f);
    typing.type<editor_test::Facing>(entity, "10");
    CHECK(sameRotation(rotation(), levain::editor::rotationFromEuler({10.0f, 100.0f, 0.0f})));
    CHECK(typing.sets == 3); // une fois par modification, jamais par image
}

namespace
{

/// Les repères de X, Y et Z que dessine `ImGuiSliderFlags_ColorMarkers` : rouge, vert, bleu
/// (`GDefaultRgbaColorMarkers`, imgui_widgets.cpp). ImGui les écrit en dur : une mise à jour
/// d'ImGui qui les change fait échouer le test, et c'est voulu.
constexpr std::array<ImU32, 3> AxisMarkerColors = {
    IM_COL32(240, 20, 20, 255), IM_COL32(20, 240, 20, 255), IM_COL32(20, 20, 240, 255)};

/// La couleur d'un sommet sans son alpha : un composant en lecture seule (grisé) dessine ses
/// repères à 60 %, les franges de l'anticrénelage sont transparentes, et c'est la teinte qui
/// compte.
bool sameRgb(ImU32 a, ImU32 b)
{
    return (a & ~static_cast<ImU32>(IM_COL32_A_MASK)) == (b & ~static_cast<ImU32>(IM_COL32_A_MASK));
}

/// Où une couleur a été dessinée : le rectangle des sommets de la liste de dessin de cette teinte.
/// `found` faux : aucun sommet de cette teinte (pas un `std::optional`, que clang-tidy veut vérifié
/// à chaque accès, et que `REQUIRE` ne lui montre pas).
struct Spot
{
    bool found = false;
    float left = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;
};

Spot spotOf(const ImDrawList& list, ImU32 color)
{
    Spot spot;
    for (const ImDrawVert& vertex : list.VtxBuffer)
    {
        if (!sameRgb(vertex.col, color))
        {
            continue;
        }
        spot = spot.found ? Spot{.found = true,
                                 .left = std::min(spot.left, vertex.pos.x),
                                 .top = std::min(spot.top, vertex.pos.y),
                                 .bottom = std::max(spot.bottom, vertex.pos.y)}
                          : Spot{.found = true,
                                 .left = vertex.pos.x,
                                 .top = vertex.pos.y,
                                 .bottom = vertex.pos.y};
    }
    return spot;
}

/// Les repères X, Y, Z qu'un composant a fait dessiner, dans le style de l'éditeur (le repère n'a
/// de taille que par `ColorMarkerSize`), et la hauteur d'une ligne de champ.
struct AxisMarkers
{
    std::array<Spot, 3> spots;
    float rowHeight = 0.0f;
};

template <class T> AxisMarkers axisMarkersOf(Typing& typing, flecs::entity entity)
{
    levain::ui::applyEditorStyle(ImGui::GetStyle(), 1.0f);
    AxisMarkers markers;
    uiFrame(typing.world,
            [&]
            {
                ImGui::SetNextWindowPos({0.0f, 0.0f});
                ImGui::SetNextWindowSize({500.0f, 400.0f});
                ImGui::Begin("repères");
                levain::editor::inspectComponent(typing.world, typing.inspector, entity,
                                                 typing.world.id<T>());
                for (std::size_t axis = 0; axis < markers.spots.size(); ++axis)
                {
                    markers.spots.at(axis) =
                        spotOf(*ImGui::GetWindowDrawList(), AxisMarkerColors.at(axis));
                }
                markers.rowHeight = ImGui::GetFrameHeight();
                ImGui::End();
            });
    return markers;
}

} // namespace

TEST_CASE("un vecteur de trois composantes porte les repères X rouge, Y vert, Z bleu, d'une seule "
          "ligne")
{
    Typing typing;
    // Tuning a un vec3 parmi des nombres seuls, un booléen, une enum et un agrégat : seul le vec3
    // porte des repères, donc ils tiennent tous dans une seule ligne de champ.
    const AxisMarkers markers =
        axisMarkersOf<Tuning>(typing, typing.world.entity("réglée").set(Tuning{}));
    for (const Spot& spot : markers.spots)
    {
        REQUIRE(spot.found);
    }
    const Spot& redSpot = markers.spots.at(0);
    CHECK(redSpot.left < markers.spots.at(1).left); // X, Y, Z de gauche à droite
    CHECK(markers.spots.at(1).left < markers.spots.at(2).left);
    for (const Spot& spot : markers.spots)
    {
        CHECK(spot.top == doctest::Approx(redSpot.top).epsilon(0.01));
        // la frange d'anticrénelage dépasse d'un pixel
        CHECK(spot.bottom - spot.top <= markers.rowHeight + 2.0f);
    }
}

TEST_CASE("un vecteur de deux composantes porte X et Y, une rotation X, Y et Z")
{
    Typing typing;
    const AxisMarkers pan =
        axisMarkersOf<editor_test::Pan>(typing, typing.world.entity("a").set(editor_test::Pan{}));
    CHECK(pan.spots.at(0).found);
    CHECK(pan.spots.at(1).found);
    CHECK_FALSE(pan.spots.at(2).found); // pas de Z : un repère bleu sans axe serait faux
    const AxisMarkers facing = axisMarkersOf<editor_test::Facing>(
        typing, typing.world.entity("b").set(editor_test::Facing{}));
    CHECK(facing.spots.at(0).found);
    CHECK(facing.spots.at(1).found);
    CHECK(facing.spots.at(2).found);
}

TEST_CASE("un nombre seul et une ligne de tableau n'ont aucun repère : ce ne sont pas des axes")
{
    Typing typing;
    const AxisMarkers dial =
        axisMarkersOf<editor_test::Dial>(typing, typing.world.entity("c").set(editor_test::Dial{}));
    const AxisMarkers weights = axisMarkersOf<editor_test::Weights>(
        typing, typing.world.entity("d").set(editor_test::Weights{}));
    for (const AxisMarkers* markers : {&dial, &weights})
    {
        for (const Spot& spot : markers->spots)
        {
            CHECK_FALSE(spot.found);
        }
    }
}

namespace
{

/// Une couleur dans ce qu'ImGui a dessiné à la dernière image (`Render` a tout rassemblé).
bool drawn(ImU32 color)
{
    const ImDrawData* data = ImGui::GetDrawData();
    for (const ImDrawList* list : data->CmdLists)
    {
        for (const ImDrawVert& vertex : list->VtxBuffer)
        {
            if (sameRgb(vertex.col, color))
            {
                return true;
            }
        }
    }
    return false;
}

} // namespace

TEST_CASE("la ligne choisie de la hiérarchie prend l'accent, les autres restent grises")
{
    const levain::ui::UiContext context = levain::ui::createUiContext(1.0f);
    levain::ui::applyEditorStyle(ImGui::GetStyle(), 1.0f);
    flecs::world world;
    world.entity("première").set(levain::scene::Transform{});
    const flecs::entity second = world.entity("seconde").set(levain::scene::Transform{});
    levain::editor::Hierarchy hierarchy = levain::editor::createHierarchy(world);
    const ImU32 accent = ImGui::ColorConvertFloat4ToU32(levain::ui::EditorAccentDark);
    const auto frames = [&](flecs::entity_t selected)
    {
        for (int frame = 0; frame < 3; ++frame) // une fenêtre neuve se montre à la deuxième image
        {
            uiFrame(world,
                    [&]
                    {
                        ImGui::SetNextWindowPos({0.0f, 0.0f});
                        ImGui::SetNextWindowSize({300.0f, 200.0f});
                        levain::editor::drawHierarchy(world, hierarchy, selected, 0);
                    });
            ImGui::Render();
        }
    };
    frames(0);
    CHECK_FALSE(drawn(accent));
    frames(second.id());
    CHECK(drawn(accent));
}

TEST_CASE("une entité se lit comme un flecs::entity, par son chemin, vivante ou non")
{
    flecs::world world;
    const flecs::entity target = world.entity("ancre::bouée");
    const editor_test::Beacon beacon{.target = target};
    CHECK(levain::editor::entityFieldOf(world, &beacon.target) == target);
    CHECK(levain::editor::entityLabelOf(levain::editor::entityFieldOf(world, &beacon.target)) ==
          "ancre::bouée");
    const editor_test::Beacon none{};
    CHECK(levain::editor::entityLabelOf(levain::editor::entityFieldOf(world, &none.target)) ==
          "aucune");
    target.destruct();
    CHECK(levain::editor::entityLabelOf(levain::editor::entityFieldOf(world, &beacon.target)) ==
          "(détruite)");
}

TEST_CASE("les textes que l'inspecteur construit à l'exécution passent par le catalogue")
{
    flecs::world world;
    const flecs::entity target = world.entity("ancre");
    const editor_test::Beacon beacon{.target = target};
    const editor_test::Beacon none{};
    target.destruct();
    levain::core::activeCatalog().entries = {
        {"aucune", "none"}, {"(détruite)", "(destroyed)"}, {"aucun", "no asset"}};
    const auto entityText = [&world](const flecs::entity& field)
    { return levain::editor::entityLabelOf(levain::editor::entityFieldOf(world, &field)); };
    CHECK(entityText(none.target) == "none");
    CHECK(entityText(beacon.target) == "(destroyed)");
    CHECK(levain::editor::assetNameOf(nullptr, levain::assets::AssetRef{}) == "no asset");
    levain::core::activeCatalog().entries.clear(); // vide, hors de ce cas
}

TEST_CASE("un asset se nomme par son fichier au registre, sinon par son GUID, en lecture seule")
{
    using levain::assets::AssetRef;
    levain::assets::AssetRegistry registry;
    const levain::assets::AssetId known = levain::assets::generateAssetId();
    const levain::assets::AssetId unknown = levain::assets::generateAssetId();
    registry.entries[known].file = "models/Fox.gltf";
    CHECK(levain::editor::assetNameOf(&registry, AssetRef{known, 0}) == "Fox.gltf #0");
    CHECK(levain::editor::assetNameOf(&registry, AssetRef{unknown, 2}) ==
          levain::assets::toString(unknown) + " #2");
    CHECK(levain::editor::assetNameOf(nullptr, AssetRef{known, 1}) ==
          levain::assets::toString(known) + " #1");
    CHECK(levain::editor::assetNameOf(&registry, AssetRef{}) == "aucun");

    // MeshRef (describe, non Authored) : un seul champ, l'asset, sans ses deux entiers de 64 bits.
    const levain::ui::UiContext context = levain::ui::createUiContext(1.0f);
    flecs::world world;
    world.import<levain::scene::SceneModule>();
    world.import<levain::assets::AssetsModule>();
    levain::editor::Inspector inspector = levain::editor::createInspector(world, &registry);
    const flecs::entity entity =
        world.entity("renard").set(levain::assets::MeshRef{.mesh = AssetRef{known, 0}});
    int fields = 0;
    uiFrame(world,
            [&]
            {
                ImGui::Begin("champs");
                fields = levain::editor::inspectComponent(world, inspector, entity,
                                                          world.id<levain::assets::MeshRef>());
                ImGui::End();
            });
    CHECK(fields == 1);
}

TEST_CASE("un composant neuf, une seule déclaration : tous ses champs dessinés, éditable")
{
    Typing typing;
    // LA déclaration : la seule ligne qu'écrit le module qui possède le composant.
    levain::scene::describeAuthored<editor_test::Beacon>(typing.world)
        .range(&editor_test::Beacon::intensity, 0.0, 1.0);
    const flecs::entity entity = typing.entityWith("balise", editor_test::Beacon{});

    typing.type<editor_test::Beacon>(entity, "5");
    CHECK(typing.fields == 4); // la rotation, l'entité, l'enum et le nombre
    CHECK(entity.get<editor_test::Beacon>().intensity == 1.0f); // la borne
    CHECK(typing.sets == 1);
}

TEST_CASE(
    "l'Édition arrête la simulation et le frame du programme, la route suit le mode et le focus")
{
    using levain::app::InputRoute;
    using levain::editor::Mode;
    CHECK_FALSE(levain::editor::simulationRunsIn(Mode::Edit, false));
    CHECK(levain::editor::simulationRunsIn(Mode::Play, false));
    CHECK_FALSE(levain::editor::programFrameRunsIn(Mode::Edit, false, false));
    CHECK(levain::editor::programFrameRunsIn(Mode::Play, false, false));
    // Édition : rien au jeu, que la scène ait le focus ou non.
    CHECK(levain::editor::inputRouteFor(Mode::Edit, true) == InputRoute::Editor);
    CHECK(levain::editor::inputRouteFor(Mode::Edit, false) == InputRoute::Editor);
    // Jeu : au jeu si la scène a le focus ; sinon à l'UI, qu'un champ de texte actif ne soit pas
    // une touche de vol.
    CHECK(levain::editor::inputRouteFor(Mode::Play, true) == InputRoute::Game);
    CHECK(levain::editor::inputRouteFor(Mode::Play, false) == InputRoute::Ui);
    CHECK(levain::editor::otherMode(Mode::Edit) == Mode::Play);
    CHECK(levain::editor::otherMode(Mode::Play) == Mode::Edit);
}

namespace
{

/// L'état des modes, chaque champ nommé : le dernier (`keys`) est un défaut, et les initialiseurs
/// désignés les veulent tous.
levain::editor::ModeState modeIn(levain::editor::Mode mode, bool paused = false,
                                 bool mouseFreed = false)
{
    levain::editor::ModeState state;
    state.current = mode;
    state.paused = paused;
    state.mouseFreed = mouseFreed;
    return state;
}

} // namespace

TEST_CASE("la pause arrête la simulation et le frame du programme, et le jeu ne reçoit plus rien")
{
    using levain::app::InputRoute;
    using levain::editor::Mode;
    using levain::editor::ModeState;
    // En pause : ni pas ni frame du programme. Une pause n'existe pas en Édition, où rien ne
    // tourne de toute façon.
    CHECK_FALSE(levain::editor::simulationRunsIn(Mode::Play, true));
    CHECK_FALSE(levain::editor::programFrameRunsIn(Mode::Play, true, false));
    // Un pas en pause fait tourner `frame` : ce qu'il pose (l'input, vide en pause) est ce que le
    // pas lit, non l'input d'avant la pause. Pas en Édition, où un pas est refusé.
    CHECK(levain::editor::programFrameRunsIn(Mode::Play, true, true));
    CHECK_FALSE(levain::editor::programFrameRunsIn(Mode::Edit, false, true));
    CHECK_FALSE(levain::editor::simulationRunsIn(Mode::Edit, true));
    // La route : la pause comme la souris rendue (Maj+F1) ôtent le jeu de l'input, la scène
    // focalisée ou non ; sinon la route du mode.
    const ModeState playing = modeIn(Mode::Play);
    const ModeState paused = modeIn(Mode::Play, true);
    const ModeState freed = modeIn(Mode::Play, false, true);
    CHECK(levain::editor::inputRouteOf(playing, true) == InputRoute::Game);
    CHECK(levain::editor::inputRouteOf(playing, false) == InputRoute::Ui);
    CHECK(levain::editor::inputRouteOf(paused, true) == InputRoute::Editor);
    CHECK(levain::editor::inputRouteOf(freed, true) == InputRoute::Editor);
    CHECK(levain::editor::inputRouteOf(modeIn(Mode::Edit), true) == InputRoute::Editor);
}

TEST_CASE("Simuler : la simulation et le frame du programme tournent, sous l'input vide de "
          "l'éditeur et sa caméra, tant que la pause ne les arrête pas")
{
    using levain::app::InputRoute;
    using levain::editor::editorOwnsViewIn;
    using levain::editor::Mode;
    // La simulation et le `frame` : comme Jouer, pause et pas compris. Le `frame` y tourne pour
    // lire un input vide (route *éditeur*), sans quoi le dernier input de Jouer serait rejoué.
    CHECK(levain::editor::simulationRunsIn(Mode::Simulate, false));
    CHECK_FALSE(levain::editor::simulationRunsIn(Mode::Simulate, true));
    CHECK(levain::editor::programFrameRunsIn(Mode::Simulate, false, false));
    CHECK_FALSE(levain::editor::programFrameRunsIn(Mode::Simulate, true, false));
    CHECK(levain::editor::programFrameRunsIn(Mode::Simulate, true, true));
    // Le jeu ne reçoit rien, la scène focalisée ou non ; la pause n'y change rien.
    CHECK(levain::editor::inputRouteFor(Mode::Simulate, true) == InputRoute::Editor);
    CHECK(levain::editor::inputRouteFor(Mode::Simulate, false) == InputRoute::Editor);
    CHECK(levain::editor::inputRouteOf(modeIn(Mode::Simulate), true) == InputRoute::Editor);
    CHECK(levain::editor::inputRouteOf(modeIn(Mode::Simulate, true), true) == InputRoute::Editor);
    CHECK(levain::editor::otherMode(Mode::Simulate) == Mode::Edit);
    // La caméra et les gestes sont ceux de l'éditeur en Édition et en Simuler, ceux du jeu en
    // Jouer.
    CHECK(editorOwnsViewIn(Mode::Edit));
    CHECK(editorOwnsViewIn(Mode::Simulate));
    CHECK_FALSE(editorOwnsViewIn(Mode::Play));
    CHECK(levain::editor::helpersVisibleIn(Mode::Simulate, false));
    CHECK_FALSE(levain::editor::helpersVisibleIn(Mode::Simulate, true));
    CHECK_FALSE(levain::editor::helpersVisibleIn(Mode::Play, false));
    // Le mot de la ligne `editor.mode` que lit la CI, et le nom traduit du mode.
    CHECK(std::string_view{levain::editor::modeTokenOf(Mode::Simulate)} == "simulate");
    CHECK(std::string_view{levain::editor::modeNameOf(Mode::Simulate)} == "Simuler");
}

TEST_CASE("le frame du programme voit la durée d'un pas sur un pas en pause, le temps mesuré sinon")
{
    using levain::editor::programFrameSecondsOf;
    constexpr float Measured = 0.5f;
    constexpr float Step = 1.0f / 60.0f;
    // Un pas en pause : le monde avance d'un pas fixe, le programme le voit.
    CHECK(programFrameSecondsOf(true, true, Measured, Step) == Step);
    // En marche (pas demandé ou non : `planFrame` l'ignore), ou en pause sans pas : le temps
    // mesuré.
    CHECK(programFrameSecondsOf(false, false, Measured, Step) == Measured);
    CHECK(programFrameSecondsOf(false, true, Measured, Step) == Measured);
    CHECK(programFrameSecondsOf(true, false, Measured, Step) == Measured);
}

TEST_CASE("la teinte du jeu : rien en Édition, une teinte qui joue, une autre en pause")
{
    using levain::editor::Mode;
    using levain::editor::playTintOf;
    CHECK_FALSE(playTintOf(Mode::Edit, false).has_value());
    CHECK_FALSE(playTintOf(Mode::Edit, true).has_value()); // une pause d'Édition n'existe pas
    // `value_or` : clang-tidy ne suit pas `REQUIRE`, et lit `.value()` comme un accès non gardé
    // (build/GOTCHA.md). Les deux teintes existent, les `CHECK` suivants le disent par leurs
    // couleurs.
    CHECK(playTintOf(Mode::Play, false).has_value());
    CHECK(playTintOf(Mode::Play, true).has_value());
    const ImVec4 playing = playTintOf(Mode::Play, false).value_or(ImVec4{});
    const ImVec4 paused = playTintOf(Mode::Play, true).value_or(ImVec4{});
    // Les couleurs du style, non des littéraux d'ici ; et deux teintes qu'on distingue d'un coup
    // d'œil : une autre couleur, pas une nuance de la première (la teinte du jeu est chaude, celle
    // de la pause froide).
    CHECK(playing.x == levain::ui::PlayTint.x);
    CHECK(paused.x == levain::ui::PausedTint.x);
    CHECK(playing.x > playing.z); // plus de rouge que de bleu
    CHECK(paused.z > paused.x);   // plus de bleu que de rouge
}

TEST_CASE("Simuler a la teinte de Jouer, et celle de la pause en pause : le choix est celui-là")
{
    using levain::editor::Mode;
    using levain::editor::playTintOf;
    // Une couleur de plus voudrait son contraste mesuré contre le ciel, le sol et les panneaux :
    // Simuler dit déjà qui il est par son bandeau, et la teinte dit que le monde bouge.
    for (const bool paused : {false, true})
    {
        const ImVec4 play = playTintOf(Mode::Play, paused).value_or(ImVec4{});
        const ImVec4 simulate = playTintOf(Mode::Simulate, paused).value_or(ImVec4{});
        CHECK(play.w > 0.0f); // le test n'est pas vide : la teinte de Jouer existe
        CHECK(simulate.x == play.x);
        CHECK(simulate.y == play.y);
        CHECK(simulate.z == play.z);
        CHECK(simulate.w == play.w);
    }
}

namespace
{

constexpr std::uint16_t ScanEscape = 41, ScanF5 = 62, ScanF1 = 58, ScanPause = 72, ScanW = 26,
                        ScanQ = 20, ScanZ = 29;

/// Une touche pressée ou relâchée telle que la fenêtre la rend (`UiEventType::Key`).
levain::platform::UiEvent keyEvent(std::uint16_t scancode, bool down,
                                   levain::platform::KeyModifiers modifiers = {})
{
    return {.type = levain::platform::UiEventType::Key,
            .keycode = scancode | (1U << 30U),
            .scancode = scancode,
            .down = down,
            .modifiers = modifiers};
}

levain::platform::Events eventsOf(std::vector<levain::platform::UiEvent> keys)
{
    levain::platform::Events events;
    events.ui = std::move(keys);
    return events;
}

} // namespace

TEST_CASE("Échap arrête à son appui, pas tant qu'elle est tenue, Maj tenu ou non")
{
    levain::editor::ModeState state = modeIn(levain::editor::Mode::Play);
    const auto table = levain::editor::defaultShortcuts();
    using levain::editor::gameKeysPressed;
    CHECK_FALSE(gameKeysPressed(state, eventsOf({}), table, false).stop);
    CHECK(gameKeysPressed(state, eventsOf({keyEvent(ScanEscape, true)}), table, false).stop);
    // Tenue, elle se répète (SDL) : le front est passé, Échap n'arrête pas de nouveau.
    CHECK_FALSE(gameKeysPressed(state, eventsOf({keyEvent(ScanEscape, true)}), table, false).stop);
    CHECK_FALSE(gameKeysPressed(state, eventsOf({}), table, false).stop);
    CHECK_FALSE(gameKeysPressed(state, eventsOf({keyEvent(ScanEscape, false)}), table, false).stop);
    // Un nouvel appui, un modificateur tenu : Échap ne lit aucun modificateur (`isHeldAction`).
    CHECK(gameKeysPressed(state, eventsOf({keyEvent(ScanEscape, true, {.shift = true})}), table,
                          false)
              .stop);
    // Appuyée et relâchée dans la même image : le front y est (l'ancien état « tenu » le perdait).
    CHECK(gameKeysPressed(state,
                          eventsOf({keyEvent(ScanF5, true), keyEvent(ScanEscape, false),
                                    keyEvent(ScanEscape, true), keyEvent(ScanEscape, false)}),
                          table, false)
              .stop);
}

TEST_CASE("les touches du jeu se lisent à la place que la table leur donne, non en dur")
{
    using levain::editor::Action;
    using levain::editor::gameKeysPressed;
    levain::editor::ModeState state = modeIn(levain::editor::Mode::Play);
    levain::editor::ShortcutTable table;
    table.bindings = {{.action = Action::Stop,
                       .context = levain::editor::Context::Game,
                       .chords = {levain::editor::positionChord(ScanQ), {}}},
                      {.action = Action::Pause,
                       .context = levain::editor::Context::Game,
                       .chords = {levain::editor::positionChord(ScanW), {}}},
                      {.action = Action::NextFrame,
                       .context = levain::editor::Context::Game,
                       .chords = {levain::editor::letterChord('n'), {}}}};
    // Échap n'est plus l'arrêt, la touche à la position de Q l'est.
    CHECK_FALSE(gameKeysPressed(state, eventsOf({keyEvent(ScanEscape, true)}), table, false).stop);
    CHECK(gameKeysPressed(state, eventsOf({keyEvent(ScanQ, true)}), table, false).stop);
    // Pause est une position : celle du W d'un QWERTY, que la lettre marquée dessus soit « w » ou
    // « z » (AZERTY) ; la touche qui tape « w » ailleurs (la position de Z) ne pause pas.
    auto atW = keyEvent(ScanW, true);
    atW.keycode = 'z'; // AZERTY : la touche à la place du W porte un Z
    CHECK(gameKeysPressed(state, eventsOf({atW}), table, false).actions ==
          std::vector<Action>{Action::Pause});
    atW.down = false;
    static_cast<void>(gameKeysPressed(state, eventsOf({atW}), table, false));
    auto atZ = keyEvent(ScanZ, true);
    atZ.keycode = 'w'; // AZERTY : la touche à la place du Z porte un W
    CHECK(gameKeysPressed(state, eventsOf({atZ}), table, false).actions.empty());
    // Image suivante, ici sur une lettre : la lettre de la disposition, quelle que soit la
    // position.
    auto typedN = keyEvent(ScanF5, true);
    typedN.keycode = 'n';
    CHECK(gameKeysPressed(state, eventsOf({typedN}), table, false).actions ==
          std::vector<Action>{Action::NextFrame});
}

TEST_CASE("un clic est un bouton de la souris enfoncé : ni relâchement, ni mouvement, ni molette")
{
    using levain::editor::mousePressedIn;
    using levain::platform::UiEvent;
    using levain::platform::UiEventType;
    CHECK_FALSE(mousePressedIn(eventsOf({})));
    CHECK_FALSE(mousePressedIn(eventsOf({{.type = UiEventType::MouseMoved, .x = 3.0f, .y = 4.0f},
                                         {.type = UiEventType::MouseWheel},
                                         {.type = UiEventType::MouseButton, .down = false},
                                         keyEvent(ScanW, true)})));
    CHECK(
        mousePressedIn(eventsOf({{.type = UiEventType::MouseMoved},
                                 {.type = UiEventType::MouseButton, .button = 3, .down = true}})));
}

TEST_CASE("Maj+F1 rend la souris, F1 seule non, et les modificateurs sont exacts")
{
    using levain::editor::Action;
    using levain::editor::gameKeysPressed;
    levain::editor::ModeState state = modeIn(levain::editor::Mode::Play);
    const auto table = levain::editor::defaultShortcuts();
    CHECK(gameKeysPressed(state, eventsOf({keyEvent(ScanF1, true, {.shift = true})}), table, false)
              .actions == std::vector<Action>{Action::GiveMouseBack});
    CHECK(
        gameKeysPressed(state, eventsOf({keyEvent(ScanF1, false)}), table, false).actions.empty());
    // F1 seule ouvre les panneaux (`togglesPanels`) : elle ne rend pas la souris. Ctrl+Maj+F1 non
    // plus.
    CHECK(gameKeysPressed(state, eventsOf({keyEvent(ScanF1, true)}), table, false).actions.empty());
    CHECK(
        gameKeysPressed(state, eventsOf({keyEvent(ScanF1, false)}), table, false).actions.empty());
    CHECK(gameKeysPressed(state, eventsOf({keyEvent(ScanF1, true, {.ctrl = true, .shift = true})}),
                          table, false)
              .actions.empty());
    static_cast<void>(gameKeysPressed(state, eventsOf({keyEvent(ScanF1, false)}), table, false));
    // La touche Pause, et l'ordre des appuis d'une image.
    CHECK(gameKeysPressed(state,
                          eventsOf({keyEvent(ScanPause, true), keyEvent(ScanPause, false),
                                    keyEvent(ScanF1, true, {.shift = true})}),
                          table, false)
              .actions == std::vector<Action>{Action::Pause, Action::GiveMouseBack});
}

TEST_CASE(
    "les touches du jeu ne partent ni en Édition, ni dans un champ de texte, mais sont suivies")
{
    using levain::editor::gameKeysPressed;
    using levain::editor::Mode;
    const auto table = levain::editor::defaultShortcuts();
    levain::editor::ModeState edit = modeIn(Mode::Edit);
    const auto pressed = eventsOf({keyEvent(ScanEscape, true), keyEvent(ScanPause, true)});
    const auto none = gameKeysPressed(edit, pressed, table, false);
    CHECK_FALSE(none.stop);
    CHECK(none.actions.empty());
    // Échap tenue depuis l'Édition : le front a eu lieu, le jeu qui commence ne le revoit pas.
    CHECK(edit.keys.test(ScanEscape));
    levain::editor::ModeState play = modeIn(Mode::Play);
    play.keys = edit.keys;
    CHECK_FALSE(gameKeysPressed(play, eventsOf({keyEvent(ScanEscape, true)}), table, false).stop);

    // Un champ de texte actif garde Échap (elle y annule la frappe) et les autres touches.
    levain::editor::ModeState typing = modeIn(Mode::Play);
    const auto inField = gameKeysPressed(typing, pressed, table, true);
    CHECK_FALSE(inField.stop);
    CHECK(inField.actions.empty());
    CHECK(typing.keys.test(ScanEscape)); // suivie quand même
}

TEST_CASE("Alt+P joue, Échap arrête, seul le bouton va dans les deux sens")
{
    using levain::editor::Mode;
    using levain::editor::modeRequested;
    using levain::editor::ModeRequests;
    constexpr Mode Edit = Mode::Edit;
    constexpr Mode Play = Mode::Play;
    // Alt+P : de l'Édition au jeu, jamais l'inverse (sous la route jeu, ImGui ne la reçoit pas :
    // un retour par Alt+P ne marcherait qu'à certaines images).
    CHECK(modeRequested(Edit, {.playShortcut = true}) == Play);
    CHECK_FALSE(modeRequested(Play, {.playShortcut = true}).has_value());
    // Échap : du jeu à l'Édition, et rien en Édition.
    CHECK(modeRequested(Play, {.stop = true}) == Edit);
    CHECK_FALSE(modeRequested(Edit, {.stop = true}).has_value());
    // Le bouton propose l'autre mode ; un bouton qui propose le mode courant ne change rien.
    CHECK(modeRequested(Edit, {.toolbar = Play}) == Play);
    CHECK(modeRequested(Play, {.toolbar = Edit}) == Edit);
    CHECK_FALSE(modeRequested(Play, {.toolbar = Play}).has_value());
    CHECK_FALSE(modeRequested(Edit, ModeRequests{}).has_value());
    // Échap l'emporte sur le reste : sortir du jeu ne se dispute pas.
    CHECK(modeRequested(Play, {.playShortcut = true, .stop = true}) == Edit);
    CHECK(modeRequested(Play, {.toolbar = Play, .stop = true}) == Edit);
}

TEST_CASE("le rappel de la barre de mode suit la table, et ne promet pas d'Alt+P pour revenir")
{
    using levain::editor::Mode;
    const auto qwerty = levain::platform::qwertyKeyAtPosition;
    levain::editor::ShortcutTable table = levain::editor::defaultShortcuts();
    CHECK(levain::editor::modeHintOf(Mode::Edit, table, qwerty) == "Alt+P : jouer");
    // En jeu, seule Échap ramène : Alt+P n'y fait rien (`modeRequested`).
    const std::string inPlay = levain::editor::modeHintOf(Mode::Play, table, qwerty);
    CHECK(inPlay == "Échap : arrêter");
    CHECK_FALSE(inPlay.contains("Alt+P"));
    CHECK(levain::editor::modeHintOf(Mode::Simulate, table, qwerty) == inPlay); // sortir : Échap
    // Un accord changé change le rappel : jouer sur Ctrl+K, arrêter sur la position du F5 (62).
    levain::editor::replaceChord(table, levain::editor::Action::Play, 0,
                                 levain::editor::letterChord('k', {.ctrl = true}), qwerty);
    levain::editor::replaceChord(table, levain::editor::Action::Stop, 0,
                                 levain::editor::positionChord(ScanF5), qwerty);
    CHECK(levain::editor::modeHintOf(Mode::Edit, table, qwerty) == "Ctrl+K : jouer");
    CHECK(levain::editor::modeHintOf(Mode::Play, table, qwerty) == "F5 : arrêter");
    // Sans accord, rien n'est promis.
    levain::editor::replaceChord(table, levain::editor::Action::Play, 0, {}, qwerty);
    CHECK(levain::editor::modeHintOf(Mode::Edit, table, qwerty).empty());
}

namespace
{

/// Un clic de la souris à `at`, étalé sur les images qu'ImGui met à lire ses événements.
/// `framesAfterRelease` : combien d'images après le relâchement ; une seule est l'image qui reçoit
/// le clic (le bouton agit au relâchement), de quoi vérifier ce qui se passe « dans l'image même ».
void clickAt(ImVec2 at, const std::function<void()>& frame, int framesAfterRelease = 4)
{
    ImGuiIO& io = ImGui::GetIO();
    const auto settle = [&](int frames)
    {
        for (int i = 0; i < frames; ++i)
        {
            frame();
        }
    };
    io.AddMousePosEvent(at.x, at.y);
    settle(4);
    io.AddMouseButtonEvent(0, true);
    settle(4);
    io.AddMouseButtonEvent(0, false);
    settle(framesAfterRelease);
}

void push(levain::core::LogRing& ring, levain::core::LogLevel level, const char* text)
{
    levain::core::pushLogEntry(ring,
                               {.level = level, .category = "test", .text = text, .seconds = 0.0});
}

/// La console dans une fenêtre à une place connue, comme `Typing::frame` pour l'inspecteur.
struct ConsoleBench
{
    const levain::ui::UiContext context = levain::ui::createUiContext(1.0f);
    flecs::world world;
    levain::core::LogRing ring{200};
    levain::editor::Console console;
    float width = 630.0f; ///< la fenêtre : 24 % d'un écran de 1280 est une colonne de 307 px

    void frame()
    {
        uiFrame(world,
                [&]
                {
                    ImGui::SetNextWindowPos({0.0f, 0.0f});
                    ImGui::SetNextWindowSize({width, 220.0f});
                    levain::editor::drawConsole(console, ring, 0);
                });
    }

    void settle()
    {
        for (int i = 0; i < 4; ++i)
        {
            frame();
        }
    }

    void click(ImVec2 at, int framesAfterRelease = 4)
    {
        clickAt(at, [this] { frame(); }, framesAfterRelease);
    }
};

} // namespace

TEST_CASE(
    "la console : les cases filtrent et comptent, Réduire, la recherche tapée et Effacer, au clic")
{
    using levain::core::LogLevel;
    ConsoleBench bench;
    push(bench.ring, LogLevel::Info, "chargé");
    push(bench.ring, LogLevel::Warning, "lent");
    push(bench.ring, LogLevel::Error, "panne");
    push(bench.ring, LogLevel::Error, "panne");
    bench.settle();
    CHECK(bench.console.rows.size() == 4);
    CHECK(bench.console.snapshot.counts.errors == 2);
    // Ce que l'utilisateur lit sur les cases : les comptes de tous les messages reçus.
    CHECK(bench.console.labels ==
          std::array<std::string, 3>{"Infos : 1", "Avertissements : 1", "Erreurs : 2"});

    // Décocher « Erreurs » retire les deux erreurs, et la recocher les rend ; ni l'une ni l'autre
    // case des deux autres niveaux ne bouge.
    bench.click(bench.console.layout.error);
    CHECK_FALSE(bench.console.error);
    CHECK(bench.console.info);
    CHECK(bench.console.warning);
    CHECK(bench.console.rows.size() == 2);
    bench.click(bench.console.layout.error);
    CHECK(bench.console.rows.size() == 4);
    bench.click(bench.console.layout.warning);
    CHECK_FALSE(bench.console.warning);
    CHECK(bench.console.rows.size() == 3);
    bench.click(bench.console.layout.warning);
    bench.click(bench.console.layout.info);
    CHECK_FALSE(bench.console.info);
    CHECK(bench.console.rows.size() == 3);
    bench.click(bench.console.layout.info);

    // Réduire : les deux « panne » qui se suivent ne font qu'une ligne, comptée deux fois.
    bench.click(bench.console.layout.collapse);
    REQUIRE(bench.console.rows.size() == 3);
    CHECK(bench.console.rows.back().repeats == 2);

    // La recherche est tapée dans le champ : elle filtre à chaque lettre.
    bench.click(bench.console.layout.search);
    ImGui::GetIO().AddInputCharactersUTF8("LEN");
    bench.settle();
    REQUIRE(bench.console.rows.size() == 1);
    CHECK(bench.console.snapshot.entries.at(bench.console.rows.front().entry).text == "lent");

    // Effacer vide l'anneau et les comptes dans l'image qui reçoit le clic (une seule image après
    // le relâchement) : la liste et les cases de cette image sont celles d'après l'effacement.
    bench.click(bench.console.layout.clear, 1);
    CHECK(bench.console.rows.empty());
    CHECK(bench.console.snapshot.counts.errors == 0);
    CHECK(bench.console.labels ==
          std::array<std::string, 3>{"Infos : 0", "Avertissements : 0", "Erreurs : 0"});
    CHECK(levain::core::snapshotOf(bench.ring).entries.empty());
}

TEST_CASE("la console tient dans une colonne étroite : chaque case, le bouton et le champ de "
          "recherche restent dans la fenêtre")
{
    using levain::core::LogLevel;
    // La colonne gauche de l'éditeur : 24 % d'un écran de 1280 px, moins que la barre d'outils sur
    // une seule ligne (environ 500 px).
    ConsoleBench bench;
    bench.width = 300.0f;
    push(bench.ring, LogLevel::Error, "panne");
    bench.settle();
    const levain::editor::ConsoleLayout& layout = bench.console.layout;
    for (const ImVec2 center :
         {layout.clear, layout.collapse, layout.info, layout.warning, layout.error, layout.search})
    {
        CHECK(center.x > 0.0f);
        CHECK(center.x < bench.width);
        CHECK(center.y > 0.0f);
        CHECK(center.y < 220.0f);
    }
    // Le champ garde presque toute la largeur : ImGui lirait une largeur négative comme « jusqu'au
    // bord droit » et le réduirait à un pixel.
    CHECK(layout.searchWidth > bench.width - 40.0f);
    // Et la dernière case se coche : son centre est cliquable, pas rogné par le bord.
    bench.click(layout.error);
    CHECK_FALSE(bench.console.error);
}

TEST_CASE("fitsOnLine : un élément qui dépasse le bord droit va à la ligne")
{
    CHECK(levain::editor::fitsOnLine(100.0f, 8.0f, 50.0f, 200.0f));
    CHECK(levain::editor::fitsOnLine(100.0f, 8.0f, 92.0f, 200.0f)); // pile au bord : il tient
    CHECK_FALSE(levain::editor::fitsOnLine(100.0f, 8.0f, 93.0f, 200.0f));
    CHECK_FALSE(levain::editor::fitsOnLine(100.0f, 8.0f, 50.0f, 150.0f));
}

TEST_CASE("la console suit le bout de la liste tant qu'on y est, et laisse en paix qui est remonté")
{
    using levain::core::LogLevel;
    ConsoleBench bench;
    const auto pushMany = [&](int count)
    {
        for (int i = 0; i < count; ++i)
        {
            push(bench.ring, LogLevel::Info, "message");
        }
    };
    pushMany(60);
    bench.settle();
    REQUIRE(bench.console.scrollMax > 0.0f); // la liste déborde de la fenêtre
    CHECK(levain::editor::shouldFollowTail(bench.console.scrollY, bench.console.scrollMax));

    // Un message arrive pendant qu'on est au bout : la liste s'allonge et le défilement la suit.
    const float before = bench.console.scrollMax;
    pushMany(10);
    bench.settle();
    CHECK(bench.console.scrollMax > before);
    CHECK(levain::editor::shouldFollowTail(bench.console.scrollY, bench.console.scrollMax));

    // Remontée à la molette : un message de plus ne la ramène pas en bas.
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(bench.console.layout.list.x, bench.console.layout.list.y);
    bench.settle();
    io.AddMouseWheelEvent(0.0f, 10.0f);
    bench.settle();
    REQUIRE_FALSE(levain::editor::shouldFollowTail(bench.console.scrollY, bench.console.scrollMax));
    const float held = bench.console.scrollY;
    pushMany(10);
    bench.settle();
    CHECK(bench.console.scrollY == held);
    CHECK_FALSE(levain::editor::shouldFollowTail(bench.console.scrollY, bench.console.scrollMax));

    // Redescendue tout en bas, elle suit de nouveau.
    io.AddMouseWheelEvent(0.0f, -100.0f);
    bench.settle();
    pushMany(10);
    bench.settle();
    CHECK(levain::editor::shouldFollowTail(bench.console.scrollY, bench.console.scrollMax));
}

TEST_CASE("la Vue : la taille demandée est l'entier du contenu, vide sous un pixel")
{
    using levain::editor::sceneSizeOfContent;
    CHECK(sceneSizeOfContent(640.7f, 360.2f) == levain::app::SceneSize{640, 360});
    CHECK(sceneSizeOfContent(1.0f, 1.0f) == levain::app::SceneSize{1, 1});
    // Un côté sous un pixel : 0 x 0, jamais une texture de 0 x 1.
    CHECK(sceneSizeOfContent(0.6f, 300.0f).empty());
    CHECK(sceneSizeOfContent(300.0f, 0.0f) == levain::app::SceneSize{});
    CHECK(sceneSizeOfContent(-5.0f, 10.0f) == levain::app::SceneSize{});
}

TEST_CASE("la scène a le focus : la Vue panneaux ouverts, aucune fenêtre panneaux fermés")
{
    using levain::editor::sceneFocusedIn;
    CHECK(sceneFocusedIn(true, true, true, false));
    CHECK_FALSE(sceneFocusedIn(true, false, false, false)); // une autre fenêtre ne le donne pas
    CHECK_FALSE(sceneFocusedIn(true, true, true, true));    // la souris sur la barre de mode
    CHECK(sceneFocusedIn(false, false, false, false));
    CHECK_FALSE(sceneFocusedIn(false, true, true, false));
}

TEST_CASE("la modale « Quitter ? » : une demande, une ouverture, jamais empilée")
{
    using levain::editor::askToQuit;
    using levain::editor::QuitState;
    using levain::editor::shouldOpenQuitModal;
    QuitState state;
    CHECK_FALSE(shouldOpenQuitModal(state)); // personne n'a fermé
    askToQuit(state);
    CHECK(shouldOpenQuitModal(state));
    state.asked = false; // ce que `drawQuitModal` fait en l'ouvrant
    state.open = true;
    CHECK_FALSE(shouldOpenQuitModal(state));
    // Une deuxième croix modale ouverte : ni une autre modale, ni une pour après la fermeture.
    askToQuit(state);
    CHECK_FALSE(shouldOpenQuitModal(state));
    state.open = false; // Annuler
    CHECK_FALSE(shouldOpenQuitModal(state));
    askToQuit(state); // une croix modale fermée : une nouvelle modale
    CHECK(shouldOpenQuitModal(state));
}

TEST_CASE("sous la modale, la route est celle de l'UI et les raccourcis globaux se taisent")
{
    using levain::app::InputRoute;
    levain::editor::QuitState state;
    // Fermée : la route du mode passe telle quelle, les raccourcis marchent.
    CHECK(levain::editor::inputRouteWhileQuit(state, InputRoute::Game) == InputRoute::Game);
    CHECK(levain::editor::inputRouteWhileQuit(state, InputRoute::Editor) == InputRoute::Editor);
    CHECK(levain::editor::globalChordsAllowed(state));
    // Ouverte : l'UI garde tout, quelle que soit la route du mode.
    state.open = true;
    CHECK(levain::editor::inputRouteWhileQuit(state, InputRoute::Game) == InputRoute::Ui);
    CHECK(levain::editor::inputRouteWhileQuit(state, InputRoute::Editor) == InputRoute::Ui);
    CHECK_FALSE(levain::editor::globalChordsAllowed(state));
}

TEST_CASE("croix reçue ou modale ouverte : aucun geste de la caméra")
{
    levain::editor::QuitState state;
    CHECK(levain::editor::cameraGesturesAllowed(state));
    levain::editor::askToQuit(state); // la croix, avant que `ui` n'ouvre la modale
    CHECK_FALSE(levain::editor::cameraGesturesAllowed(state));
    state.asked = false; // ce que `drawQuitModal` fait en l'ouvrant
    state.open = true;
    CHECK_FALSE(levain::editor::cameraGesturesAllowed(state));
    state.open = false; // Annuler
    CHECK(levain::editor::cameraGesturesAllowed(state));
}

namespace
{

/// La modale « Quitter ? » dans l'ImGui sans fenêtre, comme `ConsoleBench`.
struct QuitBench
{
    const levain::ui::UiContext context = levain::ui::createUiContext(1.0f);
    flecs::world world;
    levain::editor::QuitState state;
    levain::editor::QuitAnswer answer = levain::editor::QuitAnswer::Pending;
    levain::editor::QuitAnswer lastAnswer =
        levain::editor::QuitAnswer::Pending; ///< la dernière non « Pending »

    void frame()
    {
        uiFrame(world,
                [&]
                {
                    answer = levain::editor::drawQuitModal(state);
                    if (answer != levain::editor::QuitAnswer::Pending)
                    {
                        lastAnswer = answer;
                    }
                });
    }

    void settle(int frames = 4)
    {
        for (int i = 0; i < frames; ++i)
        {
            frame();
        }
    }

    static ImVec2 centerOf(const levain::app::ScreenRect& rect)
    {
        return {rect.x + rect.width / 2.0f, rect.y + rect.height / 2.0f};
    }
};

} // namespace

TEST_CASE("la modale « Quitter ? » dessinée : une ouverture, Quitter, Annuler et Échap répondent")
{
    using levain::editor::QuitAnswer;
    QuitBench bench;
    bench.settle();
    CHECK_FALSE(bench.state.open); // rien demandé : rien dessiné
    CHECK(bench.state.quitButton.width == 0.0f);

    // Une croix : la modale s'ouvre, dessine Quitter puis Annuler, et attend.
    levain::editor::askToQuit(bench.state);
    bench.settle();
    REQUIRE(bench.state.open);
    CHECK(bench.state.timesOpened == 1);
    CHECK(bench.lastAnswer == QuitAnswer::Pending);
    REQUIRE(bench.state.quitButton.width > 0.0f);
    CHECK(bench.state.quitButton.x < bench.state.cancelButton.x);
    levain::editor::askToQuit(bench.state); // une deuxième croix : pas une deuxième ouverture
    bench.settle();
    CHECK(bench.state.timesOpened == 1);

    // Annuler au clic.
    clickAt(QuitBench::centerOf(bench.state.cancelButton), [&] { bench.frame(); });
    CHECK(bench.lastAnswer == QuitAnswer::Cancel);
    CHECK_FALSE(bench.state.open);

    // Échap annule aussi.
    levain::editor::askToQuit(bench.state);
    bench.settle();
    REQUIRE(bench.state.open);
    bench.lastAnswer = QuitAnswer::Pending;
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
    bench.settle(2);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
    bench.settle(2);
    CHECK(bench.lastAnswer == QuitAnswer::Cancel);
    CHECK_FALSE(bench.state.open);

    // Quitter au clic.
    levain::editor::askToQuit(bench.state);
    bench.settle();
    REQUIRE(bench.state.open);
    bench.lastAnswer = QuitAnswer::Pending;
    clickAt(QuitBench::centerOf(bench.state.quitButton), [&] { bench.frame(); });
    CHECK(bench.lastAnswer == QuitAnswer::Quit);
    CHECK_FALSE(bench.state.open);
}
