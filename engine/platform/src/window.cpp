#include "levain/platform/window.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include <SDL3/SDL.h>

#include "input_sdl.hpp"

#include "levain/core/assert.hpp"
#include "levain/core/log.hpp"

namespace levain::platform
{

namespace
{

// Seule une assertion s'en sert : en Release, elle n'apparaît plus que dans un sizeof, et clang
// la déclare inutile (-Wunneeded-internal-declaration).
[[maybe_unused]] bool isAscii(const std::string& text)
{
    return std::ranges::all_of(text, [](char c) { return static_cast<unsigned char>(c) < 0x80; });
}

bool isWindowEvent(const SDL_Event& event)
{
    return event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST;
}

/// Traduit un événement SDL de **fenêtre** vers les nôtres. Le clavier, la souris et les manettes
/// passent par `appendInputEvent` (`input.cpp`).
std::optional<WindowEvent> translateEvent(const SDL_Event& event, SDL_WindowID windowId)
{
    // Envoyé par SDL sur SIGINT (Ctrl+C), SIGTERM, et à la fermeture de la dernière fenêtre.
    if (event.type == SDL_EVENT_QUIT)
    {
        return WindowEvent{.type = WindowEventType::CloseRequested};
    }

    // Le filtre sur l'identifiant servira quand l'éditeur ouvrira des fenêtres secondaires
    // (viewports d'ImGui, M7.1) : fermer l'une d'elles ne doit pas quitter le moteur.
    if (!isWindowEvent(event) || event.window.windowID != windowId)
    {
        return std::nullopt;
    }

    switch (event.type)
    {
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        return WindowEvent{.type = WindowEventType::CloseRequested};

    // PIXEL_SIZE_CHANGED et non RESIZED : RESIZED donne la taille en points, la swapchain a
    // besoin des pixels.
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        return WindowEvent{
            .type = WindowEventType::Resized,
            .pixelSize = {.width = event.window.data1, .height = event.window.data2}};

    // Wayland ne dit jamais à une application qu'elle a été minimisée : le compositeur la
    // « suspend », ce que SDL traduit en OCCLUDED, puis la réveille avec EXPOSED. X11 envoie
    // MINIMIZED et RESTORED. On accepte les deux paires : le moteur n'a besoin que de savoir
    // s'il y a quelque chose à dessiner.
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_OCCLUDED:
        return WindowEvent{.type = WindowEventType::Hidden};

    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_EXPOSED:
        return WindowEvent{.type = WindowEventType::Shown};

    default:
        return std::nullopt;
    }
}

void appendEvent(Events& events, const SDL_Event& event, SDL_WindowID windowId)
{
    if (const auto translated = translateEvent(event, windowId))
    {
        events.window.push_back(*translated);
    }
    appendInputEvent(events.input, event);
}

/// SDL compte en millisecondes sur 32 bits, -1 pour « sans limite ». Une durée finie au-delà (24
/// jours) est tronquée : l'appelant qui a une échéance rappelle. Arrondi au-dessus : 0,4 ms
/// deviendrait sinon une attente nulle, et la boucle tournerait à vide jusqu'à l'échéance.
Sint32 waitTimeoutMs(double seconds)
{
    if (!(seconds > 0.0)) // négatif ou NaN
    {
        return 0;
    }
    if (seconds == std::numeric_limits<double>::infinity())
    {
        return -1;
    }
    return static_cast<Sint32>(
        std::min(std::ceil(seconds * 1000.0), double{std::numeric_limits<Sint32>::max()}));
}

void appendPendingEvents(Events& events, SDL_WindowID windowId)
{
    SDL_Event event{};
    while (SDL_PollEvent(&event))
    {
        appendEvent(events, event, windowId);
    }
}

} // namespace

void WindowDeleter::operator()(SDL_Window* window) const noexcept
{
    closeAllGamepads(); // avant SDL_Quit, qui les fermerait sans le dire
    SDL_DestroyWindow(window);
    SDL_Quit();
}

core::Result<Window> createWindow(const std::string& title, int width, int height)
{
    // Chaque fenêtre arrête SDL en se détruisant. Une deuxième fenêtre vivante serait donc
    // privée de SDL à la destruction de la première.
    LEVAIN_ASSERT(SDL_WasInit(SDL_INIT_VIDEO) == 0, "une seule fenêtre à la fois");

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               std::format("SDL_Init : {}", SDL_GetError()));
    }

    // HIGH_PIXEL_DENSITY : sans ce drapeau, sur un écran à 200 %, SDL demande une surface en
    // basse résolution que le compositeur agrandit, et l'image est floue. VULKAN : SDL refuse de
    // créer une surface Vulkan pour une fenêtre qui ne l'a pas annoncé (engine/gpu). Dans le
    // navigateur, la fenêtre est le canvas de la page, où WebGPU dessine sans rien demander à SDL.
#ifdef __EMSCRIPTEN__
    constexpr SDL_WindowFlags GpuFlag = 0;
#else
    constexpr SDL_WindowFlags GpuFlag = SDL_WINDOW_VULKAN;
#endif
    SDL_Window* handle =
        SDL_CreateWindow(title.c_str(), width, height,
                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | GpuFlag);
    if (handle == nullptr)
    {
        std::string message = std::format("SDL_CreateWindow : {}", SDL_GetError());
        SDL_Quit();
        return core::makeError(core::ErrorCode::Unsupported, std::move(message));
    }

    return Window{.handle = std::unique_ptr<SDL_Window, WindowDeleter>{handle}};
}

PixelSize windowPixelSize(const Window& window)
{
    PixelSize size;
    SDL_GetWindowSizeInPixels(window.handle.get(), &size.width, &size.height);
    return size;
}

Events pollEvents(const Window& window)
{
    Events events;
    appendPendingEvents(events, SDL_GetWindowID(window.handle.get()));
    return events;
}

Events waitEvents(const Window& window, double maxSeconds)
{
    const SDL_WindowID windowId = SDL_GetWindowID(window.handle.get());
    const Sint32 timeoutMs = waitTimeoutMs(maxSeconds);
    Events events;

    // SDL rend false à l'échéance comme en cas d'erreur : seule une attente sans limite qui
    // revient sans événement est une erreur.
    SDL_Event event{};
    if (SDL_WaitEventTimeout(&event, timeoutMs))
    {
        appendEvent(events, event, windowId);
    }
    else if (timeoutMs < 0)
    {
        core::log("platform", core::LogLevel::Warning, "SDL_WaitEventTimeout : {}", SDL_GetError());
    }

    appendPendingEvents(events, windowId);
    return events;
}

void setWindowTitle(Window& window, const std::string& title)
{
    LEVAIN_ASSERT(isAscii(title), "titre non ASCII : perdu sous X11 (voir window.hpp)");
    SDL_SetWindowTitle(window.handle.get(), title.c_str());
}

} // namespace levain::platform
