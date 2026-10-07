#include <cstddef>
#include <string_view>

#include <doctest/doctest.h>

#include "levain/scene/components.hpp"
#include "levain/scene/reflection.hpp"

namespace reflection_test
{

struct Capsule
{
    float halfHeight = 0.5f;
    float radius = 0.25f;
};

/// Des champs de types variés, du remplissage entre eux, et un agrégat imbriqué.
struct Probe
{
    bool flag = false;
    glm::vec3 position{0.0f};
    Capsule shape{};
    float speed = 0.0f;
};

} // namespace reflection_test

using reflection_test::Capsule;
using reflection_test::Probe;

TEST_CASE("les champs d'un agrégat se lisent dans la struct : nombre, noms, décalages")
{
    using levain::scene::detail::FieldCount;
    using levain::scene::detail::fieldName;
    using levain::scene::detail::offsetInProbe;
    using levain::scene::detail::tieFields;

    static_assert(FieldCount<Probe> == 4); // l'agrégat imbriqué compte pour un champ
    static_assert(FieldCount<Capsule> == 2);
    CHECK(fieldName<Probe, 0>() == "flag");
    CHECK(fieldName<Probe, 1>() == "position");
    CHECK(fieldName<Probe, 2>() == "shape");
    CHECK(fieldName<Probe, 3>() == "speed");
    const Probe probe{};
    CHECK(offsetInProbe(probe, std::get<0>(tieFields(probe))) == offsetof(Probe, flag));
    CHECK(offsetInProbe(probe, std::get<1>(tieFields(probe))) == offsetof(Probe, position));
    CHECK(offsetInProbe(probe, std::get<2>(tieFields(probe))) == offsetof(Probe, shape));
    CHECK(offsetInProbe(probe, std::get<3>(tieFields(probe))) == offsetof(Probe, speed));
}
