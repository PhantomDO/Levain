#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>
#include <flecs.h>
#include <imgui.h>

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
    const levain::editor::Inspector inspector = levain::editor::createInspector(world);
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

/// Les champs de `component` dans une fenêtre à une place connue ; `field` reçoit le centre du
/// dernier widget, le seul champ d'un composant à un champ.
std::function<void()> frameOf(flecs::world& world, levain::editor::Inspector& inspector,
                              flecs::entity entity, flecs::entity_t component, ImVec2& field)
{
    return [&world, &inspector, entity, component, &field]
    {
        uiFrame(world,
                [&]
                {
                    ImGui::SetNextWindowPos({0.0f, 0.0f});
                    ImGui::SetNextWindowSize({400.0f, 200.0f});
                    ImGui::Begin("champ");
                    levain::editor::inspectComponent(world, inspector, entity, component);
                    const ImVec2 low = ImGui::GetItemRectMin();
                    const ImVec2 high = ImGui::GetItemRectMax();
                    field = {low.x + 10.0f, (low.y + high.y) / 2.0f};
                    ImGui::End();
                });
    };
}

} // namespace

TEST_CASE("un nombre tapé dans le champ d'une donnée d'auteur est borné, et part en un seul OnSet")
{
    const levain::ui::UiContext context = levain::ui::createUiContext(1.0f);
    flecs::world world;
    describeTestComponents(world);
    levain::editor::Inspector inspector = levain::editor::createInspector(world);
    const flecs::entity entity = world.entity("cadran").set(editor_test::Dial{});
    int sets = 0;
    world.observer<editor_test::Dial>()
        .event(flecs::OnSet)
        .each([&](editor_test::Dial&) { ++sets; });
    ImVec2 field{};
    const auto frame = frameOf(world, inspector, entity, world.id<editor_test::Dial>(), field);

    for (int i = 0; i < 4; ++i)
    {
        frame(); // le champ se place ; au repos, rien n'est écrit
    }
    CHECK(sets == 0);
    typeInto(field, "0.25", frame);
    CHECK(entity.get<editor_test::Dial>().level == doctest::Approx(0.25f));
    CHECK(sets == 1);
    // 5 dépasse la borne [0, 1] : Ctrl+clic tape au-delà sans AlwaysClamp.
    typeInto(field, "5", frame);
    CHECK(entity.get<editor_test::Dial>().level == 1.0f);
    CHECK(sets == 2);
}

TEST_CASE("le champ d'une donnée en lecture seule ne se tape pas, et rien n'est écrit")
{
    const levain::ui::UiContext context = levain::ui::createUiContext(1.0f);
    flecs::world world;
    describeTestComponents(world);
    levain::editor::Inspector inspector = levain::editor::createInspector(world);
    const flecs::entity entity = world.entity("jauge").set(editor_test::Gauge{});
    int sets = 0;
    world.observer<editor_test::Gauge>()
        .event(flecs::OnSet)
        .each([&](editor_test::Gauge&) { ++sets; });
    ImVec2 field{};
    const auto frame = frameOf(world, inspector, entity, world.id<editor_test::Gauge>(), field);

    frame();
    typeInto(field, "0.9", frame);
    CHECK(entity.get<editor_test::Gauge>().level == 0.5f);
    CHECK(sets == 0);
}
