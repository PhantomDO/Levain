#include "levain/platform/input_script.hpp"

#include <algorithm>
#include <bitset>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <ranges>
#include <span>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "levain/core/file.hpp"
#include "levain/platform/input.hpp"

namespace levain::platform
{

// Les scancodes que `qwertyKeycodeOf` suppose, ceux d'USB HID : une version de SDL qui les
// changerait casserait le build ici, pas un script en silence.
static_assert(SDL_SCANCODE_A == 4 && SDL_SCANCODE_Z == 29 && SDL_SCANCODE_1 == 30 &&
              SDL_SCANCODE_0 == 39 && SDL_SCANCODE_RETURN == 40 && SDL_SCANCODE_ESCAPE == 41 &&
              SDL_SCANCODE_BACKSPACE == 42 && SDL_SCANCODE_TAB == 43 && SDL_SCANCODE_SPACE == 44 &&
              SDL_SCANCODE_DELETE == 76 && SDL_SCANCODE_LCTRL == 224 && SDL_SCANCODE_RGUI == 231);

namespace
{

using Words = std::vector<std::string_view>;

/// Un refus qui nomme la ligne et le champ fautif.
std::unexpected<core::Error> refusal(int line, std::string_view field, std::string_view what)
{
    return core::makeError(core::ErrorCode::InvalidData,
                           std::format("ligne {}, {} : {}", line, field, what));
}

/// Les mots d'une ligne, séparés par des espaces ou des tabulations, sans son commentaire. Le
/// retour chariot d'une fin de ligne de Windows est un séparateur de plus : il ne reste pas collé
/// au dernier mot.
Words wordsOf(std::string_view line)
{
    constexpr std::string_view Blanks = " \t\r";
    line = line.substr(0, line.find('#'));
    Words words;
    for (auto start = line.find_first_not_of(Blanks); start != std::string_view::npos;)
    {
        const auto end = std::min(line.find_first_of(Blanks, start), line.size());
        words.push_back(line.substr(start, end - start));
        start = line.find_first_not_of(Blanks, end);
    }
    return words;
}

/// Un numéro d'image : un entier de 0 à `MaxInputScriptFrame`, et rien d'autre dans le texte
/// (« 12x » est refusé).
std::optional<int> frameOf(std::string_view text)
{
    int frame = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), frame);
    return error == std::errc{} && end == text.data() + text.size() && frame >= 0 &&
                   frame <= MaxInputScriptFrame
               ? std::optional{frame}
               : std::nullopt;
}

/// `down` ou `up`, le troisième mot d'une touche ou d'un bouton.
std::optional<bool> directionOf(const Words& words)
{
    if (words.size() > 2 && (words[2] == "down" || words[2] == "up"))
    {
        return words[2] == "down";
    }
    return std::nullopt;
}

/// Un nom de touche peut avoir des espaces (« Left Ctrl »).
std::string joined(std::span<const std::string_view> words)
{
    std::string text;
    for (const std::string_view word : words)
    {
        text += (text.empty() ? "" : " ") + std::string{word};
    }
    return text;
}

/// Le keycode (`SDLK_`) qu'un clavier QWERTY américain donne à ce scancode : la lettre, le chiffre,
/// ou, pour une touche qui ne tape rien (F1, les flèches, Ctrl), son scancode avec le bit 30 posé.
/// Une table à nous, et non `SDL_GetKeyFromScancode`, qui lit la disposition de la machine : le
/// même script doit donner les mêmes touches à ImGui sur toutes. La ponctuation n'a pas de lettre
/// ici : ImGui la lit par position, et `as` en donne une.
std::uint32_t qwertyKeycodeOf(std::uint16_t scancode)
{
    constexpr std::string_view Typed = "1234567890\r\x1b\b\t "; // les scancodes 30 à 44
    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z)
    {
        return 'a' + static_cast<std::uint32_t>(scancode - SDL_SCANCODE_A);
    }
    if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_SPACE)
    {
        return static_cast<std::uint32_t>(
            Typed[static_cast<std::size_t>(scancode - SDL_SCANCODE_1)]);
    }
    return scancode == SDL_SCANCODE_DELETE ? 0x7FU : scancode | (1U << 30U); // SDLK_SCANCODE_MASK
}

