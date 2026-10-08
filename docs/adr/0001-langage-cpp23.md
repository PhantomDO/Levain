# ADR-0001 — Langage : C++23, sans modules

- **Statut** : accepté le 2026-09-20 ; remis en vigueur par [ADR-0011](0011-retour-au-cpp.md) après la parenthèse Rust ;
  « MSVC sous Windows » remplacé par clang-cl ([ADR-0035](0035-windows-compile-depuis-linux.md))
- **Date** : 2026-09-20
- **Milestone** : M0.1

## Contexte

Donnovan travaille en C++ depuis ses débuts et se demande si un langage plus récent serait un meilleur choix
pour écrire un moteur en 2026. Le but du projet est de comprendre les moteurs du marché.

La v1 de cet ADR retenait C++20 et renvoyait C++23 à plus tard, « pour garder un socle commun sûr entre MSVC,
libstdc++ et libc++ ». Donnovan a demandé de réévaluer. Le moment est le bon : aucune ligne de code moteur n'est
écrite, le changement coûte une ligne de CMake aujourd'hui et une migration plus tard.

## Options envisagées

Le choix du **langage** (C++ contre Rust, Zig, Odin, C) est inchangé et reste argumenté plus bas. Ce qui est
réévalué ici est la **version** de C++.

| Option | Pour | Contre |
|---|---|---|
| **C++23** | `std::expected`, `std::print`, `std::stacktrace`, deducing this, `operator[]` multidimensionnel ; complet sur la chaîne Linux de la machine de référence (mesuré) | MSVC n'a pas de `/std:c++23` stable ; quelques trous du cœur du langage restent chez MSVC |
| C++20 | Socle le plus conservateur ; `/std:c++20` stable chez MSVC | Renonce à `std::expected` alors que la politique d'erreurs se décide en M0.3 (ADR-0008) ; migrer plus tard coûtera plus cher |
| C++26 | Reflection, contrats | Norme non publiée ; les implémentations sont partielles et instables ; sans intérêt pour nous aujourd'hui |

### Ce que supporte notre chaîne Linux (mesuré le 2026-09-20)

Sonde de macros de test de fonctionnalité compilée avec `-std=c++23` sur la machine de référence :

| Chaîne | `__cplusplus` | Résultat |
|---|---|---|
| Clang 22.1.8 + libstdc++ 16 | `202302` | **Toutes** les fonctionnalités sondées présentes |
| GCC 16.2.1 + libstdc++ 16 | `202302` | **Toutes** les fonctionnalités sondées présentes |

Sondées : `__cpp_explicit_this_parameter`, `__cpp_if_consteval`, `__cpp_multidimensional_subscript`,
`__cpp_static_call_operator`, `__cpp_auto_cast`, `__cpp_size_t_suffix`, `__cpp_lib_expected`, `__cpp_lib_print`,
`__cpp_lib_mdspan`, `__cpp_lib_flat_map`, `__cpp_lib_generator`, `__cpp_lib_stacktrace`,
`__cpp_lib_to_underlying`, `__cpp_lib_unreachable`, `__cpp_lib_byteswap`, `__cpp_lib_start_lifetime_as`,
`__cpp_lib_ranges_zip`, `__cpp_lib_ranges_chunk`, `__cpp_lib_spanstream`.

libc++ n'est pas installé sur la machine et ne fait pas partie de la matrice de CI (Linux = libstdc++,
Windows = STL de Microsoft). La réserve « libc++ » de la v1 de cet ADR tombe donc d'elle-même.

### Ce que supporte MSVC (doc Microsoft, consultée le 2026-09-20)

C'est le point dur, et il faut l'énoncer franchement : **`/std:c++23` n'existe toujours pas**, ni en
Visual Studio 2022 (`?view=msvc-170`) ni en Visual Studio 2026 (`?view=msvc-180`). Microsoft propose :

- `/std:c++23preview` (depuis VS 2022 17.13 Preview 4) — la doc précise que les fonctionnalités « peuvent changer
  et peuvent ne pas être compatibles en ABI d'une version à l'autre », et que ce commutateur *disparaîtra* le jour
  où `/std:c++23` sera implémenté, « moment auquel C++23 sera pleinement implémenté et stable en ABI » ;
- `/std:c++latest` — tout ce qui est implémenté, **y compris des fonctionnalités expérimentales et en cours**,
  donc un sur-ensemble de C++23 qui déborde sur le brouillon C++26.

**CMake 4.4 mappe `CMAKE_CXX_STANDARD 23` vers `-std:c++latest` chez MSVC**, pas vers `/std:c++23preview`
(`/usr/share/cmake/Modules/Compiler/MSVC-CXX.cmake:46`). C'est donc `/std:c++latest` que nous aurons sous Windows.

