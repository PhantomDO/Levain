#pragma once

// Le banc d'essai de l'input (ADR-0036, morceau 2) : des événements écrits dans un fichier, rejoués
// image par image à la place d'un clavier et d'une souris. Ce que le moteur en voit est du type de
// ce que `pollEvents` rend, `Events` : toute la chaîne qui suit (ImGui, `gameInputOf`, les actions)
// est celle du vrai programme. À une différence près : pas de répétition de touche, un texte d'un
// seul mot (`text`), et la lettre d'une ponctuation sans `as` est son scancode au lieu du caractère
// que SDL donnerait.
// **Le temps de l'interface compte en images**, non en secondes : sous un script, la boucle donne à
// ImGui le pas fixe de 1/60 s à chaque image (`app::uiFrameSecondsFor`), quelle que soit la
// lenteur de la machine. Un double-clic (0,30 s, `io.MouseDoubleClickTime`) veut donc deux appuis
// à moins de 18 images l'un de l'autre, la répétition d'une touche tenue (0,275 s,
// `io.KeyRepeatDelay`) ne commence qu'après 17 images, et un délai de survol se compte de même.

#include <cstdint>
#include <filesystem>
#include <map>
#include <string_view>

#include "levain/core/error.hpp"
#include "levain/platform/window.hpp"

namespace levain::platform
{

/// Le plus grand numéro d'image d'un script : au-delà, le script est une faute de frappe (et
/// « dernière image plus une » déborderait).
inline constexpr int MaxInputScriptFrame = 1'000'000;

/// Le keycode (`SDLK_`) qu'un clavier QWERTY américain donne à ce scancode : la disposition du banc
/// d'essai, la même sur toutes les machines (`SDL_GetKeyFromScancode` lit celle de la machine).
/// Une disposition de test en part : `keyAtPosition` d'un AZERTY n'en change que quelques lettres.
[[nodiscard]] std::uint32_t qwertyKeyAtPosition(std::uint16_t scancode);

/// Les événements à rejouer, par numéro d'image (0 pour la première de la boucle).
struct InputScript
{
    std::map<int, Events> frames;
};

/// Le nombre d'images que le script joue : celle de son dernier événement plus une. La boucle s'y
/// arrête quand aucune autre fin n'est donnée (app.hpp). Calculé, et non gardé à côté de `frames` :
/// un script construit à la main, par un test, ne peut pas l'oublier.
[[nodiscard]] int scriptLength(const InputScript& script);

/// Un script qui rejoue quelque chose : au moins un événement, tous entre l'image 0 et
/// `MaxInputScriptFrame`. Un script vide ne rejouerait rien et laisserait passer un test sans qu'il
/// ait vérifié quoi que ce soit (règle n°7).
[[nodiscard]] core::Result<void> checkInputScript(const InputScript& script);

/// Lit un script, une ligne par événement, `#` pour un commentaire. Les mots sont séparés par des
/// espaces ou des tabulations, les images en ordre croissant (un fichier peut commencer par la
/// marque UTF-8, comme l'écrit le Bloc-notes) :
///
// clang-format off
///     <image> key down|up <touche> [as <lettre>]   une position du clavier, par son nom SDL
///     <image> button down|up left|right|middle     un bouton de la souris
///     <image> mouse move <x> <y>                   le curseur, en pixels de l'image
///     <image> mouse motion <dx> <dy>               le déplacement relatif (le vol, le jeu), sans
///                                                  bouger le curseur
///     <image> wheel <crans>                        la molette, positive vers l'avant (SDL)
///     <image> text <mot>                           le texte tapé (un mot, sans blanc ni #)
///     <image> keymap changed                       la disposition du clavier a changé
///     <image> window close                         la croix de la fenêtre
///     <image> window scale <échelle>               le zoom du système change (> 0)
// clang-format on
///
/// **Une touche a deux identités** : sa position (`W`, le scancode, que lit le jeu) et la lettre
/// qu'elle tape (le keycode, que lit ImGui). Sans `as`, la lettre est celle d'un clavier QWERTY
/// américain ; `key down W as z` simule un AZERTY, où la touche à la place du W tape « z ». Les
/// touches de modification tenues s'ajoutent aux événements qui suivent, comme chez SDL.
///
/// Une ligne que le script ne sait pas lire est refusée en nommant la ligne et le champ : un script
/// qui ne rejouerait pas ce qu'il dit serait un test qui ne vérifie rien (règle n°7). Un texte sans
/// aucun événement l'est aussi (`checkInputScript`).
[[nodiscard]] core::Result<InputScript> parseInputScript(std::string_view text);

/// Comme `parseInputScript`, pour un fichier : une erreur y ajoute son nom.
[[nodiscard]] core::Result<InputScript> loadInputScript(const std::filesystem::path& path);

/// Ajoute aux événements d'une image, ceux que `pollEvents` a rendus, ceux que le script lui
/// réserve, après les leurs. Sans événement pour cette image, ne fait rien.
void addScriptedEvents(Events& events, const InputScript& script, int frame);

/// **Sous un script, le script est la seule source d'input** : des événements d'une image, ne
/// garde que ceux de la fenêtre (fermeture, signal, taille, échelle, masquée, montrée). Le vrai
/// curseur n'est pas celui du script, et sur un vrai bureau (Windows, où lavapipe n'a pas de
/// surface hors écran) le système le rapporte quand il veut : la fin d'un vol le replace au point
/// de l'appui (`App::mouseReturnTo`), et sur le runner Windows en Debug ce point revenait deux
/// images plus tard, après le déplacement scripté suivant, posant le clic suivant sur le menu
/// ouvert là. Le focus, la sortie de la souris, le texte et la disposition du clavier en sont
/// aussi : un script qui en a besoin les écrit (`keymap changed`). L'échelle, elle, est la
/// fenêtre : un vrai changement de zoom refait le style sous un script aussi.
[[nodiscard]] Events withoutRealInput(Events events);

} // namespace levain::platform
