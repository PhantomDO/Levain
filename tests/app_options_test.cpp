#include <filesystem>
#include <fstream>

#include <doctest/doctest.h>

#include "levain/app/app.hpp"
#include "levain/assets/asset_ref.hpp"
#include "levain/gpu/device.hpp"

// Les options communes de la ligne de commande (ADR-0029) : le contrat que le sandbox et *Rando*
// partagent, et ce que lit la CI.

TEST_CASE("--steps prend un entier strictement positif, au plus un million")
{
    using levain::app::OptionUse;
    levain::app::AppSettings settings;
    CHECK(levain::app::parseCommonOption(settings, "--steps", "120") == OptionUse::Taken);
    CHECK(settings.steps == 120);
    for (const char* refused : {"1.5", "0", "1000001", "nan", "-3", "12x"})
    {
        CAPTURE(refused);
        CHECK(levain::app::parseCommonOption(settings, "--steps", refused) == OptionUse::Invalid);
    }
}

TEST_CASE("les options à valeurs fixées refusent toute autre valeur")
{
    using levain::app::OptionUse;
    levain::app::AppSettings settings;
    CHECK(levain::app::parseCommonOption(settings, "--gpu", "webgpu") == OptionUse::Taken);
    CHECK(settings.api == nvrhi::GraphicsAPI::WEBGPU);
    CHECK(levain::app::parseCommonOption(settings, "--gpu", "metal") == OptionUse::Invalid);
    CHECK(levain::app::parseCommonOption(settings, "--tonemap", "aces") == OptionUse::Taken);
    CHECK(settings.tonemap.tonemapper == levain::render::Tonemapper::Aces);
    CHECK(levain::app::parseCommonOption(settings, "--tonemap", "filmic") == OptionUse::Invalid);
    CHECK(levain::app::parseCommonOption(settings, "--sun", "1,2") == OptionUse::Invalid);
    CHECK(levain::app::parseCommonOption(settings, "--sun", "0,1,0") == OptionUse::Taken);
}

TEST_CASE("--gpu lit les trois backends, et seul un build Windows construit Direct3D 12")
{
    using levain::app::OptionUse;
    levain::app::AppSettings settings;
    CHECK(levain::app::parseCommonOption(settings, "--gpu", "d3d12") == OptionUse::Taken);
    CHECK(settings.api == nvrhi::GraphicsAPI::D3D12);
    CHECK(levain::app::parseCommonOption(settings, "--gpu", "vulkan") == OptionUse::Taken);
    CHECK(settings.api == nvrhi::GraphicsAPI::VULKAN);
    CHECK(levain::app::parseCommonOption(settings, "--gpu", "dx12") == OptionUse::Invalid);
    CHECK(levain::app::parseCommonOption(settings, "--gpu", "D3D12") == OptionUse::Invalid);

    CHECK(levain::gpu::requireBackendBuilt(nvrhi::GraphicsAPI::VULKAN).has_value());
    CHECK(levain::gpu::requireBackendBuilt(nvrhi::GraphicsAPI::WEBGPU).has_value());
    CHECK_FALSE(levain::gpu::requireBackendBuilt(nvrhi::GraphicsAPI::D3D11).has_value());
    // Le refus dit pourquoi : c'est ce qu'affiche le programme lancé avec --gpu d3d12.
    const auto d3d12 = levain::gpu::requireBackendBuilt(nvrhi::GraphicsAPI::D3D12);
#ifdef _WIN32
    CHECK(d3d12.has_value());
#else
    CHECK((!d3d12 && d3d12.error().message.contains("n'existe que dans un build Windows")));
#endif
}

TEST_CASE("les nombres doivent être positifs et sans rien d'autre dans le texte")
{
    using levain::app::OptionUse;
    levain::app::AppSettings settings;
    CHECK(levain::app::parseCommonOption(settings, "--seconds", "2.5") == OptionUse::Taken);
    CHECK(settings.loopSeconds == 2.5);
    CHECK(levain::app::parseCommonOption(settings, "--seconds", "-1") == OptionUse::Invalid);
    CHECK(levain::app::parseCommonOption(settings, "--seconds", "3x") == OptionUse::Invalid);
    CHECK(levain::app::parseCommonOption(settings, "--exposure", "2") == OptionUse::Taken);
    CHECK(settings.tonemap.exposure == 2.0f);
}

TEST_CASE("les chemins se rangent tels quels, et les options du programme lui reviennent")
{
    using levain::app::OptionUse;
    levain::app::AppSettings settings;
    CHECK(levain::app::parseCommonOption(settings, "--capture", "out.png") == OptionUse::Taken);
    CHECK(settings.capturePath == std::filesystem::path{"out.png"});
    CHECK(levain::app::parseCommonOption(settings, "--sky", "none") == OptionUse::Taken);
    CHECK(settings.sky == std::filesystem::path{"none"});
    CHECK(levain::app::parseCommonOption(settings, "--input-script", "gestes.txt") ==
          OptionUse::Taken);
    CHECK(settings.inputScriptFile == std::filesystem::path{"gestes.txt"});
    CHECK(levain::app::parseCommonOption(settings, "--view", "hike") == OptionUse::NotMine);
    CHECK(levain::app::parseCommonOption(settings, "--walk", "1,0") == OptionUse::NotMine);
}

TEST_CASE("inputScriptOf : le fichier est lu, un script refusé, absent ou vide est une erreur")
{
    // `runApp` s'arrête sur cette erreur avant sa fenêtre (tests/app_script_gpu.cpp le joue de bout
    // en bout) : ici, sans fenêtre ni device, ce qu'elle refuse.
    namespace fs = std::filesystem;
    using levain::app::inputScriptOf;
    const fs::path file = fs::temp_directory_path() /
                          ("levain-app-options-" +
                           levain::assets::toString(levain::assets::generateAssetId()) + ".txt");
    levain::app::AppSettings settings;
    CHECK_FALSE(inputScriptOf(settings).value().has_value()); // sans script, rien n'est touché

    settings.inputScriptFile = file; // absent
    CHECK_FALSE(inputScriptOf(settings).has_value());
    std::ofstream(file) << "0 key down W\n1 key down Wxyz\n"; // touche inconnue
    const auto refused = inputScriptOf(settings);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message.find("ligne 2, touche") != std::string::npos);
    std::ofstream(file) << "0 key down W\n";
    const auto read = inputScriptOf(settings);
    fs::remove(file);
    CHECK((read && *read ? levain::platform::scriptLength(**read) : 0) == 1);

    // L'API de test : un script sans événement, écrit à la main, ne rejouerait rien non plus.
    settings.inputScriptFile.reset();
    settings.inputScript = levain::platform::InputScript{};
    CHECK_FALSE(inputScriptOf(settings).has_value());
    settings.inputScript = levain::platform::InputScript{.frames = {{3, {}}}};
    CHECK(inputScriptOf(settings).has_value());
}
