#include <array>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>
#include <flecs.h>

#include "levain/editor/editor.hpp"

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
