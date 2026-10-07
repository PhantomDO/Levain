#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include <flecs.h>

/// La réflexion des composants (ADR-0034) : une ligne dans le module qui possède le composant lit
/// ses champs dans la struct et les écrit dans celle de flecs (addon meta), que lisent l'explorer,
/// le JSON, l'inspecteur et la sauvegarde.
///
///     scene::describeAuthored<RigidBody>(world).range(&RigidBody::mass, 0.001, 1.0e6);
namespace levain::scene
{

/// Posée sur l'entité d'un composant par `describeAuthored` : la donnée d'auteur, que l'inspecteur
/// édite, que la scène sauvegarde (M7.3) et que Play/Stop restaure (M7.5). Sans elle, le composant
/// est en lecture seule et n'est pas sauvegardé : un oubli se voit.
struct Authored
{
};

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

/// Pose la déclaration ; `describe` et `describeAuthored` sur un même type arrêtent l'import.
void declare(flecs::world& world, flecs::entity_t component, bool authored);

/// Pose la borne du champ à ce décalage ; vide, inversée, ou autre que celle d'une déclaration
/// précédente, elle arrête l'import.
void setFieldRange(flecs::world& world, flecs::entity_t component, std::size_t offset, double min,
                   double max);

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

/// Ce que rend une déclaration, pour y poser les bornes de ses champs.
template <class T> class Description
{
public:
    Description(flecs::world& world, flecs::entity_t component)
        : m_world(&world), m_component(component)
    {
    }

    /// Les bornes d'un champ nombre, que l'inspecteur impose : flecs ne borne rien. Le champ se
    /// désigne par pointeur de membre, une faute de frappe ne compile pas.
    template <class F> Description& range(F T::* field, double min, double max)
    {
        static_assert(std::is_arithmetic_v<F> && !std::is_same_v<std::remove_cv_t<F>, bool>,
                      "réflexion : une borne sur un nombre seulement (ADR-0034)");
        const T probe{};
        detail::setFieldRange(*m_world, m_component, detail::offsetInProbe(probe, probe.*field),
                              min, max);
        return *this;
    }

private:
    flecs::world* m_world;
    flecs::entity_t m_component;
};

/// Ce que le moteur ou le jeu réécrit (`WorldTransform`, une entrée reposée à chaque image) :
/// visible de l'éditeur en lecture seule, jamais sauvegardé.
template <class T> Description<T> describe(flecs::world& world)
{
    const flecs::entity_t component = detail::describeFields<T>(world);
    detail::declare(world, component, false);
    return {world, component};
}

/// La donnée d'auteur : éditée, sauvegardée, restaurée, et gardée en octets par l'annulation et
/// Play/Stop, d'où la copie octet par octet.
template <class T> Description<T> describeAuthored(flecs::world& world)
{
    static_assert(std::is_trivially_copyable_v<T>,
                  "réflexion : une donnée d'auteur se copie octet par octet (ADR-0034)");
    static_assert(std::is_copy_assignable_v<T>,
                  "réflexion : une donnée d'auteur s'écrit par set, ni membre const ni référence "
                  "(ADR-0034)");
    const flecs::entity_t component = detail::describeFields<T>(world);
    detail::declare(world, component, true);
    return {world, component};
}

/// setComponentValue : la seule écriture de l'éditeur (inspecteur, annulation, chargement, gizmos,
/// Stop). Un `set` d'une copie entière : `OnSet` part une fois, comme pour le code du jeu, hook
/// `on_replace` compris. Une écriture par référence, elle, ne déclenche rien : un corps déplacé
/// ainsi diverge en silence.
void setComponentValue(flecs::world& world, flecs::entity_t entity, flecs::entity_t component,
                       const void* value);

/// sameValue : deux valeurs d'un type décrit, comparées feuille par feuille en suivant `EcsStruct`,
/// jamais par `memcmp` de la struct : ses octets de remplissage diffèrent entre deux valeurs
/// égales. Deux étiquettes, sans valeur, sont égales. Une feuille se compare par ses octets (NaN
/// égale NaN, -0 diffère de 0) : une commande d'annulation n'est jetée que si rien n'a bougé.
[[nodiscard]] bool sameValue(const flecs::world& world, flecs::entity_t type, const void* first,
                             const void* second);

/// stableKeyOf : la clé d'un composant dans une sauvegarde, son symbole (le nom C++,
/// `levain.scene.Transform`), quel que soit le module qui l'a enregistré en premier. Pas son
/// chemin, qui en dépend (`levain.scene.SceneModule.Transform`). Vide pour une entité sans symbole.
[[nodiscard]] std::string_view stableKeyOf(const flecs::world& world, flecs::entity_t component);

/// componentOfKey : le composant d'une clé, relue comme symbole, jamais comme chemin ; 0 sinon.
[[nodiscard]] flecs::entity_t componentOfKey(const flecs::world& world, std::string_view key);

/// rangeOf : les bornes d'un champ, s'il en a. flecs écrit une borne absente `[0, 0]` ; une borne
/// posée a toujours min < max, l'import refuse les autres.
[[nodiscard]] std::optional<ecs_member_value_range_t> rangeOf(const ecs_member_t& member);

} // namespace levain::scene
