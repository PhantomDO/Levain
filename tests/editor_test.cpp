#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>
#include <flecs.h>

#include "levain/editor/editor.hpp"
#include "levain/editor/hierarchy.hpp"
#include "levain/scene/components.hpp"

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

TEST_CASE("la hiérarchie tait les entités de flecs")
{
    flecs::world world;
    world.entity("placée").set(Transform{});
    // Un singleton est rangé sur l'entité de son composant : sans `isEngineInternal`, le
    // composant Transform passerait pour une racine de la scène, comme un système placé.
    world.set(Transform{});
    world.system("système").run([](flecs::iter&) {}).set(Transform{});
    const levain::editor::Hierarchy hierarchy = levain::editor::createHierarchy(world);

    CHECK(rowsOf(world, hierarchy) == std::vector<std::string>{"placée"});
}
