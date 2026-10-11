// La scène dans une texture (ADR-0036, décision 7) : les fonctions pures qui décident quand la
// texture se recrée et où va la scène. Le GPU suit `levain_scene_view` (gpu.scene-view.*).

#include <doctest/doctest.h>
#include <imgui.h>

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

TEST_CASE("le centre du docking ne laisse passer l'image que tant que la scène est dans la fenêtre")
{
    using levain::app::dockspaceFlagsFor;
    CHECK(dockspaceFlagsFor(SceneOutput::Window) == ImGuiDockNodeFlags_PassthruCentralNode);
    CHECK(dockspaceFlagsFor(SceneOutput::Texture) == ImGuiDockNodeFlags_None);
    CHECK(dockspaceFlagsFor(SceneOutput::Nothing) == ImGuiDockNodeFlags_None);
}

TEST_CASE("où le curseur vise dans la scène : la fenêtre, ou le rectangle de la Vue, jamais dehors")
{
    using levain::app::scenePixelAt;
    using levain::app::ScreenRect;
    constexpr ScreenRect View{.x = 200.0f, .y = 30.0f, .width = 800.0f, .height = 450.0f};
    // La scène dans la fenêtre : le curseur et la fenêtre, comme avant la Vue.
    constexpr levain::app::ScenePixel None{-1.0f, -1.0f, -1.0f,
                                           -1.0f}; // vu seulement si rien n'est visé
    const auto inWindow =
        scenePixelAt(SceneOutput::Window, View, 1920.0f, 1080.0f, 50.0f, 60.0f).value_or(None);
    CHECK((inWindow.x == 50.0f && inWindow.y == 60.0f));
    CHECK((inWindow.width == 1920.0f && inWindow.height == 1080.0f));
    // Dans la Vue : ramené au coin du rectangle, et la taille du rectangle.
    const auto inView =
        scenePixelAt(SceneOutput::Texture, View, 1920.0f, 1080.0f, 600.0f, 255.0f).value_or(None);
    CHECK((inView.x == 400.0f && inView.y == 225.0f));
    CHECK((inView.width == 800.0f && inView.height == 450.0f));
    // Le coin haut gauche en est ; hors du rectangle, ou sans scène, rien n'est visé.
    CHECK(scenePixelAt(SceneOutput::Texture, View, 1920.0f, 1080.0f, 200.0f, 30.0f));
    CHECK_FALSE(scenePixelAt(SceneOutput::Texture, View, 1920.0f, 1080.0f, 100.0f, 255.0f));
    CHECK_FALSE(scenePixelAt(SceneOutput::Texture, View, 1920.0f, 1080.0f, 1000.0f, 255.0f));
    CHECK_FALSE(scenePixelAt(SceneOutput::Nothing, View, 1920.0f, 1080.0f, 600.0f, 255.0f));
    CHECK_FALSE(scenePixelAt(SceneOutput::Texture, ScreenRect{}, 1920.0f, 1080.0f, 0.0f, 0.0f));
    CHECK_FALSE(scenePixelAt(SceneOutput::Window, View, 0.0f, 0.0f, 5.0f, 5.0f));
}

TEST_CASE("la taille où se lit un pixel : la fenêtre, le rectangle de la Vue, ou rien")
{
    using levain::app::sceneExtentOf;
    using levain::app::ScreenRect;
    constexpr ScreenRect View{.x = 200.0f, .y = 30.0f, .width = 800.0f, .height = 450.0f};
    const auto window = sceneExtentOf(SceneOutput::Window, View, 1920.0f, 1080.0f);
    CHECK((window.width == 1920.0f && window.height == 1080.0f));
    const auto view = sceneExtentOf(SceneOutput::Texture, View, 1920.0f, 1080.0f);
    CHECK((view.width == 800.0f && view.height == 450.0f));
    // Nulle part, rectangle vide ou fenêtre sans taille : 0 x 0, jamais une taille inventée.
    const auto nothing = sceneExtentOf(SceneOutput::Nothing, View, 1920.0f, 1080.0f);
    CHECK((nothing.width == 0.0f && nothing.height == 0.0f));
    const auto empty = sceneExtentOf(SceneOutput::Texture, ScreenRect{}, 1920.0f, 1080.0f);
    CHECK((empty.width == 0.0f && empty.height == 0.0f));
    const auto noWindow = sceneExtentOf(SceneOutput::Window, View, 0.0f, 0.0f);
    CHECK((noWindow.width == 0.0f && noWindow.height == 0.0f));
}
