#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
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
#include "levain/editor/editor.hpp"
#include "levain/editor/hierarchy.hpp"
#include "levain/editor/inspector.hpp"
#include "levain/scene/components.hpp"
#include "levain/scene/reflection.hpp"
#include "levain/scene/scene.hpp"
#include "levain/ui/context.hpp"

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

TEST_CASE("la hiérarchie tait les entités de flecs, et un singleton va dans « Singletons »")
{
    flecs::world world;
    // flecs garde l'instance d'un module sur l'entité du module, et `Component` se décrit
    // lui-même : deux composants qui portent leur valeur, sans être des singletons de la scène.
    const flecs::entity scene = world.import<levain::scene::SceneModule>();
    world.entity("placée").set(Transform{});
    // Un singleton est rangé sur l'entité de son composant : sans `isEngineInternal`, le
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

    uiFrame(world, [&] { levain::editor::drawInspector(world, inspector, entity, 0); });
    // Tuning : gain, count, enabled, mode, inner.a, inner.b, offset. WorldTransform : les quatre
    // colonnes de sa matrice. Weights : [0-3] et [4-5]. Le composant non décrit, l'étiquette et le
    // nom : une ligne chacun, aucun champ.
    CHECK(inspector.fieldsDrawn == 7 + 4 + 2);

    // Sans sélection, ou une entité détruite : aucun champ.
    entity.destruct();
    uiFrame(world, [&] { levain::editor::drawInspector(world, inspector, entity, 0); });
    CHECK(inspector.fieldsDrawn == 0);
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
    typing.type<editor_test::Dial>(entity, "0.25");
    CHECK(level() == doctest::Approx(0.25f));
    CHECK(typing.sets == 1);
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
