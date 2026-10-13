#pragma once

// Les préférences de l'éditeur (ADR-0036, décision 10) : ce que l'éditeur garde d'une session à
// l'autre, dans un fichier texte versionné que ce module lit et écrit **sans ImGui** (les tests le
// parcourent). La disposition des fenêtres y est le texte d'ImGui (`SaveIniSettingsToMemory`), à
// la fin, tel quel ; le reste est à nous : la vitesse de la caméra, le choix de « Temps réel », les
// fenêtres ouvertes, et (morceau 16) la table des raccourcis. Le glue d'ImGui est `layout.hpp`.
// Équivalents : les `EditorPerProjectUserSettings.ini` et `EditorLayout` d'Unreal, le dossier
// `Library/` et les `.dwlt` de disposition d'Unity, `editor_layouts.cfg` et
// `editor_settings-4.tres` de Godot.
//
// Le format, des lignes :
//   levain-editor 1                  <- le nom et la version, rien d'autre sur la ligne
//   [editeur]                        <- vitesse_camera = 8, temps_reel = true
//   [fenetres]                       <- hierarchie, inspecteur, console, vue, image, passes, scene
//   [raccourcis]                     <- action = accord, accord  (noms stables de `tokenOf`)
//   [disposition]                    <- tout ce qui suit, tel quel : le texte d'ImGui
// Les lignes vides et celles qui commencent par « # » sont ignorées avant `[disposition]`.
// **Le fichier est refusé en entier à la première faute** (règle n°7) : version inconnue, section
// inconnue ou en double, clé inconnue ou en double, valeur illisible ou hors bornes, **fichier
// tronqué** (`[disposition]` est la marque de fin : sans elle, la disposition aurait été perdue
// sans un mot). L'éditeur le dit dans sa console et repart de la disposition par défaut, plutôt que
// d'en lire la moitié.

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "levain/core/error.hpp"

namespace levain::editor
{

/// Le nom du fichier, dans le dossier de l'utilisateur (`platform::prefPath`).
inline constexpr const char* PreferencesFileName = "preferences.cfg";
/// La version que ce code écrit et lit. Un fichier d'une autre version est refusé, pas deviné.
inline constexpr int PreferencesVersion = 1;

/// Les fenêtres ouvertes ou fermées, dans l'ordre du menu « Fenêtre ».
struct OpenWindows
{
    bool hierarchy = true;
    bool inspector = true;
    bool console = true;
    bool view = true;
    bool image = true;
    bool passes = true;
    bool scene = true;

    bool operator==(const OpenWindows&) const = default;
};

/// Une ligne de `[raccourcis]` : le nom de l'action et ses accords (au plus deux), chacun un nom
/// stable de `tokenOf`. Le morceau 15 ne l'écrit pas (absence = les défauts) mais la relit et la
/// garde ; le morceau 16 la remplit.
struct ShortcutLine
{
    std::string action;
    std::vector<std::string> chords;

    bool operator==(const ShortcutLine&) const = default;
};

/// Tout ce que le fichier tient.
struct Preferences
{
    float flySpeed = 8.0f; ///< `DefaultFlySpeed`, bornée par `clampFlySpeed`.
    bool realTime = true;
    OpenWindows windows;
    std::vector<ShortcutLine> shortcuts;
    /// Le texte de disposition d'ImGui. Vide : pas de disposition gardée, la défaut se construit.
    std::string layout;

    bool operator==(const Preferences&) const = default;
};

/// Le texte du fichier. Déterministe : relu puis réécrit, il ne change pas.
[[nodiscard]] std::string formatPreferences(const Preferences& preferences);

/// Lit le texte d'un fichier. La première faute le refuse, avec son numéro de ligne.
[[nodiscard]] core::Result<Preferences> parsePreferences(std::string_view text);

/// Lit le fichier : rien s'il n'existe pas (une première session), une erreur qui nomme le fichier
/// s'il est illisible, refusé, ou si le disque ne dit pas s'il existe (droits : ce n'est pas une
/// première session, c'est un fichier qu'on ne sait pas lire).
[[nodiscard]] core::Result<std::optional<Preferences>>
loadPreferences(const std::filesystem::path& file);

/// **Le fichier voisin d'un écrivain** : `<fichier>.<writer>.tmp`. Deux éditeurs lancés ensemble
/// ne partagent pas un seul `.tmp` (l'un tronquait celui de l'autre, et le renommage posait un
/// texte hybride, mesuré : 610 fichiers sur 2000 tours). `writer` est un identifiant propre à
/// chaque écriture (`savePreferences` tire un `AssetId`).
[[nodiscard]] std::filesystem::path temporaryFileFor(const std::filesystem::path& file,
                                                     std::string_view writer);

/// **Ce fichier vient d'une version plus récente** (`loadPreferences` rend alors `Unsupported`) :
/// on ne le met pas de côté et on ne l'écrase pas, une branche plus ancienne le rétrograderait.
[[nodiscard]] bool isFromNewerVersion(const core::Error& error);

/// Écrit le fichier, dossier compris, par un fichier voisin renommé ensuite : un plantage du
/// processus au milieu de l'écriture laisse l'ancien fichier, jamais un tronqué. C'est **à l'abri
/// d'un plantage, pas d'une coupure de courant** (pas de `fsync` avant le renommage). Un échec
/// retire le fichier voisin.
[[nodiscard]] core::Result<void> savePreferences(const std::filesystem::path& file,
                                                 const Preferences& preferences);

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