/// Le keycode que tape une disposition, après `as` : un caractère (« z »), ou une touche par son
/// nom (« F1 »), alors celle d'un QWERTY.
std::optional<std::uint32_t> keycodeNamed(const std::string& name)
{
    if (name.size() == 1 && name[0] > ' ' && name[0] < 0x7F)
    {
        return static_cast<std::uint32_t>(std::tolower(static_cast<unsigned char>(name[0])));
    }
    const auto scancode = keyCodeFromName(name);
    return scancode ? std::optional{qwertyKeycodeOf(*scancode)} : std::nullopt;
}

/// Les touches de modification tenues, un bit par scancode de 224 (Ctrl, Maj, Alt, Windows, à
/// gauche puis à droite). SDL joint à chaque touche celles qui sont tenues, et ImGui n'a pas
/// Ctrl+Z sans : un script écrit donc les quatre lignes d'un accord, comme un clavier les envoie.
using HeldModifiers = std::bitset<8>;

KeyModifiers modifiersOf(const HeldModifiers& held)
{
    return {.ctrl = held[0] || held[4],
            .shift = held[1] || held[5],
            .alt = held[2] || held[6],
            .super = held[3] || held[7]};
}

core::Result<void> addKey(Events& events, int line, const Words& words, HeldModifiers& held)
{
    const auto down = directionOf(words);
    if (!down)
    {
        return refusal(line, "sens", "key down|up <touche> [as <lettre>] est attendu");
    }
    const std::span<const std::string_view> rest =
        std::span{words}.subspan(std::min<std::size_t>(3, words.size()));
    const auto nameLength = static_cast<std::size_t>(std::ranges::find(rest, "as") - rest.begin());
    const std::string keyName = joined(rest.first(nameLength));
    const auto scancode = keyCodeFromName(keyName);
    if (!scancode)
    {
        return refusal(line, "touche", std::format("« {} » : nom inconnu de SDL", keyName));
    }
    const bool hasLetter = nameLength < rest.size();
    const std::string letter = hasLetter ? joined(rest.subspan(nameLength + 1)) : "";
    const auto keycode =
        hasLetter ? keycodeNamed(letter) : std::optional{qwertyKeycodeOf(*scancode)};
    if (!keycode)
    {
        return refusal(line, "lettre",
                       std::format("« {} » : un caractère ou un nom de touche", letter));
    }
    if (*scancode >= SDL_SCANCODE_LCTRL && *scancode <= SDL_SCANCODE_RGUI)
    {
        held.set(static_cast<std::size_t>(*scancode - SDL_SCANCODE_LCTRL), *down);
    }
    // Comme SDL (`appendInputEvent`, `appendUiEvent`) : le jeu reçoit la position, l'interface la
    // lettre et les modificateurs.
    events.input.push_back({.type = *down ? InputEventType::ButtonDown : InputEventType::ButtonUp,
                            .device = InputDevice::Keyboard,
                            .code = *scancode});
    events.ui.push_back({.type = UiEventType::Key,
                         .keycode = *keycode,
                         .scancode = *scancode,
                         .down = *down,
                         .modifiers = modifiersOf(held)});
    return {};
}

core::Result<void> addButton(Events& events, int line, const Words& words)
{
    const auto down = directionOf(words);
    if (!down || words.size() != 4)
    {
        return refusal(line, down ? "champs" : "sens",
                       "button down|up left|right|middle est attendu");
    }
    const auto button = mouseButtonCodeFromName(std::string{words[3]});
    if (!button)
    {
        return refusal(line, "bouton", std::format("« {} » : left, right ou middle", words[3]));
    }
    events.input.push_back({.type = *down ? InputEventType::ButtonDown : InputEventType::ButtonUp,
                            .device = InputDevice::Mouse,
                            .code = *button});
    events.ui.push_back({.type = UiEventType::MouseButton,
                         .button = static_cast<std::uint8_t>(*button),
                         .down = *down});
    return {};
}

/// Un nombre à virgule, fini, et rien d'autre dans le texte : `from_chars` lit aussi « nan » et
/// « inf », qu'une position de souris ne peut pas être.
std::optional<float> coordinateOf(std::string_view text)
{
    float value = 0.0f;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() && std::isfinite(value)
               ? std::optional{value}
               : std::nullopt;
}

