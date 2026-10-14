// Les lignes de debug coupées au volume de vue (M7.7, piece 17c, review) : de la logique pure, sans
// GPU. Une ligne qui traverse le plan proche, ou dont un bout est derrière l'œil, ne doit pas
// partir telle quelle : sa projection serait absurde (grille du sol, rayons du bac à sable).

#include <cmath>
#include <cstddef>
#include <optional>

#include <doctest/doctest.h>
#include <glm/glm.hpp>

#include "levain/render/camera.hpp"
#include "levain/render/culling.hpp"
#include "levain/render/debug_lines.hpp"

namespace
{

using levain::render::DebugLine;

/// L'indice du plan proche parmi les six du frustum, dans l'ordre de `frustumOf` : gauche, droite,
/// bas, haut, **proche** (le cinquième, indice 4), lointain.
constexpr std::size_t NearPlaneIndex = 4;

/// La caméra par défaut : en (0, 1,5, 3), vers −Z ; proche 0,1, lointain 100 ; 60° de champ.
levain::render::Frustum frustum()
{
    return levain::render::frustumOf(
        levain::render::viewProjectionOf(levain::render::Camera{}, 16.0f / 9.0f));
}

/// Le segment coupé, ou un segment nul : clang-tidy ne suit pas `REQUIRE(...has_value())`, l'accès
/// aux résultats se fait par une valeur.
DebugLine valueOf(const std::optional<DebugLine>& clipped)
{
    return clipped ? *clipped : DebugLine{};
}

DebugLine line(glm::vec3 from, glm::vec3 to)
{
    return {
        .from = from, .to = to, .color = {1.0f, 0.0f, 0.0f}, .alphaFrom = 0.0f, .alphaTo = 1.0f};
}

} // namespace

TEST_CASE("une ligne tout entière dans le volume de vue ressort inchangée")
{
    const DebugLine inside = line({-1.0f, 0.5f, 0.0f}, {1.0f, 0.5f, -5.0f});
    const auto clippedOptional = levain::render::clipToFrustum(inside, frustum());
    REQUIRE(clippedOptional.has_value());
    const DebugLine clipped = valueOf(clippedOptional);
    CHECK(clipped.from == inside.from);
    CHECK(clipped.to == inside.to);
    CHECK(clipped.alphaFrom == 0.0f);
    CHECK(clipped.alphaTo == 1.0f);
}

TEST_CASE("une ligne tout entière derrière l'œil, ou hors du champ, est écartée")
{
    // Derrière la caméra (z > 3), loin sur le côté, au-delà du plan lointain.
    CHECK_FALSE(levain::render::clipToFrustum(line({0, 1.5f, 5.0f}, {2, 1.5f, 40.0f}), frustum()));
    CHECK_FALSE(
        levain::render::clipToFrustum(line({50, 0, -5.0f}, {60, 0, -9.0f}), frustum()).has_value());
    CHECK_FALSE(levain::render::clipToFrustum(line({0, 0, -200.0f}, {1, 0, -300.0f}), frustum())
                    .has_value());
}

TEST_CASE("une ligne qui traverse le plan proche est coupée là, couleur et opacité interpolées")
{
    // Part de 10 m devant l'œil (z = −7) pour finir 10 m derrière lui (z = 13) : le plan proche est
    // à 0,1 m devant l'œil, soit z = 2,9 environ.
    const DebugLine crossing = line({0.0f, 1.5f, -7.0f}, {0.0f, 1.5f, 13.0f});
    const auto clippedOptional = levain::render::clipToFrustum(crossing, frustum());
    REQUIRE(clippedOptional.has_value());
    const DebugLine clipped = valueOf(clippedOptional);
    CHECK(clipped.from.z == doctest::Approx(-7.0f));
    // La caméra penche un peu vers le bas : le plan proche n'est pas exactement à z = 2,9. Le bout
    // coupé est **sur** le plan proche, pas au-delà.
    const glm::vec4 nearPlane = frustum().planes[NearPlaneIndex];
    CHECK(glm::dot(glm::vec3{nearPlane}, clipped.to) + nearPlane.w ==
          doctest::Approx(0.0f).scale(1.0));
    CHECK(clipped.to.z > 2.5f);
    CHECK(clipped.to.z < 3.0f);
    CHECK(clipped.to.y == doctest::Approx(1.5f));
    // L'opacité a suivi le même chemin : la part du trajet parcourue, de −7 à 13.
    CHECK(clipped.alphaTo == doctest::Approx((clipped.to.z + 7.0f) / 20.0f));
    CHECK(clipped.color == crossing.color);
    // Et l'autre sens : le bout derrière l'œil est le premier.
    const auto reversedOptional =
        levain::render::clipToFrustum(line(crossing.to, crossing.from), frustum());
    REQUIRE(reversedOptional.has_value());
    const DebugLine reversed = valueOf(reversedOptional);
    CHECK(reversed.from.z == doctest::Approx(clipped.to.z));
    CHECK(reversed.to.z == doctest::Approx(-7.0f));
}

TEST_CASE("une ligne qui traverse deux plans est coupée aux deux bouts")
{
    // De loin à gauche à loin à droite, à 10 m devant l'œil : coupée par le plan gauche puis le
    // droit.
    const auto clippedOptional = levain::render::clipToFrustum(
        line({-100.0f, 1.5f, -7.0f}, {100.0f, 1.5f, -7.0f}), frustum());
    REQUIRE(clippedOptional.has_value());
    const DebugLine clipped = valueOf(clippedOptional);
    CHECK(clipped.from.x < -3.0f);
    CHECK(clipped.from.x > -20.0f);
    CHECK(clipped.to.x == doctest::Approx(-clipped.from.x));
    CHECK(clipped.alphaFrom > 0.0f);
    CHECK(clipped.alphaTo < 1.0f);
    CHECK(clipped.alphaFrom < clipped.alphaTo);
    // Le résultat est dans le volume : plus aucun bout n'est de l'autre côté d'un plan.
    for (const glm::vec3& end : {clipped.from, clipped.to})
    {
        for (const glm::vec4& plane : frustum().planes)
        {
            CHECK(glm::dot(glm::vec3{plane}, end) + plane.w > -1.0e-3f);
        }
    }
}

TEST_CASE("une droite qui frôle le volume sans y entrer est écartée")
{
    // Passe au-dessus du coin haut gauche : les deux bouts sont dehors, d'un plan différent.
    CHECK_FALSE(
        levain::render::clipToFrustum(line({-30.0f, 20.0f, -5.0f}, {5.0f, 40.0f, -5.0f}), frustum())
            .has_value());
}
