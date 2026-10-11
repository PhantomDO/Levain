// La scène dans une texture (ADR-0036, décision 7) : les fonctions pures qui décident quand la
// texture se recrée et où va la scène.

#include <doctest/doctest.h>

#include "levain/app/scene_view.hpp"

using levain::app::SceneOutput;
using levain::app::SceneSize;
using levain::app::SizeSettling;

namespace
{

constexpr SceneSize Window{.width = 320, .height = 180};
constexpr SceneSize Wide{.width = 640, .height = 360};
constexpr int Frames = levain::app::StableFramesBeforeResize;

} // namespace

TEST_CASE("la première taille demandée crée la texture tout de suite : il n'y a pas d'ancienne "
          "image à étirer")
{
    SizeSettling settling;
    CHECK(levain::app::shouldRecreateScene(settling, SceneSize{}, Wide, Frames));
}

TEST_CASE("une autre taille n'est reprise qu'après StableFramesBeforeResize images de suite")
{
    SizeSettling settling;
    // La texture fait 320 × 180 ; la Vue en demande 640 × 360, image après image.
    for (int frame = 1; frame < Frames; ++frame)
    {
        CHECK_FALSE(levain::app::shouldRecreateScene(settling, Window, Wide, Frames));
    }
    CHECK(levain::app::shouldRecreateScene(settling, Window, Wide, Frames));
}

TEST_CASE("un glissé qui change la taille à chaque image ne recrée jamais la texture")
{
    SizeSettling settling;
    for (std::uint32_t frame = 0; frame < 30; ++frame)
    {
        const SceneSize dragged{.width = 400 + frame, .height = 200 + frame};
        CHECK_FALSE(levain::app::shouldRecreateScene(settling, Window, dragged, Frames));
    }
}

TEST_CASE("une taille qui est déjà celle de la texture ne la recrée pas, et un retour à elle "
          "annule l'attente")
{
    SizeSettling settling;
    for (int frame = 0; frame < 3 * Frames; ++frame)
    {
        CHECK_FALSE(levain::app::shouldRecreateScene(settling, Window, Window, Frames));
    }
    // 640 × 360 demandée une image, puis la taille de la texture : l'attente repart de zéro.
    CHECK_FALSE(levain::app::shouldRecreateScene(settling, Window, Wide, Frames));
    CHECK_FALSE(levain::app::shouldRecreateScene(settling, Window, Window, Frames));
    CHECK_FALSE(levain::app::shouldRecreateScene(settling, Window, Wide, Frames));
}

TEST_CASE("une demande vide ne recrée rien, et fait repartir l'attente de la taille qui suit")
{
    SizeSettling settling;
    CHECK_FALSE(levain::app::shouldRecreateScene(settling, Window, SceneSize{}, Frames));
    CHECK_FALSE(levain::app::shouldRecreateScene(settling, SceneSize{}, SceneSize{}, Frames));
    // Une Vue rouverte à la taille de la texture d'avant, ou à une autre : l'attente est celle
    // d'une taille neuve.
    CHECK_FALSE(levain::app::shouldRecreateScene(settling, Window, Wide, Frames));
    CHECK(levain::app::shouldRecreateScene(settling, Window, Wide, Frames));
}

TEST_CASE("où va la scène : rien demandé, dans la fenêtre, une Vue fermée ou sans texture, nulle "
          "part, sinon dans la texture")
{
    using levain::app::sceneOutputFor;
    CHECK(sceneOutputFor(std::nullopt, false) == SceneOutput::Window);
    CHECK(sceneOutputFor(std::nullopt, true) == SceneOutput::Window);
    CHECK(sceneOutputFor(SceneSize{}, true) == SceneOutput::Nothing);
    CHECK(sceneOutputFor(SceneSize{.width = 640, .height = 0}, true) == SceneOutput::Nothing);
    CHECK(sceneOutputFor(Wide, false) == SceneOutput::Nothing);
    CHECK(sceneOutputFor(Wide, true) == SceneOutput::Texture);
}
