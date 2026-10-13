#pragma once

// Les préférences de l'éditeur (ADR-0036, décision 10) : ce que l'éditeur garde d'une session à
// l'autre, dans un fichier texte versionné que ce module lit et écrit **sans ImGui** (les tests le
// parcourent). La disposition des fenêtres y est le texte d'ImGui (`SaveIniSettingsToMemory`), à
// la fin, tel quel ; le reste est à nous : la vitesse de la caméra, le choix de « Temps réel », les
// fenêtres ouvertes, et (morceau 16) la table des raccourcis. Le glue d'ImGui est `layout.hpp`.
// Équivalents : les `EditorPerProjectUserSettings.ini` et `EditorLayout` d'Unreal, le dossier
// `Library/` et les `.dwlt` de disposition d'Unity, `editor_layouts.cfg` et
// `editor_settings-4.tres` de Godot.

#include <filesystem>
#include <functional>
#include <optional>

#include "levain/core/error.hpp"

namespace levain::editor
{

/// Le nom du fichier, dans le dossier de l'utilisateur (`platform::prefPath`).
inline constexpr const char* PreferencesFileName = "preferences.cfg";

/// **Où vont les préférences de cette exécution**, ou rien. Dans l'ordre :
/// - **une exécution scriptée n'en a pas** (`scriptedRun`, `isScriptedRun`) : les réglages d'un
///   développeur fausseraient les clics des tests, et une session changerait la suivante
///   (ADR-0032) ;
/// - `testDirectory` (EditorOptions, tests seuls) prend la place du dossier de l'utilisateur ;
/// - **`testRun` sans dossier injecté n'en a pas non plus** (`AppSettings::testInteractive` : un
///   test qui lève la règle scriptée pour éprouver ce qu'un humain déclenche). Sans cette ligne,
///   un test qui oublie d'injecter un dossier lisait et écrivait celui de qui le lance ;
/// - sinon, le dossier de l'utilisateur.
/// `userFolder` n'est appelé qu'au dernier cas : il crée le dossier.
[[nodiscard]] core::Result<std::optional<std::filesystem::path>>
preferencesFileFor(bool scriptedRun, bool testRun,
                   const std::optional<std::filesystem::path>& testDirectory,
                   const std::function<core::Result<std::filesystem::path>()>& userFolder);

} // namespace levain::editor
