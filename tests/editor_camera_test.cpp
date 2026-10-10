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
using levain::editor::flyCamera;
using levain::editor::FlyInput;
using levain::editor::orbitCamera;
using levain::editor::panCamera;
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

/// Une caméra qui regarde en biais, loin de l'origine et des axes : une faute de signe ou d'axe ne
/// tombe pas sur un zéro qui la masque.
EditorCamera tiltedCamera()
{
    return {.position = {10.0f, 3.0f, -7.0f},
            .yawDegrees = 35.0f,
            .pitchDegrees = 20.0f,
            .pivotDistance = 40.0f,
            .flySpeed = 8.0f};
}

bool isFinite(const glm::vec3& vector)
{
    return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
}

/// La caméra du rendu de cette caméra, écrite à la main : la projection n'est pas celle de la
/// logique qu'on vérifie.
levain::render::Camera renderCameraBy(const EditorCamera& camera)
{
    return {.position = camera.position,
            .target = camera.position + viewDirectionOf(camera),
            .verticalFovRadians = glm::radians(camera.verticalFovDegrees),
            .nearPlane = 0.1f,
            .farPlane = 1000.0f};
}

glm::vec3 movedBy(const EditorCamera& camera, const FlyInput& input, float seconds = 1.0f)
{
    return flyCamera(camera, input, seconds).position - camera.position;
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
        CHECK(isFinite(orbitCamera(camera, {10.0f, 0.0f}).position));
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

    // Mille tours de souris, par petits coups : le lacet ne croît pas sans fin.
    EditorCamera camera;
    for (int i = 0; i < 1000; ++i)
    {
        camera = flyCamera(camera, {.lookPixels = {-137.0f, 0.0f}}, 0.016f);
        CHECK(std::abs(camera.yawDegrees) <= 180.0f);
    }
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

TEST_CASE("caméra de l'éditeur : le regard suit la souris, et le tangage reste borné")
{
    const EditorCamera camera;
    // 40 pixels à droite, 20 vers le bas : on tourne à droite (le lacet décroît) et on baisse les
    // yeux.
    const EditorCamera turned = flyCamera(camera, {.lookPixels = {40.0f, 20.0f}}, 0.016f);
    CHECK(turned.yawDegrees == doctest::Approx(-10.0f));
    CHECK(turned.pitchDegrees == doctest::Approx(-5.0f));
    CHECK(turned.position == camera.position); // le regard seul ne déplace pas

    CHECK(flyCamera(camera, {.lookPixels = {0.0f, -1.0e6f}}, 0.016f).pitchDegrees ==
          doctest::Approx(89.0f));
    CHECK(flyCamera(camera, {.lookPixels = {0.0f, 1.0e6f}}, 0.016f).pitchDegrees ==
          doctest::Approx(-89.0f));

    // Un delta qui n'est pas un nombre est ignoré, il ne reste pas dans les angles.
    const EditorCamera garbage = flyCamera(camera, {.lookPixels = {NaN, Infinity}}, 0.016f);
    CHECK(garbage.yawDegrees == doctest::Approx(0.0f));
    CHECK(garbage.pitchDegrees == doctest::Approx(0.0f));
}

TEST_CASE("caméra de l'éditeur : voler avance le long du regard, tangage compris")
{
    const EditorCamera camera = tiltedCamera();

    // Une seconde à 8 unités par seconde, vers l'avant : exactement le long de l'avant, et en haut
    // puisque la caméra regarde en haut.
    const glm::vec3 forward = movedBy(camera, {.move = {0.0f, 0.0f, 1.0f}});
    CHECK(glm::length(forward) == doctest::Approx(8.0f));
    CHECK(glm::dot(glm::normalize(forward), viewDirectionOf(camera)) == doctest::Approx(1.0f));
    CHECK(forward.y > 0.0f);

    // La droite est à plat, la verticale est celle du monde : le tangage n'y change rien.
    const glm::vec3 right = movedBy(camera, {.move = {1.0f, 0.0f, 0.0f}});
    CHECK(glm::dot(glm::normalize(right), rightOf(camera)) == doctest::Approx(1.0f));
    CHECK(right.y == doctest::Approx(0.0f));
    const glm::vec3 up = movedBy(camera, {.move = {0.0f, 1.0f, 0.0f}});
    CHECK(up.x == doctest::Approx(0.0f));
    CHECK(up.y == doctest::Approx(8.0f));
    CHECK(up.z == doctest::Approx(0.0f));

    // Le pivot suit l'œil : voler ne change pas la distance à ce qu'on regarde.
    CHECK(glm::distance(pivotOf(flyCamera(camera, {.move = {0.0f, 0.0f, 1.0f}}, 1.0f)),
                        pivotOf(camera) + forward) < 1.0e-4f);

    // Le regard d'abord, le pas ensuite : tourner et avancer dans la même image va là où l'on
    // regarde maintenant, pas là où l'on regardait.
    const EditorCamera turned =
        flyCamera(camera, {.lookPixels = {-80.0f, 0.0f}, .move = {0.0f, 0.0f, 1.0f}}, 1.0f);
    CHECK(turned.yawDegrees == doctest::Approx(camera.yawDegrees + 20.0f));
    CHECK(glm::dot(glm::normalize(turned.position - camera.position), viewDirectionOf(turned)) ==
          doctest::Approx(1.0f));
}

TEST_CASE("caméra de l'éditeur : voler suit la durée et la vitesse, sans accélérer en diagonale")
{
    const EditorCamera camera = tiltedCamera();
    const FlyInput forward{.move = {0.0f, 0.0f, 1.0f}};

    CHECK(glm::length(movedBy(camera, {})) == doctest::Approx(0.0f)); // sans entrée, immobile
    CHECK(glm::length(movedBy(camera, forward, 0.5f)) == doctest::Approx(4.0f));
    CHECK(glm::length(movedBy(camera, {.move = {0.0f, 0.0f, 1.0f}, .speedMultiplier = 4.0f})) ==
          doctest::Approx(32.0f));
    CHECK(glm::length(movedBy(camera, {.move = {0.0f, 0.0f, 1.0f}, .speedMultiplier = -3.0f})) ==
          doctest::Approx(0.0f));

    // Avant et droite ensemble : la même vitesse qu'une ligne droite, pas 8 × √2.
    CHECK(glm::length(movedBy(camera, {.move = {1.0f, 0.0f, 1.0f}})) == doctest::Approx(8.0f));

    // Une durée ou un axe qui n'est pas un nombre, ou négatif, n'avance pas et ne pollue rien.
    CHECK(glm::length(movedBy(camera, forward, NaN)) == doctest::Approx(0.0f));
    CHECK(glm::length(movedBy(camera, forward, -1.0f)) == doctest::Approx(0.0f));
    CHECK(glm::length(movedBy(camera, {.move = {NaN, 0.0f, Infinity}})) == doctest::Approx(0.0f));

    // La vitesse de la caméra règle le vol, bornée.
    EditorCamera slow = camera;
    slow.flySpeed = 2.0f;
    CHECK(glm::length(movedBy(slow, forward)) == doctest::Approx(2.0f));
    slow.flySpeed = 1.0e9f;
    CHECK(glm::length(movedBy(slow, forward)) == doctest::Approx(levain::editor::MaxFlySpeed));
}

TEST_CASE("caméra de l'éditeur : l'orbite garde la distance et le pivot")
{
    for (const float yaw : {0.0f, 45.0f, -130.0f, 179.0f})
    {
        for (const float pitch : {-60.0f, 0.0f, 30.0f, 85.0f})
        {
            for (const glm::vec2 pixels : {glm::vec2{100.0f, 0.0f}, glm::vec2{0.0f, 50.0f},
                                           glm::vec2{-37.0f, 80.0f}, glm::vec2{5000.0f, -5000.0f}})
            {
                EditorCamera camera = tiltedCamera();
                camera.yawDegrees = yaw;
                camera.pitchDegrees = pitch;
                const glm::vec3 pivot = pivotOf(camera);
                const EditorCamera orbited = orbitCamera(camera, pixels);

                CHECK(glm::distance(orbited.position, pivot) ==
                      doctest::Approx(camera.pivotDistance).epsilon(1.0e-4));
                CHECK(orbited.pivotDistance == doctest::Approx(camera.pivotDistance));
                CHECK(glm::distance(pivotOf(orbited), pivot) < 1.0e-3f); // le pivot ne bouge pas
                CHECK(std::abs(orbited.pitchDegrees) <= 89.0f);
            }
        }
    }
}

TEST_CASE("caméra de l'éditeur : l'orbite regarde le pivot et tourne dans le sens de la souris")
{
    const EditorCamera camera{.position = {0.0f, 0.0f, 10.0f}, .pivotDistance = 10.0f};
    // La caméra regarde −Z, le pivot est à l'origine. Souris à droite : la caméra tourne à droite
    // autour du pivot, elle passe donc vers −X en regardant plus à droite (le lacet décroît).
    const EditorCamera orbited = orbitCamera(camera, {40.0f, 0.0f});
    CHECK(orbited.yawDegrees == doctest::Approx(-10.0f));
    CHECK(orbited.position.x < 0.0f);
    CHECK(glm::dot(glm::normalize(glm::vec3{0.0f} - orbited.position), viewDirectionOf(orbited)) ==
          doctest::Approx(1.0f));

    // Deux cents petits coups, les mêmes : ni la distance ni le pivot ne dérivent.
    EditorCamera drifting = camera;
    for (int i = 0; i < 200; ++i)
    {
        drifting = orbitCamera(drifting, {7.0f, -3.0f});
    }
    CHECK(glm::distance(drifting.position, glm::vec3{0.0f}) ==
          doctest::Approx(10.0f).epsilon(1.0e-3));
    CHECK(glm::distance(pivotOf(drifting), glm::vec3{0.0f}) < 1.0e-2f);

    // Un delta qui n'est pas un nombre ne déplace rien.
    CHECK(orbitCamera(camera, {NaN, Infinity}).position == camera.position);
}

TEST_CASE("caméra de l'éditeur : le pan garde le point visé sous le curseur")
{
    constexpr float Width = 1280.0f;
    constexpr float Height = 720.0f;
    const EditorCamera camera = tiltedCamera();
    const glm::vec3 pivot = pivotOf(camera);

    // 100 pixels à droite et 40 vers le haut du curseur : le pivot, qui était au centre de l'image,
    // doit s'y trouver, à ce pixel près.
    const EditorCamera panned = panCamera(camera, {100.0f, -40.0f}, Height);
    const glm::mat4 viewProjection =
        levain::render::viewProjectionOf(renderCameraBy(panned), Width / Height);
    const glm::vec4 clip = viewProjection * glm::vec4{pivot, 1.0f};
    const glm::vec2 expected = levain::render::ndcOfPixel(
        {(Width / 2.0f) + 100.0f, (Height / 2.0f) - 40.0f}, Width, Height);
    CHECK(clip.x / clip.w == doctest::Approx(expected.x).epsilon(1.0e-4));
    CHECK(clip.y / clip.w == doctest::Approx(expected.y).epsilon(1.0e-4));

    // On glisse sans tourner ni changer de distance, dans le plan de l'écran.
    CHECK(panned.yawDegrees == camera.yawDegrees);
    CHECK(panned.pitchDegrees == camera.pitchDegrees);
    CHECK(panned.pivotDistance == camera.pivotDistance);
    CHECK(std::abs(glm::dot(panned.position - camera.position, viewDirectionOf(camera))) < 1.0e-3f);
}

TEST_CASE("caméra de l'éditeur : le pan suit la droite et le haut de l'écran, à l'échelle du pivot")
{
    const EditorCamera camera = tiltedCamera();
    // À 40 unités, un champ de 60° sur 720 pixels : 2 · 40 · tan(30°) / 720 unités par pixel.
    const float unitsPerPixel = 2.0f * 40.0f * std::tan(glm::radians(30.0f)) / 720.0f;

    const glm::vec3 toTheRight =
        panCamera(camera, {10.0f, 0.0f}, 720.0f).position - camera.position;
    CHECK(glm::distance(toTheRight, rightOf(camera) * (-10.0f * unitsPerPixel)) < 1.0e-4f);
    const glm::vec3 down = panCamera(camera, {0.0f, 10.0f}, 720.0f).position - camera.position;
    CHECK(glm::distance(down, upOf(camera) * (10.0f * unitsPerPixel)) < 1.0e-4f);

    // Deux fois plus loin du pivot, deux fois plus d'unités par pixel.
    EditorCamera farther = camera;
    farther.pivotDistance = 80.0f;
    CHECK(glm::length(panCamera(farther, {10.0f, 0.0f}, 720.0f).position - farther.position) ==
          doctest::Approx(2.0f * glm::length(toTheRight)));

    // Une fenêtre sans hauteur, ou un delta absurde, ne déplace rien.
    CHECK(panCamera(camera, {10.0f, 10.0f}, 0.0f).position == camera.position);
    CHECK(panCamera(camera, {10.0f, 10.0f}, NaN).position == camera.position);
    CHECK(panCamera(camera, {NaN, Infinity}, 720.0f).position == camera.position);
}
