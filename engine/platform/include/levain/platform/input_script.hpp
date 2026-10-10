#pragma once

// Le banc d'essai de l'input (ADR-0036, morceau 2) : des événements écrits dans un fichier, rejoués
// image par image à la place d'un clavier et d'une souris. Ce que le moteur en voit est du type de
// ce que `pollEvents` rend, `Events` : toute la chaîne qui suit (ImGui, `gameInputOf`, les actions)
// est celle du vrai programme. À une différence près : pas de répétition de touche, pas de texte,
// et la lettre d'une ponctuation est son scancode au lieu du caractère que SDL donnerait.

#include <map>
#include <string_view>

#include "levain/core/error.hpp"
#include "levain/platform/window.hpp"

namespace levain::platform
{

/// Le plus grand numéro d'image d'un script : au-delà, le script est une faute de frappe (et
/// « dernière image plus une » déborderait).
inline constexpr int MaxInputScriptFrame = 1'000'000;

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
/// espaces, les images en ordre croissant :
///
///     <image> key down|up <touche>   une position du clavier, par son nom SDL
///
/// **Une touche a deux identités** : sa position (`W`, le scancode, que lit le jeu) et la lettre
/// qu'elle tape (le keycode, que lit ImGui). La lettre est celle d'un clavier QWERTY américain.
///
/// Une ligne que le script ne sait pas lire est refusée en nommant la ligne et le champ : un script
/// qui ne rejouerait pas ce qu'il dit serait un test qui ne vérifie rien (règle n°7). Un texte sans
/// aucun événement l'est aussi (`checkInputScript`).
[[nodiscard]] core::Result<InputScript> parseInputScript(std::string_view text);

/// Ajoute aux événements d'une image, ceux que `pollEvents` a rendus, ceux que le script lui
/// réserve, après les leurs. Sans événement pour cette image, ne fait rien.
void addScriptedEvents(Events& events, const InputScript& script, int frame);

} // namespace levain::platform
