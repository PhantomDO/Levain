#include "levain/ui/input.hpp"

#include <cfloat>

namespace levain::ui
{

namespace
{

/// Les `SDLK_` d'une touche qui ne tape pas de caractère : son scancode, bit 30 posé
/// (`SDLK_SCANCODE_MASK`, vérifié dans `platform/src/input.cpp`).
constexpr std::uint32_t KeycodeFromScancode = 1U << 30U;

constexpr std::uint32_t special(std::uint32_t scancode)
{
    return scancode | KeycodeFromScancode;
}

/// Le pavé numérique et la ponctuation : par position (les scancodes USB HID que SDL reprend).
ImGuiKey keyOfScancode(std::uint16_t scancode)
{
    switch (scancode)
    {
    case 98:
        return ImGuiKey_Keypad0;
    case 89:
        return ImGuiKey_Keypad1;
    case 90:
        return ImGuiKey_Keypad2;
    case 91:
        return ImGuiKey_Keypad3;
    case 92:
        return ImGuiKey_Keypad4;
    case 93:
        return ImGuiKey_Keypad5;
    case 94:
        return ImGuiKey_Keypad6;
    case 95:
        return ImGuiKey_Keypad7;
    case 96:
        return ImGuiKey_Keypad8;
    case 97:
        return ImGuiKey_Keypad9;
    case 99:
        return ImGuiKey_KeypadDecimal;
    case 84:
        return ImGuiKey_KeypadDivide;
    case 85:
        return ImGuiKey_KeypadMultiply;
    case 86:
        return ImGuiKey_KeypadSubtract;
    case 87:
        return ImGuiKey_KeypadAdd;
    case 88:
        return ImGuiKey_KeypadEnter;
    case 103:
        return ImGuiKey_KeypadEqual;
    default:
        return ImGuiKey_None;
    }
}

/// Les signes que le keycode ne donne pas sur toutes les dispositions : par position, en dernier
/// recours, comme le fait le backend SDL3 d'ImGui.
ImGuiKey punctuationOfScancode(std::uint16_t scancode)
{
    switch (scancode)
    {
    case 53:
        return ImGuiKey_GraveAccent;
    case 45:
        return ImGuiKey_Minus;
    case 46:
        return ImGuiKey_Equal;
    case 47:
        return ImGuiKey_LeftBracket;
    case 48:
        return ImGuiKey_RightBracket;
    case 100:
        return ImGuiKey_Oem102;
    case 49:
        return ImGuiKey_Backslash;
    case 51:
        return ImGuiKey_Semicolon;
    case 52:
        return ImGuiKey_Apostrophe;
    case 54:
        return ImGuiKey_Comma;
    case 55:
        return ImGuiKey_Period;
    case 56:
        return ImGuiKey_Slash;
    default:
        return ImGuiKey_None;
    }
}

/// Le bouton d'ImGui pour un bouton de `platform` : SDL compte 1 à gauche, 2 au milieu, 3 à
/// droite ; ImGui 0 à gauche, 1 à droite, 2 au milieu.
int imguiButtonOf(std::uint8_t button)
{
    switch (button)
    {
    case 1:
        return ImGuiMouseButton_Left;
    case 2:
        return ImGuiMouseButton_Middle;
    case 3:
        return ImGuiMouseButton_Right;
    case 4:
        return 3;
    case 5:
        return 4;
    default:
        return -1;
    }
}

} // namespace

ImGuiKey imguiKeyOf(std::uint32_t keycode, std::uint16_t scancode)
{
    if (const ImGuiKey keypad = keyOfScancode(scancode); keypad != ImGuiKey_None)
    {
        return keypad;
    }
    // Les lettres et les chiffres : leur caractère (les SDLK_ sont des points de code). Une
    // majuscule ne vient que du navigateur, où SDL donne le keycode modifié par Maj : la même
    // touche.
    if (keycode >= 'A' && keycode <= 'Z')
    {
        return static_cast<ImGuiKey>(ImGuiKey_A + static_cast<int>(keycode - 'A'));
    }
    if (keycode >= 'a' && keycode <= 'z')
    {
        return static_cast<ImGuiKey>(ImGuiKey_A + static_cast<int>(keycode - 'a'));
    }
    if (keycode >= '0' && keycode <= '9')
    {
        return static_cast<ImGuiKey>(ImGuiKey_0 + static_cast<int>(keycode - '0'));
    }
    // F1 à F12 (scancodes 58 à 69), F13 à F24 (104 à 115).
    if (keycode >= special(58) && keycode <= special(69))
    {
        return static_cast<ImGuiKey>(ImGuiKey_F1 + static_cast<int>(keycode - special(58)));
    }
    if (keycode >= special(104) && keycode <= special(115))
    {
        return static_cast<ImGuiKey>(ImGuiKey_F13 + static_cast<int>(keycode - special(104)));
    }
    switch (keycode)
    {
    case '\t':
        return ImGuiKey_Tab;
    case ' ':
        return ImGuiKey_Space;
    case '\r':
        return ImGuiKey_Enter;
    case 0x1B:
        return ImGuiKey_Escape;
    case 0x08:
        return ImGuiKey_Backspace;
    case 0x7F:
        return ImGuiKey_Delete;
    case ',':
        return ImGuiKey_Comma;
    case '.':
        return ImGuiKey_Period;
    case ';':
        return ImGuiKey_Semicolon;
    case special(80):
        return ImGuiKey_LeftArrow;
    case special(79):
        return ImGuiKey_RightArrow;
    case special(82):
        return ImGuiKey_UpArrow;
    case special(81):
        return ImGuiKey_DownArrow;
    case special(75):
        return ImGuiKey_PageUp;
    case special(78):
        return ImGuiKey_PageDown;
    case special(74):
        return ImGuiKey_Home;
    case special(77):
        return ImGuiKey_End;
    case special(73):
        return ImGuiKey_Insert;
    case special(57):
        return ImGuiKey_CapsLock;
    case special(71):
        return ImGuiKey_ScrollLock;
    case special(83):
        return ImGuiKey_NumLock;
    case special(70):
        return ImGuiKey_PrintScreen;
    case special(72):
        return ImGuiKey_Pause;
    case special(224):
        return ImGuiKey_LeftCtrl;
    case special(225):
        return ImGuiKey_LeftShift;
    case special(226):
        return ImGuiKey_LeftAlt;
    case special(227):
        return ImGuiKey_LeftSuper;
    case special(228):
        return ImGuiKey_RightCtrl;
    case special(229):
        return ImGuiKey_RightShift;
    case special(230):
        return ImGuiKey_RightAlt;
    case special(231):
        return ImGuiKey_RightSuper;
    case special(101):
        return ImGuiKey_Menu;
    case special(282):
        return ImGuiKey_AppBack;
    case special(283):
        return ImGuiKey_AppForward;
    default:
        break;
    }
    return punctuationOfScancode(scancode);
}

namespace
{

/// La touche d'ImGui d'un événement de touche : à l'appui, celle de son keycode, retenue pour son
/// scancode ; au relâchement, celle retenue à l'appui (`PressedKeys`), quel que soit le keycode.
ImGuiKey releaseAsPressed(PressedKeys& pressed, const platform::UiEvent& event)
{
    // Le scancode 0 est celui des touches inconnues (possible dans le navigateur) : partagé, il
    // mêlerait deux touches tenues ensemble.
    if (event.scancode == 0 || event.scancode >= pressed.size())
    {
        return imguiKeyOf(event.keycode, event.scancode);
    }
    ImGuiKey& remembered = pressed.at(event.scancode);
    if (event.down)
    {
        remembered = imguiKeyOf(event.keycode, event.scancode);
        return remembered;
    }
    const ImGuiKey released =
        remembered != ImGuiKey_None ? remembered : imguiKeyOf(event.keycode, event.scancode);
    remembered = ImGuiKey_None;
    return released;
}

} // namespace

void feedInput(ImGuiIO& io, const platform::Events& events, PressedKeys& pressed)
{
    for (const platform::UiEvent& event : events.ui)
    {
        switch (event.type)
        {
        case platform::UiEventType::MouseMoved:
            io.AddMousePosEvent(event.x, event.y);
            break;
        case platform::UiEventType::MouseButton:
            if (const int button = imguiButtonOf(event.button); button >= 0)
            {
                io.AddMouseButtonEvent(button, event.down);
            }
            break;
        case platform::UiEventType::MouseWheel:
            // ImGui compte la molette horizontale vers la gauche (comme son backend SDL3).
            io.AddMouseWheelEvent(-event.x, event.y);
            break;
        case platform::UiEventType::Key:
            io.AddKeyEvent(ImGuiMod_Ctrl, event.modifiers.ctrl);
            io.AddKeyEvent(ImGuiMod_Shift, event.modifiers.shift);
            io.AddKeyEvent(ImGuiMod_Alt, event.modifiers.alt);
            io.AddKeyEvent(ImGuiMod_Super, event.modifiers.super);
            if (const ImGuiKey key = releaseAsPressed(pressed, event); key != ImGuiKey_None)
            {
                io.AddKeyEvent(key, event.down);
            }
            break;
        case platform::UiEventType::MouseLeft:
            // Plus rien n'est survolé : sans ça, un bouton resterait en surbrillance.
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            break;
        case platform::UiEventType::FocusGained:
            io.AddFocusEvent(true);
            break;
        case platform::UiEventType::FocusLost:
            // ImGui relâche alors toutes les touches : leur relâchement n'arrivera pas.
            io.AddFocusEvent(false);
            break;
        }
    }
    if (!events.text.empty())
    {
        io.AddInputCharactersUTF8(events.text.c_str());
    }
}

} // namespace levain::ui
