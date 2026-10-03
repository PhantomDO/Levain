#include <doctest/doctest.h>

#include "levain/render/stages.hpp"

TEST_CASE("les fonctions d'une étape gardent l'ordre de leur inscription, et la description le dit")
{
    using levain::render::RenderStage;
    levain::render::RenderStages stages;
    CHECK(levain::render::describeStages(stages) ==
          "ombres : aucune ; opaques : aucune ; transparents : aucune");
    const auto nothing = [](const levain::render::StageContext&) {};
    levain::render::addStageFunction(stages, RenderStage::Opaque, "démo", nothing);
    levain::render::addStageFunction(stages, RenderStage::ShadowCasters, "démo", nothing);
    levain::render::addStageFunction(stages, RenderStage::Opaque, "terrain", nothing);
    CHECK(levain::render::describeStages(stages) ==
          "ombres : démo ; opaques : démo, terrain ; transparents : aucune");
    const auto& opaque = stages.entries[static_cast<std::size_t>(RenderStage::Opaque)];
    REQUIRE(opaque.size() == 2);
    CHECK(opaque[0].name == "démo");
    CHECK(opaque[1].name == "terrain");
}
