#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <flecs.h>

/// La réflexion des composants (ADR-0034) : une ligne dans le module qui possède le composant lit
/// ses champs dans la struct et les écrit dans celle de flecs (addon meta), que lisent l'explorer,
/// le JSON, l'inspecteur et la sauvegarde.
namespace levain::scene
{

namespace detail
{

/// Le nombre de branches de `field_ladder.inc` (MAX_FIELDS dans son script) ; le plus gros
/// composant connu a 17 champs.
inline constexpr std::size_t MaxFields = 32;

/// Se convertit en n'importe quel type, dans un contexte non évalué seulement : sert à compter.
struct AnyField
{
    template <class U> operator U() const;
};

template <class T, std::size_t... I> consteval bool bracesTake(std::index_sequence<I...> /*fields*/)
{
    return requires { T{(void(I), AnyField{})...}; };
}

/// Le nombre de champs : le plus grand N tel que `T{x1, …, xN}` compile. Un tableau C le fausse
/// (`float w[4]` compte pour 4) : les liaisons de `tieFields` refusent alors de compiler.
template <class T, std::size_t... N>
consteval std::size_t largestBraceCount(std::index_sequence<N...> /*counts*/)
{
    std::size_t largest = 0;
    ((largest = bracesTake<T>(std::make_index_sequence<N>{}) ? N : largest), ...);
    return largest;
}

template <class T>
inline constexpr std::size_t FieldCount =
    largestBraceCount<T>(std::make_index_sequence<MaxFields + 2>{});

/// Les champs d'un objet, en tuple de références : une branche par nombre de champs, écrite par
/// tools/generate_field_ladder.py.
template <class T> constexpr auto tieFields(T& object)
{
    constexpr std::size_t N = FieldCount<std::remove_cv_t<T>>;
    static_assert(N <= MaxFields, "réflexion : plus de 32 champs, couper le composant (ADR-0034)");
#include "levain/scene/detail/field_ladder.inc"
}

template <class T, std::size_t I>
using FieldType =
    std::remove_cvref_t<std::tuple_element_t<I, decltype(tieFields(std::declval<T&>()))>>;

template <class T> union Unconstructed
{
    char none;
    T value;

    constexpr Unconstructed() : none{} {}

    constexpr ~Unconstructed() {}
};

/// FakeObject : l'objet dont `fieldName` prend l'adresse des champs, à la compilation. Une union
/// jamais construite, pas l'objet `extern` jamais défini de Boost.PFR, qui déclenche
/// `-Wundefined-var-template` et que PFR tait par un pragma (règle n°4). L'union tient aussi pour
/// un composant qui n'est pas un type littéral (un `flecs::entity`).
template <class T> inline constexpr Unconstructed<T> FakeObject{};

template <auto Pointer> consteval std::string_view prettyFunction()
{
    return __PRETTY_FUNCTION__;
}

/// isIdentifier : ce que `fieldName` a découpé est-il un nom C++ ? Un autre format de
/// `__PRETTY_FUNCTION__` (MSVC) échoue ainsi à la compilation, jamais en silence.
consteval bool isIdentifier(std::string_view name)
{
    constexpr std::string_view Letters = "_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    constexpr std::string_view Characters =
        "_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    return !name.empty() && Letters.contains(name.front()) &&
           name.find_first_not_of(Characters) == std::string_view::npos;
}

/// fieldName : le nom du champ I, lu dans le `__PRETTY_FUNCTION__` d'une fonction instanciée sur
/// son adresse (la technique de Boost.PFR, et celle de flecs pour les enums). clang écrit
/// « [Pointer = &FakeObject<T>.value.speed] », GCC « [with auto Pointer = (&FakeObject<T>.…::value.
/// T::speed); …] » : de « Pointer = » au premier « ; » ou « ] », sans les parenthèses fermantes,
/// après le dernier « . » ou « : ».
template <class T, std::size_t I> consteval std::string_view fieldName()
{
    constexpr std::string_view Raw = prettyFunction<&std::get<I>(tieFields(FakeObject<T>.value))>();
    constexpr std::size_t From = Raw.find("Pointer = ");
    static_assert(From != std::string_view::npos, "réflexion : __PRETTY_FUNCTION__ inconnu");
    constexpr std::size_t Stop = Raw.find_first_of(";]", From);
    constexpr std::size_t End = Raw.find_last_not_of(')', Stop - 1) + 1;
    constexpr std::size_t Begin = Raw.find_last_of(".:", End - 1) + 1;
    constexpr std::string_view Name = Raw.substr(Begin, End - Begin);
    static_assert(isIdentifier(Name),
                  "réflexion : format de __PRETTY_FUNCTION__ inconnu (ADR-0034)");
    return Name;
}

/// offsetInProbe : le décalage d'un champ, par différence d'adresses dans un vrai `T`. Ni
/// `offsetof`, qui veut un nom, ni la surcharge de flecs par pointeur de membre, qui part d'un
/// pointeur nul : formellement un comportement indéfini.
template <class T, class F> std::size_t offsetInProbe(const T& probe, const F& field)
{
    return static_cast<std::size_t>(reinterpret_cast<const std::byte*>(&field) -
                                    reinterpret_cast<const std::byte*>(&probe));
}

/// Ajoute un champ à la description de `owner` ; un type sans réflexion arrête l'import
/// (`hasReflection` et `refuseFieldWithoutReflection`, dans reflection.cpp).
void addField(flecs::world& world, flecs::entity_t owner, std::string_view name,
              flecs::entity_t type, std::size_t offset);

template <class T> flecs::entity_t describeFields(flecs::world& world);

template <class F> inline constexpr bool IsStdArray = false;
template <class E, std::size_t N> inline constexpr bool IsStdArray<std::array<E, N>> = true;

/// Le type flecs d'un champ. Un agrégat se décrit à son tour, ce qui n'est pas une déclaration :
/// ni `Authored`, ni bornes. Nombres, booléens, enums (flecs les lit seul), `flecs::entity` et
/// feuilles glm décrites à la main ont leur réflexion ; le reste n'en a pas, et `addField` le
/// refuse. Un pointeur s'arrête sur le `static_assert` de flecs.
template <class F> flecs::entity_t fieldTypeOf(flecs::world& world)
{
    // IsStdArray : les liaisons l'ouvrent, mais `fieldName` y lirait « _M_elems[2] ».
    static_assert(!IsStdArray<F>, "réflexion : un std::array se décrit à la main, en tableau flecs "
                                  "(array<float>(N)), ou devient une feuille glm (ADR-0034)");
    if constexpr (std::is_class_v<F> && std::is_aggregate_v<F>)
    {
        return describeFields<F>(world);
    }
    else
    {
        return world.component<F>().id();
    }
}

template <class T> flecs::entity_t describeFields(flecs::world& world)
{
    static_assert(std::is_aggregate_v<T>,
                  "réflexion : pas un agrégat (un constructeur, un membre privé) : décrire ses "
                  "champs à la main, comme les feuilles glm de SceneModule (ADR-0034)");
    // Un membre référence fait échouer toute accolade : zéro champ lu, et rien d'écrit en silence.
    static_assert(FieldCount<T> > 0 || std::is_empty_v<T>,
                  "réflexion : aucun champ lu dans une struct qui en a (un membre référence ?) : "
                  "décrire ses champs à la main (ADR-0034)");
    const flecs::entity_t component = world.component<T>().id();
    // Décrire deux fois : la première description écrit les membres, les suivantes n'y touchent
    // pas. Une seconde description de flecs réinterpréterait les bits en silence.
    if constexpr (FieldCount<T> > 0)
    {
        if (!flecs::entity(world, component).has<flecs::Struct>())
        {
            const T probe{};
            [&]<std::size_t... I>(std::index_sequence<I...>)
            {
                (addField(world, component, fieldName<T, I>(), fieldTypeOf<FieldType<T, I>>(world),
                          offsetInProbe(probe, std::get<I>(tieFields(probe)))),
                 ...);
            }(std::make_index_sequence<FieldCount<T>>{});
        }
    }
    return component;
}

} // namespace detail

/// Décrit les champs de `T` dans flecs : l'explorer et le JSON les lisent.
template <class T> void describe(flecs::world& world)
{
    detail::describeFields<T>(world);
}

/// stableKeyOf : la clé d'un composant dans une sauvegarde, son symbole (le nom C++,
/// `levain.scene.Transform`), quel que soit le module qui l'a enregistré en premier. Pas son
/// chemin, qui en dépend (`levain.scene.SceneModule.Transform`). Vide pour une entité sans symbole.
[[nodiscard]] std::string_view stableKeyOf(const flecs::world& world, flecs::entity_t component);

} // namespace levain::scene
