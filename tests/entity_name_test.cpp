#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include <doctest/doctest.h>
#include <flecs.h>

#include "levain/scene/entity_name.hpp"

// Les noms d'entités (ADR-0038, décision 6, morceau A1) : chaque refus nommé, chaque nom piégé
// confronté à `lookup` de flecs 4.1.6, puis l'unicité entre frères.

using levain::scene::isValidEntityName;
using levain::scene::NameRefusal;
using levain::scene::reasonOf;
using levain::scene::refusalOfName;
using levain::scene::uniqueNameAmong;

namespace
{

flecs::entity child(const flecs::world& world, flecs::entity parent, const char* name)
{
    return world.entity().set(flecs::Parent{parent}).set_name(name);
}

/// Un nom se retrouve : `lookup` à la racine, ou sous un parent, rend bien l'entité qui le porte.
bool findsItself(const flecs::world& world, std::string_view name, bool atRoot)
{
    const std::string text{name};
    if (atRoot)
    {
        const flecs::entity e = world.entity();
        e.set_name(text.c_str());
        return world.lookup(text.c_str()).id() == e.id();
    }
    const flecs::entity parent = world.entity("P");
    const flecs::entity e = child(world, parent, text.c_str());
    const bool found = world.lookup(("P::" + text).c_str()).id() == e.id();
    parent.destruct();
    return found;
}

} // namespace

TEST_CASE("chaque refus de nom a sa cause")
{
    CHECK(refusalOfName("", true) == NameRefusal::Empty);
    CHECK(refusalOfName("", false) == NameRefusal::Empty);
    CHECK(refusalOfName("a.b", true) == NameRefusal::ContainsDot);
    CHECK(refusalOfName(".", false) == NameRefusal::ContainsDot);
    CHECK(refusalOfName("a::b", true) == NameRefusal::ContainsScope);
    CHECK(refusalOfName("::", false) == NameRefusal::ContainsScope);
    CHECK(refusalOfName("#12", true) == NameRefusal::IdentifierSyntax);
    CHECK(refusalOfName("#0", false) == NameRefusal::IdentifierSyntax);
    CHECK(refusalOfName("#abc", true) == NameRefusal::HashAtRoot);
    CHECK(refusalOfName("#", true) == NameRefusal::HashAtRoot);
    CHECK(refusalOfName("_", true) == NameRefusal::WildcardAtRoot);
    CHECK(refusalOfName("*", true) == NameRefusal::WildcardAtRoot);
    CHECK(refusalOfName("$", true) == NameRefusal::WildcardAtRoot);
}

TEST_CASE("les noms permis restent permis, espaces, chiffres et accents compris")
{
    for (const std::string_view name : {"Cube", "Cube 2", "2", "12abc", "a b", "é", "-", "this",
                                        "$x", "a/b", "(a)", "a,b", "a#1", "_x", "**", "a:b"})
    {
        CAPTURE(name);
        CHECK(isValidEntityName(name, true));
        CHECK(isValidEntityName(name, false));
    }
    // « #abc » n'est refusé qu'à la racine ; « #12 » l'est partout.
    CHECK_FALSE(isValidEntityName("#abc", true));
    CHECK(isValidEntityName("#abc", false));
    CHECK_FALSE(isValidEntityName("#12", false));
    // Sous un parent, « _ », « * » et « $ » se retrouvent par lookup : ils ne sont refusés qu'à la
    // racine.
    for (const std::string_view name : {"_", "*", "$"})
    {
        CAPTURE(name);
        CHECK_FALSE(isValidEntityName(name, true));
        CHECK(isValidEntityName(name, false));
    }
}

TEST_CASE("chaque refus a une phrase que le champ peut montrer")
{
    for (const NameRefusal refusal :
         {NameRefusal::Empty, NameRefusal::ContainsDot, NameRefusal::ContainsScope,
          NameRefusal::IdentifierSyntax, NameRefusal::HashAtRoot, NameRefusal::WildcardAtRoot})
    {
        CHECK_FALSE(reasonOf(refusal).empty());
    }
    CHECK(reasonOf(NameRefusal::ContainsDot).find('.') != std::string_view::npos);
}

TEST_CASE("tout nom permis se retrouve par lookup, tout nom piégé refusé ne se retrouve pas")
{
    flecs::world world;
    // Le contrôle de la décision 6 : le refus n'est pas une règle d'humeur, il suit flecs. Un
    // nom que flecs sait retrouver est permis, un nom qu'il confond avec autre chose est refusé.
    for (const std::string_view name :
         {"Cube", "Cube 2", "2", "12abc", "a b", "é", "-", "this", "$x", "a#1", "_x"})
    {
        CAPTURE(name);
        REQUIRE(isValidEntityName(name, true));
        CHECK(findsItself(world, name, true));
        CHECK(findsItself(world, name, false));
    }
    for (const std::string_view name : {"#12", "#abc", "#", "_", "*", "$"})
    {
        CAPTURE(name);
        REQUIRE_FALSE(isValidEntityName(name, true));
        CHECK_FALSE(findsItself(world, name, true)); // à la racine, lookup rend autre chose
    }
    // « #12 » est faux aussi sous un parent ; « #abc », « _ », « * » et « $ » y sont retrouvés.
    CHECK_FALSE(findsItself(world, "#12", false));
    CHECK(findsItself(world, "#abc", false));
    for (const std::string_view name : {"_", "*", "$"})
    {
        CAPTURE(name);
        CHECK(findsItself(world, name, false));
    }
}

