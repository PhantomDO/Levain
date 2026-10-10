#include <optional>
#include <string>

#include <doctest/doctest.h>
#include <glm/gtc/matrix_transform.hpp>

#include "levain/app/models.hpp"

TEST_CASE("un modèle est skinné dès qu'une de ses primitives a des os")
{
    levain::assets::Model model;
    model.meshes.resize(2);
    model.meshes[0].primitives.resize(1);
    model.meshes[1].primitives.resize(2);
    CHECK_FALSE(levain::app::isSkinned(model));
    model.meshes[1].primitives[1].joints.push_back({0, 1, 0, 0});
    CHECK(levain::app::isSkinned(model));
}

TEST_CASE("un clip se trouve par son nom, et un nom inconnu liste les clips du modèle")
{
    levain::animation::AnimationSet set;
    CHECK_FALSE(levain::app::clipIndexOf(set, std::nullopt).has_value()); // sans clip : un échec
    set.clips = {{.name = "Survey", .durationSeconds = 3.4f},
                 {.name = "Walk", .durationSeconds = 0.7f}};
    CHECK(levain::app::clipIndexOf(set, std::nullopt).value_or(9) == 0); // le premier par défaut
    CHECK(levain::app::clipIndexOf(set, std::string{"Walk"}).value_or(9) == 1);
    const auto unknown = levain::app::clipIndexOf(set, std::string{"Run"});
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().message.find("Survey, Walk") != std::string::npos);
}

TEST_CASE("la vitesse d'os la plus grande entre deux poses, par seconde")
{
    levain::animation::Pose before;
    before.joints = {glm::mat4{1.0f}, glm::mat4{1.0f}};
    levain::animation::Pose after = before;
    // Le second os avance de 3 unités en 0,5 s : 6 unités/s ; le premier ne bouge pas.
    after.joints[1] = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 3.0f, 0.0f});
    CHECK(levain::app::maxJointSpeedOf(before, after, 0.5f) == doctest::Approx(6.0f));
    CHECK(levain::app::maxJointSpeedOf(before, before, 0.5f) == 0.0f);
}

TEST_CASE("l'horloge des squelettes s'arrête avec la simulation, et repart sans saut")
{
    using levain::app::advanceAnimationClock;
    levain::app::AnimationClock clock;

    // La simulation tourne : l'horloge des squelettes est celle de la scène.
    CHECK(advanceAnimationClock(clock, 1.0, false) == 1.0);
    CHECK(advanceAnimationClock(clock, 1.5, false) == 1.5);

    // Arrêtée de 1,5 s à 4 s : les squelettes restent où ils étaient, quel que soit le rythme des
    // images.
    CHECK(advanceAnimationClock(clock, 2.0, true) == 1.5);
    CHECK(advanceAnimationClock(clock, 3.0, true) == 1.5);
    CHECK(advanceAnimationClock(clock, 4.0, true) == 1.5);

    // Reprise : ils repartent de 1,5 s, non de 4 s (un saut de pose), puis vont au rythme de la
    // scène.
    CHECK(advanceAnimationClock(clock, 4.5, false) == 2.0);
    CHECK(advanceAnimationClock(clock, 5.0, false) == 2.5);
}

TEST_CASE("l'horloge des squelettes suit `--time`, qui fige le temps de la scène")
{
    using levain::app::advanceAnimationClock;
    levain::app::AnimationClock clock;
    // Le temps de la scène ne change pas d'une image à l'autre : arrêt ou reprise, celui des
    // squelettes non plus.
    for (const bool paused : {false, true, false, true})
    {
        CHECK(advanceAnimationClock(clock, 7.0, paused) == doctest::Approx(7.0));
    }
}

TEST_CASE("l'horloge des squelettes démarrée à l'arrêt : la première image n'y compte pas d'arrêt")
{
    using levain::app::advanceAnimationClock;
    // L'éditeur s'ouvre en Édition : arrêtée dès la première image, sous `--time 7` comme sur
    // l'horloge réelle. Les squelettes se figent à T, non à 0.
    levain::app::AnimationClock frozen;
    for (const bool paused : {true, true, false, true})
    {
        CHECK(advanceAnimationClock(frozen, 7.0, paused) == doctest::Approx(7.0));
    }

    // Sur l'horloge réelle, arrêtée dès la première image : les squelettes restent où elle les
    // trouve, puis repartent de là.
    levain::app::AnimationClock running;
    CHECK(advanceAnimationClock(running, 0.25, true) == 0.25);
    CHECK(advanceAnimationClock(running, 1.25, true) == 0.25);
    CHECK(advanceAnimationClock(running, 2.0, false) == 1.0);
}