/// `mouse move <x> <y>` : la position du curseur, en pixels de l'image. Pour l'interface seule (la
/// position n'est pas un `InputEvent` : le jeu lit un déplacement, pas un lieu), comme chez SDL.
core::Result<void> addMouseMove(Events& events, int line, const Words& words)
{
    const auto x = coordinateOf(words[3]);
    const auto y = coordinateOf(words[4]);
    if (!x || !y)
    {
        return refusal(line, "position",
                       std::format("« {} {} » : deux nombres finis", words[3], words[4]));
    }
    events.ui.push_back({.type = UiEventType::MouseMoved, .x = *x, .y = *y});
    return {};
}

/// `mouse motion <dx> <dy>` : le déplacement relatif, ce que lisent le jeu et le vol de l'éditeur
/// (`InputEvent`, deux axes comme `appendMouseMotion`). **Il ne bouge pas le curseur d'ImGui** : à
/// la souris capturée, SDL non plus n'envoie plus de position, seulement le déplacement ; un test
/// qui veut aussi le curseur écrit un `mouse move`.
core::Result<void> addMouseMotion(Events& events, int line, const Words& words)
{
    const auto dx = coordinateOf(words[3]);
    const auto dy = coordinateOf(words[4]);
    if (!dx || !dy)
    {
        return refusal(line, "déplacement",
                       std::format("« {} {} » : deux nombres finis", words[3], words[4]));
    }
    events.input.push_back({.type = InputEventType::AxisMotion,
                            .device = InputDevice::Mouse,
                            .code = 0,
                            .value = *dx});
    events.input.push_back({.type = InputEventType::AxisMotion,
                            .device = InputDevice::Mouse,
                            .code = 1,
                            .value = *dy});
    return {};
}

core::Result<void> addMouse(Events& events, int line, const Words& words)
{
    const std::string_view what = words.size() > 2 ? words[2] : "";
    if (words.size() != 5 || (what != "move" && what != "motion"))
    {
        return refusal(line, "champs", "mouse move|motion <x> <y> est attendu");
    }
    return what == "move" ? addMouseMove(events, line, words) : addMouseMotion(events, line, words);
}

/// `wheel <dy>` : des crans de molette, positifs vers l'avant (loin du joueur), comme SDL. Pour
/// l'interface (`UiEvent`) : le jeu ne lit pas la molette.
core::Result<void> addWheel(Events& events, int line, const Words& words)
{
    if (words.size() != 3)
    {
        return refusal(line, "champs", "wheel <crans> est attendu");
    }
    const auto notches = coordinateOf(words[2]);
    if (!notches)
    {
        return refusal(line, "crans", std::format("« {} » : un nombre fini", words[2]));
    }
    events.ui.push_back({.type = UiEventType::MouseWheel, .y = *notches});
    return {};
}

/// `keymap changed` : la disposition du clavier a changé (SDL_EVENT_KEYMAP_CHANGED). L'événement ne
/// porte pas la disposition : le programme de test la change où il la lit (`App::keyAtPosition`).
core::Result<void> addKeymapChange(Events& events, int line, const Words& words)
{
    if (words.size() != 3 || words[2] != "changed")
    {
        return refusal(line, "champs", "keymap changed est attendu");
    }
    events.ui.push_back({.type = UiEventType::KeymapChanged});
    return {};
}

/// `window close` : la croix de la fenêtre (CLOSE_REQUESTED). Pas de signal scripté : celui-ci se
/// lève pour de vrai (`raise`), dans le test.
core::Result<void> addWindowClose(Events& events, int line, const Words& words)
{
    if (words.size() != 3)
    {
        return refusal(line, "champs", "window close est attendu");
    }
    events.window.push_back({.type = WindowEventType::CloseRequested});
    return {};
}

/// `window scale <échelle>` : le zoom du système change (DISPLAY_SCALE_CHANGED), ce que SDL ne fait
/// jamais sur un pilote hors écran. Strictement positif : une échelle nulle rendrait l'interface
/// invisible (`displayScale`, input.hpp).
core::Result<void> addWindowScale(Events& events, int line, const Words& words)
{
    if (words.size() != 4)
    {
        return refusal(line, "champs", "window scale <échelle> est attendu");
    }
    const auto scale = coordinateOf(words[3]);
    if (!scale || *scale <= 0.0f)
    {
        return refusal(line, "échelle",
                       std::format("« {} » : un nombre fini, plus grand que 0", words[3]));
    }
    events.window.push_back({.type = WindowEventType::ScaleChanged, .displayScale = *scale});
    return {};
}

