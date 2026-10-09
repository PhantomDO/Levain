// Les refus de la réflexion à la compilation (ADR-0034), un cas par cible : chaque `static_assert`
// de reflection.hpp doit arrêter la struct fautive en disant pourquoi. CTest construit la cible du
// cas (tests/CMakeLists.txt, scene.reflection-refuses-compile.*) et lit la sortie du compilateur.
//
// Sans cas défini, le fichier compile : c'est la cible `levain_reflection_compile_control`, que
// clang-tidy lit (celles des cas échouent exprès, hors de compile_commands.json) et qui rougit si
// le préambule casse.
//
// Sans cas, et pourquoi :
// - `From != npos` de `fieldName` : il faudrait un `__PRETTY_FUNCTION__` sans « Pointer = », celui
//   d'un autre compilateur que clang, qu'aucune struct ne provoque ;
// - une classe de base, un tableau C, une faute de frappe de `range` : pas de `static_assert` à
//   nous, l'erreur est celle de clang, dont le texte change d'une version à l'autre.
// `isIdentifier` a ses deux contrôles : le câblage ci-dessous, et le prédicat dans
// reflection_test.cpp.

#include <array>
#include <string>

#include <flecs.h>

#include "levain/scene/reflection.hpp"

namespace
{

#if defined(LEVAIN_REFUSAL_TOO_MANY_FIELDS)
struct TooManyFields // 33 champs, un de plus que la dernière branche de l'échelle de liaisons
{
    float f00, f01, f02, f03, f04, f05, f06, f07, f08, f09, f10, f11, f12, f13, f14, f15, f16, f17,
        f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28, f29, f30, f31, f32;
};

[[maybe_unused]] void refuse(flecs::world& world)
{
    levain::scene::describe<TooManyFields>(world);
}
#endif

// Deux refus pour une même struct : `IsStdArray` l'arrête avant, `isIdentifier` quand elle passe.
// Le second n'a pas d'autre déclencheur sous clang : libstdc++ nomme le membre « _M_elems[2] », que
// `fieldName` découpe en un nom qui n'en est pas un.
#if defined(LEVAIN_REFUSAL_STD_ARRAY) || defined(LEVAIN_REFUSAL_FIELD_NAME_FORMAT)
struct WithArray // les liaisons l'ouvriraient, mais `fieldName` y lirait « _M_elems[2] »
{
    std::array<float, 2> values;
};

[[maybe_unused]] void refuse(flecs::world& world)
{
    levain::scene::describe<WithArray>(world);
}
#endif

#if defined(LEVAIN_REFUSAL_NOT_AGGREGATE)
struct WithConstructor // un constructeur : plus un agrégat, les accolades ne lisent plus les champs
{
    WithConstructor() = default;

    float speed = 1.0f;
};

[[maybe_unused]] void refuse(flecs::world& world)
{
    levain::scene::describe<WithConstructor>(world);
}
#endif

#if defined(LEVAIN_REFUSAL_REFERENCE_MEMBER)
struct WithReference // une référence fait échouer toute accolade : zéro champ lu
{
    float& target;
};

[[maybe_unused]] void refuse(flecs::world& world)
{
    levain::scene::describe<WithReference>(world);
}
#endif

// La borne a deux moitiés, chacune son cas : un booléen est arithmétique pour C++ (la seconde
// moitié le refuse), une enum ou une entité ne l'est pas (la première).
#if defined(LEVAIN_REFUSAL_RANGE_ON_BOOL)
struct WithFlag // un booléen est arithmétique pour C++, mais l'inspecteur ne le borne pas
{
    bool enabled = false;
};

[[maybe_unused]] void refuse(flecs::world& world)
{
    levain::scene::describe<WithFlag>(world).range(&WithFlag::enabled, 0.0, 1.0);
}
#endif

#if defined(LEVAIN_REFUSAL_RANGE_ON_ENUM)
enum class Gait
{
    Walk,
    Run
};

struct WithGait // une enum se lit et s'édite, mais l'inspecteur ne la borne pas
{
    Gait gait = Gait::Walk;
};

[[maybe_unused]] void refuse(flecs::world& world)
{
    levain::scene::describe<WithGait>(world).range(&WithGait::gait, 0.0, 1.0);
}
#endif

#if defined(LEVAIN_REFUSAL_AUTHORED_NOT_TRIVIALLY_COPYABLE)
struct WithString // un agrégat, mais l'annulation et Play/Stop le gardent en octets
{
    std::string name;
};

[[maybe_unused]] void refuse(flecs::world& world)
{
    levain::scene::describeAuthored<WithString>(world);
}
#endif

#if defined(LEVAIN_REFUSAL_AUTHORED_NOT_COPY_ASSIGNABLE)
struct WithConst // copiable octet par octet, mais `set` d'une copie entière ne peut pas l'écrire
{
    const float limit = 1.0f;
};

[[maybe_unused]] void refuse(flecs::world& world)
{
    levain::scene::describeAuthored<WithConst>(world);
}
#endif

#if defined(LEVAIN_REFUSAL_POINTER_FIELD)
struct WithPointer // le `static_assert` est celui de flecs (component.hpp), pas le nôtre
{
    float* target = nullptr;
};

[[maybe_unused]] void refuse(flecs::world& world)
{
    levain::scene::describe<WithPointer>(world);
}
#endif

} // namespace