Trous du cœur du langage encore marqués « no » chez MSVC, d'après la table de conformité :
`P1774R8` (`[[assume]]`), `P2448R2` (assouplissement de `constexpr`), `P2582R1` (CTAD depuis les constructeurs
hérités), `P2314R4`/`P2362R3`/`P2071R2`/`P2029R4` (jeux de caractères et échappements), `P1467R9` (types
flottants étendus, optionnel). Visual Studio 2026 (MSVC Build Tools 14.50) en a comblé une bonne partie
(`auto(x)`, implicit move simplifié, `#warning`, séquences d'échappement délimitées…).

## Décision

**C++23**, compilé avec MSVC sous Windows et Clang sous Linux. **Sans modules** (inchangé) : CMake les prend en
charge, mais le support reste inégal selon les compilateurs, les IDE et clang-tidy, pour un gain faible à notre
échelle.

Conséquence directe du point MSVC : les deux moitiés de la matrice **ne compilent pas exactement le même
langage**. Linux est à C++23 strict, Windows à `/std:c++latest`, qui est plus permissif. Sans garde-fou, du code
C++26 passerait sous Windows et casserait sous Linux.

**Le garde-fou est Linux.** La CI Linux compile en `-std=c++23 -pedantic-errors` : c'est elle qui fait autorité
sur la conformité. Vérifié le 2026-09-20 sur une fonctionnalité C++26 (indexation de paquets, `P2662`) :

```
clang++ -std=c++26        -fsyntax-only guard.cpp   → compile
clang++ -std=c++23 -pedantic-errors ...             → error: pack indexing is a C++2c extension
g++     -std=c++23 -pedantic-errors ...             → error: l'indexation de paquets est uniquement
                                                        disponible avec « -std=c++2c »
```

Règles qui en découlent, à appliquer dès M0.2 :

1. `cxx_std_23` en CMake, `-pedantic-errors` sur les compilateurs GNU/Clang.
2. **Ne pas utiliser** les fonctionnalités absentes de MSVC listées plus haut. `[[assume]]` est la seule
   réellement regrettée : si le besoin se présente, elle passera par une macro avec repli sur `__builtin_assume`
   et `__assume`.
3. Toute fonctionnalité C++23 employée pour la première fois est vérifiée par la CI Windows avant d'être
   généralisée.

## Conséquences

- **`std::expected` est disponible avant l'ADR-0008** (politique de gestion d'erreurs, prévu en M0.3). C'est la
  raison la plus concrète de ce changement : la plupart des moteurs désactivent les exceptions dans le code
  moteur, et `std::expected` est la réponse moderne à « ni exceptions, ni codes de retour nus ». Décider la
  politique d'erreurs sans l'avoir sous la main aurait été un choix appauvri.
- **`std::stacktrace`** sert directement au système d'assertions de M0.3 (fichier, ligne, message, *et* pile).
- **`std::print`** évite le bruit des iostreams dans les logs et les outils.
- **Deducing this** et **`operator[]` multidimensionnel** : utiles respectivement pour éviter le CRTP et pour les
  types mathématiques et les images.
- **L'avertissement d'ABI de MSVC ne nous atteint pas** tant que nous compilons tout depuis les sources avec une
  seule chaîne : vcpkg est en mode manifeste (ADR-0007) et nous ne livrons aucune bibliothèque C++ précompilée.
  La règle pratique est : après une mise à jour de Visual Studio, reconstruction propre (cache vcpkg invalidé).
- Risques mémoire : ASan et UBSan activés en CI Linux dès M1.1, validation layers Vulkan en Debug.

## Ce que font les autres moteurs

Unreal, Godot, REEngine, Anvil, Frostbite : cœur en C++. Unity : cœur en C++, gameplay en C#. Bevy : Rust.

Sur la **version du langage** :

| Moteur | Version | Source |
|---|---|---|
| Unreal Engine | C++20 par défaut, **minimum requis** pour compiler le moteur ; réglable par `CppStandard` dans les `Target.cs` | [Epic C++ Coding Standard](https://dev.epicgames.com/documentation/en-us/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine) (documenté) |
| Godot 4 | C++17 (`-std=c++17` appliqué par SCons, avec `-fno-exceptions`) | [Working with SCons](https://docs.godotengine.org/en/stable/tutorials/scripting/cpp/build_system/scons.html) (documenté) |
| Unity, REEngine, Anvil, Frostbite | inconnu | pas de source publique trouvée — **ne pas supposer** |

Nous serions donc en avance sur les deux moteurs dont la version est publiquement documentée. C'est assumé, et la
raison est structurelle : leur conservatisme s'explique par une base de code existante de plusieurs millions de
lignes et une matrice de plateformes (consoles, mobiles) dont les chaînes de compilation suivent tard. Nous
n'avons ni l'une ni l'autre. **Si le projet visait un jour une console, ce choix serait à rouvrir** : les SDK
constructeurs sont le facteur limitant, et ils ne sont pas publics.

## Annexe — choix du langage (inchangé depuis la v1)

| Option | Pour | Contre |
|---|---|---|
| **C++** | Langage de tous les moteurs étudiés ; toutes les bibliothèques choisies sont en C ou C++ ; outillage mûr (débogueurs, sanitizers, RenderDoc, Tracy, clang-tidy) ; Donnovan le lit vite | Sécurité mémoire à la charge du développeur ; temps de compilation |
| Rust | Sécurité mémoire ; Cargo ; Bevy montre qu'un moteur sérieux est possible | Le borrow checker résiste aux graphes d'objets et à la mémoire gérée à la main, omniprésents dans un moteur ; Jolt, fastgltf et Slang demandent des bindings ; Donnovan apprendrait Rust au lieu d'apprendre les moteurs |
| Zig | Excellent interop C, build intégré, simplicité | Pas encore en 1.0, les versions cassent encore du code ; les bibliothèques C++ (Jolt, fastgltf) demandent des surcouches C |
| Odin | Pensé pour le jeu vidéo, bindings Vulkan et SDL3 fournis | Écosystème petit ; bibliothèques C++ via surcouches C ; peu de ressources sur l'architecture moteur dans ce langage |
| C | Simplicité, interop universelle | Pas de templates ni de RAII : plus de code pour le même résultat ; Jolt et fastgltf sont en C++ |