/// `window close` ou `window scale <x>` : ce que fait la fenêtre.
core::Result<void> addWindow(Events& events, int line, const Words& words)
{
    const std::string_view what = words.size() > 2 ? words[2] : "";
    if (what == "close")
    {
        return addWindowClose(events, line, words);
    }
    if (what == "scale")
    {
        return addWindowScale(events, line, words);
    }
    return refusal(line, "champs", "window close ou window scale <échelle> est attendu");
}

} // namespace

std::uint32_t qwertyKeyAtPosition(std::uint16_t scancode)
{
    return qwertyKeycodeOf(scancode);
}

core::Result<InputScript> parseInputScript(std::string_view text)
{
    // La marque UTF-8 que le Bloc-notes met en tête d'un fichier serait, sinon, un caractère
    // invisible dans le numéro de la première image.
    if (text.starts_with("\xEF\xBB\xBF"))
    {
        text.remove_prefix(3);
    }
    InputScript script;
    HeldModifiers held;
    int lineNumber = 0;
    int previousFrame = 0;
    for (const auto rawLine : std::views::split(text, '\n'))
    {
        const int line = ++lineNumber;
        const Words words = wordsOf(std::string_view{rawLine});
        if (words.empty())
        {
            continue;
        }
        const auto frame = frameOf(words[0]);
        if (!frame)
        {
            return refusal(line, "image",
                           std::format("« {} » : un entier de 0 (la première) à {}", words[0],
                                       MaxInputScriptFrame));
        }
        if (*frame < previousFrame)
        {
            return refusal(line, "image",
                           std::format("{} revient avant l'image {} de la ligne précédente", *frame,
                                       previousFrame));
        }
        previousFrame = *frame;
        const std::string_view kind = words.size() > 1 ? words[1] : "";
        Events& events = script.frames[*frame];
        const auto added =
            kind == "key"      ? addKey(events, line, words, held)
            : kind == "button" ? addButton(events, line, words)
            : kind == "mouse"  ? addMouse(events, line, words)
            : kind == "wheel"  ? addWheel(events, line, words)
            : kind == "keymap" ? addKeymapChange(events, line, words)
            : kind == "window"
                ? addWindow(events, line, words)
                : refusal(line, "type", "key, button, mouse, wheel, keymap ou window est attendu");
        if (!added)
        {
            return std::unexpected(added.error());
        }
    }
    if (auto checked = checkInputScript(script); !checked)
    {
        return std::unexpected(checked.error());
    }
    return script;
}

int scriptLength(const InputScript& script)
{
    return script.frames.empty() ? 0 : script.frames.rbegin()->first + 1;
}

core::Result<void> checkInputScript(const InputScript& script)
{
    if (script.frames.empty())
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               "aucun événement : un script vide ne rejouerait rien");
    }
    if (script.frames.begin()->first < 0 || script.frames.rbegin()->first > MaxInputScriptFrame)
    {
        return core::makeError(core::ErrorCode::InvalidData,
                               std::format("image hors de 0 à {}", MaxInputScriptFrame));
    }
    return {};
}

core::Result<InputScript> loadInputScript(const std::filesystem::path& path)
{
    auto bytes = core::readFile(path);
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    auto script = parseInputScript(
        std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()});
    if (!script)
    {
        return core::makeError(script.error().code,
                               std::format("{} : {}", path.string(), script.error().message));
    }
    return script;
}

void addScriptedEvents(Events& events, const InputScript& script, int frame)
{
    if (const auto scripted = script.frames.find(frame); scripted != script.frames.end())
    {
        const Events& extra = scripted->second;
        events.input.insert(events.input.end(), extra.input.begin(), extra.input.end());
        events.ui.insert(events.ui.end(), extra.ui.begin(), extra.ui.end());
        events.window.insert(events.window.end(), extra.window.begin(), extra.window.end());
    }
}

} // namespace levain::platform
