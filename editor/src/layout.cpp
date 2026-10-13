#include "levain/editor/layout.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <utility>

#include <imgui.h>
// DockBuilder et les réglages de fenêtre par leur identifiant : l'API interne d'ImGui, comme
// `buildLayout` (engine/app/src/panels.cpp) ; vcpkg fige la version.
#include <imgui_internal.h>

#include "levain/core/log.hpp"
#include "levain/editor/camera.hpp"
#include "levain/editor/console.hpp"
#include "levain/editor/editor.hpp"
#include "levain/editor/hierarchy.hpp"
#include "levain/editor/inspector.hpp"
#include "levain/editor/view.hpp"
#include "levain/platform/paths.hpp"
#include "levain/ui/tr.hpp"

namespace levain::editor
{

namespace
{

/// Un nœud existe : l'identifiant d'une fenêtre relue peut viser un nœud que la relecture n'a pas
/// bâti, ou que l'utilisateur a vidé.
bool nodeExists(ImGuiID id)
{
    return id != 0 && ImGui::DockBuilderGetNode(id) != nullptr;
}

/// Le nœud où la disposition relue a rangé la première de ces fenêtres qui en a un. Les réglages
/// d'une fenêtre se retrouvent par le hachage de son titre (`ImHashStr` s'arrête aux « ### » : la
/// clé du catalogue suffit, quelle que soit la langue du libellé).
ImGuiID savedNodeOf(std::span<const char* const> windows)
{
    for (const char* french : windows)
    {
        const std::string title = ui::labelOf(french);
        const ImGuiWindowSettings* settings =
            ImGui::FindWindowSettingsByID(ImHashStr(title.c_str()));
        if (settings != nullptr && nodeExists(settings->DockId))
        {
            return settings->DockId;
        }
    }
    return 0;
}

/// Les fenêtres de la Console, d'Image, de Passes et de Scène partagent le nœud du bas par défaut.
constexpr std::array<const char*, 4> BottomWindows{ConsoleWindow, "Image", "Passes", "Scène"};

/// La disposition d'ImGui est celle d'un espace d'ancrage qui existe.
bool hasDockspace()
{
    return ImGui::DockBuilderGetNode(app::mainDockspaceId()) != nullptr;
}

/// La feuille que la disposition relue a marquée « centrale » (la Vue y va), en descendant l'arbre.
/// `DockBuilderGetCentralNode` ne sert pas ici : le pointeur du nœud racine n'est posé qu'à la
/// première image (`DockNodeUpdate`), et la relecture se fait avant. Le drapeau, lui, est dans le
/// texte (`CentralNode=1`).
ImGuiID centralLeafIn(const ImGuiDockNode* node)
{
    if (node == nullptr)
    {
        return 0;
    }
    if (node->IsLeafNode())
    {
        return node->IsCentralNode() ? node->ID : 0;
    }
    for (const ImGuiDockNode* child : node->ChildNodes)
    {
        if (const ImGuiID found = centralLeafIn(child); found != 0)
        {
            return found;
        }
    }
    return 0;
}

} // namespace

app::DockNodes dockNodesOfSavedLayout()
{
    constexpr std::array<const char*, 1> Hierarchy{HierarchyWindow};
    constexpr std::array<const char*, 1> Inspector{InspectorWindow};
    constexpr std::array<const char*, 1> View{ViewWindow};
    app::DockNodes nodes{.left = savedNodeOf(Hierarchy),
                         .center = savedNodeOf(View),
                         .inspector = savedNodeOf(Inspector),
                         .bottom = savedNodeOf(BottomWindows)};
    if (nodes.center == 0)
    {
        // La Vue jamais dessinée (fermée à la dernière session) : le nœud central de la
        // disposition relue, celui où elle ira à sa réouverture.
        nodes.center = centralLeafIn(ImGui::DockBuilderGetNode(app::mainDockspaceId()));
    }
    return nodes;
}

Preferences preferencesOf(const app::App& app, const Editor& editor)
{
    Preferences preferences;
    preferences.flySpeed = editor.camera.flySpeed;
    // En jeu, `App::realTime` est celui du jeu : le choix de l'Édition est gardé à part.
    preferences.realTime = editor.mode.current == Mode::Edit ? app.realTime : editor.realTimeEdit;
    preferences.windows = {.hierarchy = editor.windows.hierarchy,
                           .inspector = editor.windows.inspector,
                           .console = editor.windows.console,
                           .view = editor.view.open,
                           .image = app.ui.windows.image,
                           .passes = app.ui.windows.passes,
                           .scene = app.ui.windows.scene};
    preferences.shortcuts = editor.shortcutLines;
    // Sans espace d'ancrage, pas de disposition : celle d'un fichier relu l'a créé dès la lecture
    // (`applyPreferences`), il n'y a donc rien d'autre à garder que « la défaut se construira ».
    if (hasDockspace())
    {
        std::size_t size = 0;
        const char* text = ImGui::SaveIniSettingsToMemory(&size);
        preferences.layout.assign(text, size);
    }
    return preferences;
}

void applyPreferences(app::App& app, Editor& editor, const Preferences& preferences)
{
    editor.windows = {.hierarchy = preferences.windows.hierarchy,
                      .inspector = preferences.windows.inspector,
                      .console = preferences.windows.console};
    editor.view.open = preferences.windows.view;
    app.ui.windows = {.image = preferences.windows.image,
                      .passes = preferences.windows.passes,
                      .scene = preferences.windows.scene};
    editor.camera.flySpeed = clampFlySpeed(preferences.flySpeed);
    app.realTime = preferences.realTime;
    editor.realTimeEdit = preferences.realTime;
    editor.shortcutLines = preferences.shortcuts;
    if (preferences.layout.empty())
    {
        return;
    }
    ImGui::LoadIniSettingsFromMemory(preferences.layout.c_str(), preferences.layout.size());
    if (!hasDockspace())
    {
        // ImGui accepte tout texte sans rien dire : une disposition sans espace d'ancrage est la
        // seule faute qu'on y voit. Ce qu'elle a posé sur les fenêtres est retiré.
        ImGui::ClearIniSettings();
        core::log("editor", core::LogLevel::Error,
                  "disposition des préférences illisible (aucun espace d'ancrage) : la disposition "
                  "par défaut est construite");
        return;
    }
    app.ui.dock = dockNodesOfSavedLayout();
}

void startPreferences(app::App& app, Editor& editor,
                      const std::optional<std::filesystem::path>& testDirectory)
{
    const auto file = preferencesFileFor(
        app::isScriptedRun(app.settings), app.settings.testInteractive, testDirectory,
        []() { return platform::prefPath(PreferencesOrganization, PreferencesApplication); });
    if (!file)
    {
        core::log("editor", core::LogLevel::Error,
                  "préférences : dossier de l'utilisateur introuvable, l'éditeur n'en gardera pas "
                  "({})",
                  file.error().message);
        return;
    }
    if (!file.value())
    {
        return; // exécution scriptée : ni lues ni écrites
    }
    const std::filesystem::path path = file.value().value();
    editor.preferencesFile = path;
    const auto loaded = loadPreferences(path);
    if (!loaded && isFromNewerVersion(loaded.error()))
    {
        // Écrit par une version plus récente : le mettre de côté puis écrire une v1 à la
        // fermeture le rétrograderait. On le laisse tel quel et la session n'enregistre pas.
        editor.preferencesFile.reset();
        core::log("editor", core::LogLevel::Error,
                  "{} : la disposition par défaut est construite ; le fichier est laissé tel "
                  "quel, pas d'enregistrement cette session",
                  loaded.error().message);
        return;
    }
    if (!loaded)
    {
        // `rename` remplace la cible, sous Linux comme sous Windows : un second fichier refusé
        // prend la place du premier `.bad` (un seul est gardé, le dernier).
        std::filesystem::path aside = path;
        aside += ".bad";
        std::error_code moved;
        std::filesystem::rename(path, aside, moved);
        if (moved)
        {
            // Le fichier refusé est encore là : l'enregistrer l'écraserait, et avec lui ce que
            // quelqu'un y a écrit à la main. Pas d'enregistrement cette session.
            editor.preferencesFile.reset();
            core::log("editor", core::LogLevel::Error,
                      "{} : la disposition par défaut est construite ; le fichier n'a pas pu être "
                      "mis de côté ({}), pas d'enregistrement cette session",
                      loaded.error().message, moved.message());
            return;
        }
        core::log("editor", core::LogLevel::Error,
                  "{} : la disposition par défaut est construite ; le fichier est gardé en {}",
                  loaded.error().message, aside.generic_string());
        return;
    }
    if (!loaded.value())
    {
        core::log("editor", core::LogLevel::Info, "préférences : première session ({})",
                  path.generic_string());
        return;
    }
    applyPreferences(app, editor, loaded.value().value());
    core::log("editor", core::LogLevel::Info, "préférences relues : {}", path.generic_string());
}

void savePreferencesNow(const app::App& app, const Editor& editor)
{
    if (!editor.preferencesFile)
    {
        return;
    }
    const auto saved = savePreferences(*editor.preferencesFile, preferencesOf(app, editor));
    if (!saved)
    {
        core::log("editor", core::LogLevel::Error, "{}", saved.error().message);
    }
}

void resetLayout(app::App& app, Editor& editor)
{
    const app::DockNodes nodes = app::resetEditorLayout(app);
    // Les fenêtres de l'éditeur, qu'`app` ne connaît pas : `DockBuilderDockWindow` ancre aussi
    // celles qui existent déjà (SetWindowDock).
    const std::array<std::pair<const char*, ImGuiID>, 4> placed{{{HierarchyWindow, nodes.left},
                                                                 {InspectorWindow, nodes.inspector},
                                                                 {ViewWindow, nodes.center},
                                                                 {ConsoleWindow, nodes.bottom}}};
    for (const auto& [french, node] : placed)
    {
        ImGui::DockBuilderDockWindow(ui::labelOf(french).c_str(), node);
    }
    ImGui::DockBuilderFinish(app::mainDockspaceId());
    editor.windows = {};
    editor.view.open = true;
}

} // namespace levain::editor
