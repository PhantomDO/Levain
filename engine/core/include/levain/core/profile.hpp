#pragma once

/// Instrumentation Tracy.
///
/// Tracy est un profileur à échantillonnage **et** à instrumentation : les zones déclarées
/// ici deviennent des barres dans sa timeline, avec leur durée exacte, ce qu'un profileur
/// purement statistique ne donne pas. On s'en sert pour répondre à « où part la frame ? ».
///
/// Compilé hors du binaire par défaut : sans `LEVAIN_PROFILING_ENABLED`, chaque macro se
/// réduit à rien et il ne reste **aucune** trace de Tracy dans le programme — ni symbole,
/// ni thread, ni socket d'écoute.
///
/// Le client Tracy attend qu'un profileur se connecte ; lancer le programme sans profileur
/// n'a pas d'effet visible.

#if defined(LEVAIN_PROFILING_ENABLED) && LEVAIN_PROFILING_ENABLED

#include <tracy/Tracy.hpp>

/// Mesure le bloc courant. Le nom affiché est celui de la fonction englobante.
#define LEVAIN_PROFILE_SCOPE() ZoneScoped

/// Mesure le bloc courant sous un nom choisi, quand le nom de la fonction ne suffit pas.
#define LEVAIN_PROFILE_SCOPE_NAMED(name) ZoneScopedN(name)

/// Mesure le bloc courant sous un nom connu à l'exécution seulement (une `std::string_view`) :
/// celui d'une fonction d'étape inscrite par un plugin. Tracy copie le texte.
#define LEVAIN_PROFILE_SCOPE_TEXT(text)                                                            \
    ZoneScoped;                                                                                    \
    ZoneName((text).data(), (text).size())

/// Ajoute un point à la courbe `name` : un `const char*` qui doit rester valide et à la même
/// adresse toute la session, car Tracy reconnaît une courbe à son pointeur, pas à son texte.
#define LEVAIN_PROFILE_PLOT(name, value) TracyPlot(name, value)

/// Marque la fin d'une frame. C'est elle qui découpe la timeline de Tracy et qui lui permet
/// de calculer les statistiques par frame.
#define LEVAIN_PROFILE_FRAME() FrameMark

/// Signale une allocation au suivi mémoire de Tracy.
#define LEVAIN_PROFILE_ALLOC(pointer, size) TracyAlloc(pointer, size)
#define LEVAIN_PROFILE_FREE(pointer) TracyFree(pointer)

#else

#define LEVAIN_PROFILE_SCOPE() ((void)0)
#define LEVAIN_PROFILE_SCOPE_NAMED(name) ((void)0)
#define LEVAIN_PROFILE_SCOPE_TEXT(text) ((void)0)
#define LEVAIN_PROFILE_PLOT(name, value) ((void)0)
#define LEVAIN_PROFILE_FRAME() ((void)0)
#define LEVAIN_PROFILE_ALLOC(pointer, size) ((void)0)
#define LEVAIN_PROFILE_FREE(pointer) ((void)0)

#endif
