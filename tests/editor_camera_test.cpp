// La caméra de l'éditeur (ADR-0036, décision 6, morceau 9) : de la logique pure, sans fenêtre ni
// monde. Chaque propriété a son cas, et chacun rougit quand sa règle disparaît (les contre-tests
// sont listés dans la PR du morceau).

#include <cmath>
#include <limits>

#include <doctest/doctest.h>
#include <glm/glm.hpp>

#include "levain/editor/camera.hpp"
#include "levain/render/camera.hpp"

using levain::editor::clampEditorPitch;
using levain::editor::clampFlySpeed;
using levain::editor::EditorCamera;
using levain::editor::pivotOf;
using levain::editor::rightOf;
using levain::editor::scaleFlySpeed;
using levain::editor::upOf;
using levain::editor::viewDirectionOf;
using levain::editor::wrapYawDegrees;

namespace
{

constexpr float NaN = std::numeric_limits<float>::quiet_NaN();
constexpr float Infinity = std::numeric_limits<float>::infinity();

bool isFinite(const glm::vec3& vector)
{
    return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
}

} // namespace

TEST_CASE("caméra de l'éditeur : le tangage est borné à ±89° et ne devient jamais NaN")
{
    CHECK(clampEditorPitch(30.0f) == doctest::Approx(30.0f));
    CHECK(clampEditorPitch(90.0f) == doctest::Approx(89.0f));
    CHECK(clampEditorPitch(-90.0f) == doctest::Approx(-89.0f));
    CHECK(clampEditorPitch(1.0e9f) == doctest::Approx(89.0f));
    CHECK(clampEditorPitch(-Infinity) == doctest::Approx(-89.0f));
    CHECK(clampEditorPitch(NaN) == doctest::Approx(0.0f)); // std::clamp rendrait NaN
}

TEST_CASE("caméra de l'éditeur : un tangage hors bornes ne retourne pas la vue")
{
    // Au-delà de 90° l'avant passe de l'autre côté de la verticale, et la droite que lookAtRH
    // cherche par son produit avec la verticale change de sens : l'image est à l'envers. L'état
    // peut porter n'importe quel tangage (un fichier, un champ tapé) : la vue ne doit pas le
    // suivre.
    for (const float yaw : {0.0f, 33.0f, 200.0f})
    {
        for (const float pitch : {-200.0f, -90.0f, 90.0f, 95.0f, 180.0f})
        {
            const EditorCamera camera{
                .position = {1.0f, 2.0f, 3.0f}, .yawDegrees = yaw, .pitchDegrees = pitch};
            const levain::render::Camera render{
                .position = camera.position, .target = camera.position + viewDirectionOf(camera)};
            const glm::mat4 view = levain::render::viewOf(render);
            // Les lignes de la vue sont la droite, le haut et l'arrière de la caméra.
            const glm::vec3 right{view[0][0], view[1][0], view[2][0]};
            CHECK(glm::dot(right, rightOf(camera)) == doctest::Approx(1.0f));
            CHECK(view[1][1] > 0.0f); // le haut de l'écran regarde vers le haut du monde
        }
    }
}

TEST_CASE("caméra de l'éditeur : un lacet qui n'est pas un nombre ne donne jamais de vue NaN")
{
    // Le lacet vient de l'état, qui peut le lire d'un fichier : comme le tangage, il est replié
    // avant de servir, sinon l'avant est NaN, la cible du rendu aussi, puis la position après une
    // orbite.
    for (const float yaw : {NaN, Infinity, -Infinity})
    {
        CAPTURE(yaw);
        const EditorCamera camera{.position = {1.0f, 2.0f, 3.0f}, .yawDegrees = yaw};
        CHECK(isFinite(viewDirectionOf(camera)));
        CHECK(isFinite(rightOf(camera)));
        CHECK(isFinite(upOf(camera)));
        CHECK(glm::distance(viewDirectionOf(camera), viewDirectionOf(EditorCamera{})) < 1.0e-6f);
    }
    // Un lacet de plusieurs tours regarde comme le même lacet replié.
    const EditorCamera turned{.yawDegrees = 395.0f};
    const EditorCamera folded{.yawDegrees = 35.0f};
    CHECK(glm::distance(viewDirectionOf(turned), viewDirectionOf(folded)) < 1.0e-5f);
}

