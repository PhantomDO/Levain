// La STL de Microsoft n'inclut pas <ostream> par <string_view>, que doctest affiche (ADR-0035).
#include <ostream>
#include <string_view>

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

TEST_CASE("le temps GPU d'une fonction d'étape est la somme des moyennes de ses appels")
{
    using levain::render::RenderStage;
    levain::render::RenderStages stages;
    const auto nothing = [](const levain::render::StageContext&) {};
    levain::render::addStageFunction(stages, RenderStage::ShadowCasters, "terrain", nothing);
    levain::render::addStageFunction(stages, RenderStage::Opaque, "herbe", nothing);
    CHECK(levain::render::describeStageTimes(stages) == "aucune mesure");

    // Deux cascades d'ombres, mesurées un nombre de fois différent : une lecture manquée ne fausse
    // que la moyenne de son appel. 0,1 ms puis 0,3 ms par image : 0,4 ms.
    auto& terrain = stages.entries[static_cast<std::size_t>(RenderStage::ShadowCasters)][0];
    terrain.calls.resize(2);
    terrain.calls[0].average = {.totalMs = 0.3, .samples = 3};
    terrain.calls[1].average = {.totalMs = 0.6, .samples = 2};
    CHECK(levain::render::describeStageTimes(stages) == "ombres/terrain 0.400 ms");

    // Le nom de la zone et celui de la courbe : l'étape devant, pour que le terrain des ombres et
    // celui des opaques restent deux zones dans Tracy.
    CHECK(std::string_view{terrain.label} == "ombres/terrain");
    CHECK(std::string_view{terrain.plotName} == "GPU ombres/terrain");
}
