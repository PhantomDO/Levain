// Le banc d'essai de l'App (ADR-0036, morceau 2) : un script d'input rejoué dans la vraie boucle
// (`runApp` : événements, ImGui, `gameInputOf`, actions, `PlayerInput`, rendu), hors écran. Une
// touche scriptée atteint `PlayerInput` par sa position, et ImGui par sa lettre : sur un AZERTY
// (`W as z`), le jeu voit la position W et ImGui la touche Z. Il faut un device, comme
// `levain_ui_gpu`.
//   levain_app_script [vulkan|d3d12|d3d12-warp]

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <print>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "gpu_test_backend.hpp"

#include "levain/app/app.hpp"
#include "levain/app/player_input.hpp"
#include "levain/assets/asset_ref.hpp"
#include "levain/scene/components.hpp"

namespace
{

using levain::app::App;

/// Ce que le programme de test voit à chaque image : le jeu (`PlayerInput`), puis ImGui.
struct Seen
{
    float forward = 0.0f; ///< l'axe `move_forward` (touches W et S)
    bool imguiW = false;
    bool imguiZ = false;
    bool operator==(const Seen&) const = default;
};

/// Un programme minimal : une caméra, et deux points d'accroche qui notent ce que chaque image a
/// vu.
levain::app::StartFunction recordInto(std::vector<Seen>& seen)
{
    return [&seen](App& app) -> levain::core::Result<levain::app::FrameHooks>
    {
        app.world.entity("camera").set(levain::scene::Transform{}).set(levain::app::CameraLens{});
        const auto forward = levain::input::axisIndex(app.bindings, "move_forward");
        if (!forward)
        {
            return levain::core::makeError(levain::core::ErrorCode::InvalidData,
                                           "data/input.cfg : move_forward manque");
        }
        levain::app::FrameHooks hooks;
        hooks.frame = [&seen, forward = *forward](App& frameApp)
        {
            const auto& state = frameApp.world.get<levain::app::PlayerInput>().state;
            seen.push_back({.forward = levain::input::axisValue(state, forward)});
        };
        hooks.ui = [&seen](App&)
        {
            seen.back().imguiW = ImGui::IsKeyDown(ImGuiKey_W);
            seen.back().imguiZ = ImGui::IsKeyDown(ImGuiKey_Z);
        };
        return hooks;
    };
}

struct Run
{
    int exitCode = 0;
    std::vector<Seen> seen;
};

/// Le programme de test dans la vraie boucle, hors écran, avec les réglages demandés (le script).
Run play(const levain::tests::TestBackend& backend, levain::app::AppSettings settings)
{
    settings.width = 320;
    settings.height = 180;
    settings.assetRoots = {LEVAIN_DATA_DIR};
    settings.bindingsFile = LEVAIN_DATA_DIR "/input.cfg";
    settings.api = backend.api;
    settings.adapter = backend.adapter;
    Run run;
    run.exitCode = levain::app::runApp(settings, recordInto(run.seen));
    return run;
}

levain::app::AppSettings withScript(std::string_view text)
{
    auto script = levain::platform::parseInputScript(text);
    if (!script)
    {
        throw std::runtime_error{script.error().message};
    }
    levain::app::AppSettings settings;
    settings.inputScript = std::move(*script);
    return settings;
}

int expect(bool holds, std::string_view what)
{
    if (!holds)
    {
        std::println(stderr, "échec : {}", what);
    }
    return holds ? 0 : 1;
}

/// Un QWERTY, puis un AZERTY. Sans `--steps`, le script mène la boucle.
constexpr std::string_view KeysScript = "1 key down W\n"
                                        "3 key up W\n"
                                        "5 key down W as z\n"
                                        "7 key up W as z\n";

/// Ce que le jeu et ImGui voient des 8 images de `KeysScript` : le jeu lit la position, quelle que
/// soit la lettre que la disposition y met ; ImGui lit la lettre : le W d'un QWERTY (images 1 et
/// 2), le Z d'un AZERTY (5 et 6), jamais l'autre.
std::vector<Seen> expectedKeys()
{
    constexpr Seen Idle{}, Qwerty{1.0f, true, false}, Azerty{1.0f, false, true};
    return {Idle, Qwerty, Qwerty, Idle, Idle, Azerty, Azerty, Idle};
}

int playKeys(const levain::tests::TestBackend& backend)
{
    const Run run = play(backend, withScript(KeysScript));
    return expect(run.exitCode == 0, "le script mène la boucle jusqu'à sa fin, code 0") +
           expect(run.seen == expectedKeys(),
                  "PlayerInput et ImGui voient les touches de leurs images");
}

/// Un fichier à lui, effacé à la fin : deux worktrees lancent ctest en même temps sur cette
/// machine.
struct ScriptFile
{
    std::filesystem::path path;

    explicit ScriptFile(std::string_view text)
        : path{std::filesystem::temp_directory_path() /
               ("levain-app-script-" + levain::assets::toString(levain::assets::generateAssetId()) +
                ".txt")}
    {
        std::ofstream{path} << text;
    }

    ~ScriptFile()
    {
        std::error_code ignored; // un destructeur ne lève pas : le fichier reste dans /tmp
        std::filesystem::remove(path, ignored);
    }

    ScriptFile(const ScriptFile&) = delete;
    ScriptFile& operator=(const ScriptFile&) = delete;
};

/// `--input-script f` de bout en bout : `runApp` lit le fichier. Sans lui (la lecture oubliée), la
/// boucle jouerait ses 8 images sans touche, et la comparaison le dirait ; `--steps` plutôt que la
/// fin du script, pour qu'elle le dise au lieu de tourner sans fin. Un script refusé arrête
/// `runApp` avant la fenêtre : le programme de test ne démarre pas, code 1.
int playFile(const levain::tests::TestBackend& backend)
{
    levain::app::AppSettings settings;
    settings.steps = 8;
    const ScriptFile keys{KeysScript};
    settings.inputScriptFile = keys.path;
    const Run run = play(backend, settings);

    const ScriptFile unknownKey{"0 key down W\n1 key down Wxyz\n"};
    settings.inputScriptFile = unknownKey.path;
    settings.steps = 2; // si le refus n'arrêtait pas, la boucle jouerait ces 2 images
    const Run refused = play(backend, settings);
    return expect(run.exitCode == 0 && run.seen == expectedKeys(),
                  "--input-script f : les touches du fichier atteignent PlayerInput et ImGui") +
           expect(refused.exitCode == 1 && refused.seen.empty(),
                  "--input-script avec une touche inconnue : code 1, sans démarrer");
}

/// Le contre-test de la règle n°7 : `--steps` coupe la boucle avant la fin du script, et le
/// programme échoue au lieu de passer sans avoir rejoué ce qui était écrit.
int playCutShort(const levain::tests::TestBackend& backend)
{
    levain::app::AppSettings settings = withScript(KeysScript);
    settings.steps = 4;
    const Run run = play(backend, settings);
    return expect(run.exitCode != 0, "--steps avant la fin du script : le programme échoue") +
           expect(run.seen.size() == 4, "les 4 images demandées sont jouées");
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::span arguments{argv, static_cast<std::size_t>(argc)};
        const auto backend =
            levain::tests::testBackendNamed(arguments.size() == 2 ? arguments[1] : "vulkan");
        if (arguments.size() > 2 || !backend)
        {
            std::println(stderr, "usage : levain_app_script [vulkan|d3d12|d3d12-warp]");
            return 2;
        }
        const int failures = playKeys(*backend) + playFile(*backend) + playCutShort(*backend);
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
}