TEST_CASE("caméra de l'éditeur : la vitesse de vol reste dans ses bornes")
{
    CHECK(clampFlySpeed(5.0f) == doctest::Approx(5.0f));
    CHECK(clampFlySpeed(0.0f) == doctest::Approx(levain::editor::MinFlySpeed));
    CHECK(clampFlySpeed(1.0e9f) == doctest::Approx(levain::editor::MaxFlySpeed));
    CHECK(clampFlySpeed(NaN) == doctest::Approx(levain::editor::DefaultFlySpeed));

    // Un cran multiplie, il n'ajoute pas : 8 → 10 → 12,5, et un cran en arrière redescend.
    const EditorCamera camera{.flySpeed = 8.0f};
    CHECK(scaleFlySpeed(camera, 1.0f).flySpeed == doctest::Approx(10.0f));
    CHECK(scaleFlySpeed(scaleFlySpeed(camera, 1.0f), 1.0f).flySpeed == doctest::Approx(12.5f));
    CHECK(scaleFlySpeed(camera, -1.0f).flySpeed == doctest::Approx(6.4f));
    CHECK(scaleFlySpeed(camera, NaN).flySpeed == doctest::Approx(8.0f));
    // 1,25ⁿ déborde d'un `float` vers l'infini, ou s'évanouit : la vitesse tombe sur une borne.
    CHECK(scaleFlySpeed(camera, 1.0e30f).flySpeed == doctest::Approx(levain::editor::MaxFlySpeed));
    CHECK(scaleFlySpeed(camera, -1.0e30f).flySpeed == doctest::Approx(levain::editor::MinFlySpeed));

    // Une molette sans fin ne sort jamais des bornes, dans un sens comme dans l'autre.
    EditorCamera fast = camera;
    EditorCamera slow = camera;
    for (int notch = 0; notch < 200; ++notch)
    {
        fast = scaleFlySpeed(fast, 1.0f);
        slow = scaleFlySpeed(slow, -1.0f);
    }
    CHECK(fast.flySpeed == doctest::Approx(levain::editor::MaxFlySpeed));
    CHECK(slow.flySpeed == doctest::Approx(levain::editor::MinFlySpeed));
}

TEST_CASE("caméra de l'éditeur : le lacet se replie entre -180° et 180°")
{
    CHECK(wrapYawDegrees(190.0f) == doctest::Approx(-170.0f));
    CHECK(wrapYawDegrees(-190.0f) == doctest::Approx(170.0f));
    CHECK(wrapYawDegrees(730.0f) == doctest::Approx(10.0f));
    CHECK(wrapYawDegrees(360.0f) == doctest::Approx(0.0f));
    CHECK(wrapYawDegrees(NaN) == doctest::Approx(0.0f));
    CHECK(wrapYawDegrees(Infinity) == doctest::Approx(0.0f));
}

TEST_CASE(
    "caméra de l'éditeur : le pivot est devant l'œil, et le haut est perpendiculaire au regard")
{
    const EditorCamera camera{.position = {10.0f, 3.0f, -7.0f},
                              .yawDegrees = 35.0f,
                              .pitchDegrees = 20.0f,
                              .pivotDistance = 40.0f};
    const glm::vec3 forward = viewDirectionOf(camera);
    CHECK(glm::length(forward) == doctest::Approx(1.0f));
    CHECK(glm::distance(pivotOf(camera), camera.position + (forward * 40.0f)) < 1.0e-4f);

    // Droite, haut et avant forment un repère : le pan se déplace dans le plan des deux premiers.
    CHECK(glm::dot(upOf(camera), forward) == doctest::Approx(0.0f).epsilon(1.0e-4));
    CHECK(glm::dot(upOf(camera), rightOf(camera)) == doctest::Approx(0.0f).epsilon(1.0e-4));
    CHECK(glm::length(upOf(camera)) == doctest::Approx(1.0f));
    CHECK(upOf(camera).y > 0.0f);
    CHECK(glm::dot(rightOf(camera), forward) == doctest::Approx(0.0f).epsilon(1.0e-4));

    // Le regard droit devant : le haut est la verticale du monde.
    const EditorCamera level{.yawDegrees = 90.0f};
    CHECK(glm::distance(upOf(level), glm::vec3{0.0f, 1.0f, 0.0f}) < 1.0e-5f);
}
