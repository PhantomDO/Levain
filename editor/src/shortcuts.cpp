#include "levain/editor/shortcuts.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <string>
#include <utility>

#include "levain/ui/input.hpp"
#include "levain/ui/tr.hpp"

namespace levain::editor
{

namespace
{

// Les scancodes d'USB HID des défauts, plus F1 et F12 qui bornent les touches de fonction que
// `displayNameOf` nomme (F1 à F12). Ils sont les mêmes partout, et le test des défauts les relit
// par la disposition du banc d'essai.
constexpr std::uint16_t ScanA = 4, ScanD = 7, ScanE = 8, ScanF = 9, ScanG = 10, ScanQ = 20,
                        ScanR = 21, ScanS = 22, ScanW = 26, ScanEscape = 41, ScanSpace = 44,
                        ScanF1 = 58, ScanF12 = 69;
// Left Ctrl à Right GUI : les modificateurs seuls, que ni Ctrl ni Maj ne lient (USB HID 0xE0-0xE7).
constexpr std::uint32_t FirstModifierScancode = 224, LastModifierScancode = 231;
constexpr std::uint32_t ScancodeMask = 1U
                                       << 30U; // SDLK_SCANCODE_MASK : une touche qui ne tape rien

constexpr platform::KeyModifiers Ctrl{.ctrl = true};
constexpr platform::KeyModifiers Shift{.shift = true};
constexpr platform::KeyModifiers Alt{.alt = true};
constexpr platform::KeyModifiers CtrlShift{.ctrl = true, .shift = true};

Binding bind(Action action, Context context, Chord first, Chord second = {})
{
    return {.action = action, .context = context, .chords = {first, second}};
}

char upperOf(std::uint32_t letter)
{
    return static_cast<char>(letter >= 'a' && letter <= 'z' ? letter - 'a' + 'A' : letter);
}

/// La lettre en minuscule, sans la locale de `std::tolower` (un nom d'accord est le même partout).
std::uint32_t lowerOf(std::uint32_t letter)
{
    return letter >= 'A' && letter <= 'Z' ? letter - 'A' + 'a' : letter;
}

/// Les touches qu'une saisie peut lier à une action de position et qui ont un nom à elles : sans
/// lui, la fenêtre dirait « pos:82 » pour la flèche du haut. Clé du catalogue, ou rien.
const char* namedKeyOf(std::uint32_t keycode)
{
    constexpr std::uint32_t ScanRight = 79, ScanLeft = 80, ScanDown = 81, ScanUp = 82;
    switch (keycode)
    {
    case '\t':
        return "Tab";
    case '\r':
        return "Entrée";
    case '\b':
        return "Retour arrière";
    case ScancodeMask | ScanRight:
        return "Droite";
    case ScancodeMask | ScanLeft:
        return "Gauche";
    case ScancodeMask | ScanDown:
        return "Bas";
    case ScancodeMask | ScanUp:
        return "Haut";
    default:
        return nullptr;
    }
}

/// Une position que le moteur se réserve : un modificateur seul ne se lie pas, et F1 sans
/// modificateur ouvre les panneaux (`togglesPanels`).
bool reservedPosition(std::uint32_t scancode, const platform::KeyModifiers& modifiers)
{
    const bool modifierOnly = scancode >= FirstModifierScancode && scancode <= LastModifierScancode;
    const bool panelsKey = scancode == ScanF1 && modifiers == platform::KeyModifiers{};
    return modifierOnly || panelsKey;
}

std::string modifiersPrefix(const platform::KeyModifiers& modifiers, std::string_view ctrl,
                            std::string_view shift, std::string_view alt, std::string_view super)
{
    std::string prefix;
    prefix += modifiers.ctrl ? std::string{ctrl} + "+" : "";
    prefix += modifiers.shift ? std::string{shift} + "+" : "";
    prefix += modifiers.alt ? std::string{alt} + "+" : "";
    prefix += modifiers.super ? std::string{super} + "+" : "";
    return prefix;
}

} // namespace

const char* nameOf(Action action)
{
    switch (action)
    {
    case Action::MoveForward:
        return "Avancer";
    case Action::MoveBackward:
        return "Reculer";
    case Action::MoveLeft:
        return "Aller à gauche";
    case Action::MoveRight:
        return "Aller à droite";
    case Action::MoveUp:
        return "Monter";
    case Action::MoveDown:
        return "Descendre";
    case Action::FrameSelection:
        return "Cadrer la sélection";
    case Action::GameView:
        return "Vue de jeu";
    case Action::RealTime:
        return "Temps réel";
    case Action::ToolMove:
        return "Outil déplacer";
    case Action::ToolRotate:
        return "Outil tourner";
    case Action::ToolScale:
        return "Outil mettre à l'échelle";
    case Action::NextTool:
        return "Outil suivant";
    case Action::Save:
        return "Enregistrer";
    case Action::Undo:
        return "Annuler";
    case Action::Redo:
        return "Rétablir";
    case Action::Play:
        return "Jouer";
    case Action::Simulate:
        return "Simuler";
    case Action::Stop:
        return "Arrêter";
    case Action::GiveMouseBack:
        return "Rendre la souris";
    case Action::Count:
        break;
    }
    return "";
}

ShortcutTable defaultShortcuts()
{
    using enum Action;
    using enum Context;
    return {
        .bindings = {
            // Le vol : W, S, A, D et E, Q à la place des touches d'un QWERTY (Z, S, Q, D et E, A
            // sur un AZERTY), comme Unreal et Unity.
            bind(MoveForward, Flight, positionChord(ScanW)),
            bind(MoveBackward, Flight, positionChord(ScanS)),
            bind(MoveLeft, Flight, positionChord(ScanA)),
            bind(MoveRight, Flight, positionChord(ScanD)),
            bind(MoveUp, Flight, positionChord(ScanE)),
            bind(MoveDown, Flight, positionChord(ScanQ)),
            // La Vue : F cadre, G bascule la vue de jeu, W E R les outils (M7.4), Espace le
            // suivant. Temps réel est un accord à Ctrl, donc par lettre.
            bind(FrameSelection, View, positionChord(ScanF)),
            bind(GameView, View, positionChord(ScanG)),
            bind(RealTime, View, letterChord('r', Ctrl)),
            bind(ToolMove, View, positionChord(ScanW)),
            bind(ToolRotate, View, positionChord(ScanE)),
            bind(ToolScale, View, positionChord(ScanR)),
            bind(NextTool, View, positionChord(ScanSpace)),
            // Le global : par lettre, comme les menus de tous les logiciels.
            bind(Save, Global, letterChord('s', Ctrl)),
            bind(Undo, Global, letterChord('z', Ctrl)),
            bind(Redo, Global, letterChord('y', Ctrl), letterChord('z', CtrlShift)),
            bind(Play, Global, letterChord('p', Alt)),
            bind(Simulate, Global, letterChord('s', Alt)),
            // Le jeu : Échap arrête, Maj+F1 rend la souris (F1 seule ouvre les panneaux).
            bind(Stop, Game, positionChord(ScanEscape)),
            bind(GiveMouseBack, Game, positionChord(ScanF1, Shift)),
        }};
}

const Binding* bindingOf(const ShortcutTable& table, Action action)
{
    const auto found = std::ranges::find(table.bindings, action, &Binding::action);
    return found == table.bindings.end() ? nullptr : &*found;
}

std::string tokenOf(const Chord& chord)
{
    if (!isBound(chord))
    {
        return {};
    }
    std::string token = modifiersPrefix(chord.modifiers, "Ctrl", "Shift", "Alt", "Super");
    token += chord.kind == ChordKind::Position ? "pos:" + std::to_string(chord.key)
                                               : std::string{upperOf(chord.key)};
    return token;
}

std::optional<Chord> chordFromToken(std::string_view token)
{
    const std::string_view whole = token;
    std::optional<Chord> found;
    Chord chord;
    for (const auto& [prefix, flag] :
         {std::pair{std::string_view{"Ctrl+"}, &platform::KeyModifiers::ctrl},
          std::pair{std::string_view{"Shift+"}, &platform::KeyModifiers::shift},
          std::pair{std::string_view{"Alt+"}, &platform::KeyModifiers::alt},
          std::pair{std::string_view{"Super+"}, &platform::KeyModifiers::super}})
    {
        if (token.starts_with(prefix))
        {
            chord.modifiers.*flag = true;
            token.remove_prefix(prefix.size());
        }
    }
    if (token.starts_with("pos:"))
    {
        token.remove_prefix(4);
        std::uint32_t scancode = 0;
        const auto [end, error] =
            std::from_chars(token.data(), token.data() + token.size(), scancode);
        const bool numeric = error == std::errc{} && end == token.data() + token.size();
        const bool bindable = numeric && scancode > 0 && scancode < platform::KeyCodeCount &&
                              !reservedPosition(scancode, chord.modifiers);
        found = bindable ? std::optional{positionChord(static_cast<std::uint16_t>(scancode),
                                                       chord.modifiers)}
                         : std::nullopt;
    }
    else
    {
        const bool letter = token.size() == 1 && ((token[0] >= 'A' && token[0] <= 'Z') ||
                                                  (token[0] >= '0' && token[0] <= '9'));
        found =
            letter
                ? std::optional{letterChord(static_cast<char>(lowerOf(token[0])), chord.modifiers)}
                : std::nullopt;
    }
    // Un seul nom par accord : « pos:026 » se relit, mais ne se réécrirait pas pareil.
    return found && tokenOf(*found) == whole ? found : std::nullopt;
}

std::string displayNameOf(const Chord& chord, const platform::KeyAtPosition& keyAtPosition)
{
    std::string name = modifiersPrefix(chord.modifiers, "Ctrl", ui::tr("Maj"), "Alt", "Super");
    if (chord.kind == ChordKind::Letter)
    {
        return name + upperOf(chord.key);
    }
    const std::uint32_t keycode = keyAtPosition(static_cast<std::uint16_t>(chord.key));
    if (keycode == ' ')
    {
        return name + ui::tr("Espace");
    }
    if (keycode == 0x1B)
    {
        return name + ui::tr("Échap");
    }
    if (const char* named = namedKeyOf(keycode))
    {
        return name + ui::tr(named);
    }
    if (keycode > ' ' && keycode < 0x7F)
    {
        return name + upperOf(keycode);
    }
    const std::uint32_t scancode = keycode & ~ScancodeMask;
    if ((keycode & ScancodeMask) != 0 && scancode >= ScanF1 && scancode <= ScanF12)
    {
        return name + "F" + std::to_string(scancode - ScanF1 + 1);
    }
    return name + tokenOf(positionChord(static_cast<std::uint16_t>(chord.key))); // « pos:N »
}

bool chordMatches(const Chord& chord, const KeyPress& press)
{
    if (!isBound(chord) || !exactModifiers(chord.modifiers, press.modifiers))
    {
        return false;
    }
    if (chord.kind == ChordKind::Position)
    {
        return chord.key == press.scancode;
    }
    return chord.key == lowerOf(press.keycode);
}

std::optional<Action> actionFor(const ShortcutTable& table, const KeyPress& press,
                                const ActiveContexts& active)
{
    // Le premier contexte qui reconnaît l'appui l'emporte : l'ordre de la liste est la précédence.
    // Sous la route jeu, le jeu seul : Ctrl+S ne part pas en jouant.
    // Un champ de texte actif ne laisse rien aux contextes de l'éditeur (`textInput`).
    const auto contexts = active.game ? std::vector{Context::Game}
                          : active.textInput
                              ? std::vector<Context>{}
                              : std::vector{Context::Flight, Context::View, Context::Global};
    for (const Context context : contexts)
    {
        // En vol, la Vue se tait : ni R ni Espace ne changent d'outil sous le clic droit tenu.
        const bool inactive = (context == Context::Flight && !active.flight) ||
                              (context == Context::View && (!active.view || active.flight));
        if (inactive)
        {
            continue;
        }
        for (const Binding& binding : table.bindings)
        {
            if (binding.context == context &&
                std::ranges::any_of(binding.chords,
                                    [&](const Chord& chord) { return chordMatches(chord, press); }))
            {
                return binding.action;
            }
        }
    }
    return std::nullopt;
}

bool positionHeld(const Binding& binding, const std::bitset<platform::KeyCodeCount>& keys)
{
    return std::ranges::any_of(binding.chords,
                               [&keys](const Chord& chord)
                               {
                                   return chord.kind == ChordKind::Position && isBound(chord) &&
                                          chord.key < keys.size() && keys.test(chord.key);
                               });
}

std::optional<ImGuiKeyChord> imguiChordOf(const Chord& chord)
{
    if (chord.kind != ChordKind::Letter || !isBound(chord))
    {
        return std::nullopt;
    }
    const ImGuiKey key = ui::imguiKeyOf(chord.key, 0);
    if (key == ImGuiKey_None)
    {
        return std::nullopt;
    }
    unsigned bits = static_cast<unsigned>(key);
    bits |= chord.modifiers.ctrl ? static_cast<unsigned>(ImGuiMod_Ctrl) : 0U;
    bits |= chord.modifiers.shift ? static_cast<unsigned>(ImGuiMod_Shift) : 0U;
    bits |= chord.modifiers.alt ? static_cast<unsigned>(ImGuiMod_Alt) : 0U;
    bits |= chord.modifiers.super ? static_cast<unsigned>(ImGuiMod_Super) : 0U;
    return static_cast<ImGuiKeyChord>(bits);
}

bool letterChordPressed(const Binding& binding)
{
    return std::ranges::any_of(binding.chords,
                               [](const Chord& chord)
                               {
                                   const auto imgui = imguiChordOf(chord);
                                   return imgui &&
                                          ImGui::Shortcut(*imgui, ImGuiInputFlags_RouteGlobal);
                               });
}

} // namespace levain::editor