TEST_CASE("un « . » éclate le chemin au rechargement, un « :: » se lit comme un chemin")
{
    flecs::world world;
    // Le chemin « P.a.b » (séparateur « . ») d'un nom « a.b » est lu comme P, puis a, puis b.
    const flecs::entity parent = world.entity("P");
    const flecs::entity dotted = child(world, parent, "a.b");
    CHECK(ecs_lookup_path_w_sep(world, 0, "P.a.b", ".", nullptr, true) != dotted.id());
    // Un nom « x::y » existe bien (set_name ne lit pas de chemin), mais lookup le lit comme x puis
    // y.
    const flecs::entity scoped = world.entity();
    scoped.set_name("x::y");
    CHECK(world.lookup("x::y").id() != scoped.id());
}

TEST_CASE("uniqueNameAmong rend le nom voulu quand il est libre")
{
    flecs::world world;
    world.entity("Autre");
    const auto name = uniqueNameAmong(world, 0, "Entité");
    REQUIRE(name.has_value());
    CHECK(*name == "Entité");
}

TEST_CASE("uniqueNameAmong numérote à partir de 2 et prend le plus petit numéro libre")
{
    flecs::world world;
    world.entity("Entité");
    CHECK(*uniqueNameAmong(world, 0, "Entité") == "Entité 2");

    world.entity("Entité 2");
    CHECK(*uniqueNameAmong(world, 0, "Entité") == "Entité 3");

    // Un trou : « Entité 3 » est pris, « Entité 2 » ne l'est plus.
    flecs::world gap;
    gap.entity("Entité");
    gap.entity("Entité 3");
    CHECK(*uniqueNameAmong(gap, 0, "Entité") == "Entité 2");
}

TEST_CASE("uniqueNameAmong remplace le numéro final au lieu de l'empiler")
{
    flecs::world world;
    world.entity("Cube");
    world.entity("Cube 2");
    CHECK(*uniqueNameAmong(world, 0, "Cube 2") == "Cube 3");

    // Après son numéro, pas depuis 2 : « Lampe 10 » pris donne « Lampe 11 ».
    world.entity("Lampe 10");
    CHECK(*uniqueNameAmong(world, 0, "Lampe 10") == "Lampe 11");

    // Un nom qui n'est qu'un nombre garde son corps : jamais de nom vide.
    flecs::world numbers;
    numbers.entity("2");
    CHECK(*uniqueNameAmong(numbers, 0, "2") == "2 2");
}

TEST_CASE("uniqueNameAmong ne compare qu'entre frères, racines comprises")
{
    flecs::world world;
    const flecs::entity left = world.entity("gauche");
    const flecs::entity right = world.entity("droite");
    child(world, left, "Cube");
    child(world, right, "Cube");
    world.entity("Cube"); // une racine

    // Sous « gauche » : « Cube » est pris par un frère, « Cube 2 » est libre.
    CHECK(*uniqueNameAmong(world, left.id(), "Cube") == "Cube 2");
    // Sous « gauche », « droite » est libre : c'est une racine, pas un frère.
    CHECK(*uniqueNameAmong(world, left.id(), "droite") == "droite");
    // Parmi les racines, « gauche » est pris (et pas par l'enfant « Cube » d'un autre).
    CHECK(*uniqueNameAmong(world, 0, "gauche") == "gauche 2");
    CHECK(*uniqueNameAmong(world, 0, "Cube") == "Cube 2");
}

TEST_CASE("uniqueNameAmong ne compte pas l'entité qu'on renomme")
{
    flecs::world world;
    const flecs::entity cube = world.entity("Cube");
    CHECK(*uniqueNameAmong(world, 0, "Cube", cube.id()) == "Cube");
    CHECK(*uniqueNameAmong(world, 0, "Cube") == "Cube 2");
}

TEST_CASE("uniqueNameAmong refuse un nom invalide en disant pourquoi")
{
    flecs::world world;
    const flecs::entity parent = world.entity("P");

    const auto dotted = uniqueNameAmong(world, 0, "a.b");
    REQUIRE_FALSE(dotted.has_value());
    CHECK(dotted.error().message.find("a.b") != std::string::npos);
    CHECK(dotted.error().message.find(reasonOf(NameRefusal::ContainsDot)) != std::string::npos);

    CHECK_FALSE(uniqueNameAmong(world, 0, "").has_value());
    CHECK_FALSE(uniqueNameAmong(world, 0, "#12").has_value());
    CHECK_FALSE(uniqueNameAmong(world, 0, "*").has_value());
    CHECK(uniqueNameAmong(world, parent.id(), "*").has_value()); // sous un parent, « * » est permis
}
