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
