cmake_minimum_required(VERSION 3.28)
project(Levain VERSION 0.1.0 LANGUAGES CXX)

# Garde-fou : sans la toolchain vcpkg, CMake va chercher les dépendances dans le système.
#
# Ce n'est pas théorique : un build configuré sans elle a trouvé le spdlog d'Arch dans
# /usr/lib/cmake/spdlog et a continué sans rien dire, contournant la baseline figée de
# l'ADR-0007. Il n'a échoué que sur Tracy, qui n'existe pas en paquet système — une
# dépendance de moins et personne ne s'apercevait de rien.
if(NOT DEFINED VCPKG_TOOLCHAIN)
    message(FATAL_ERROR
        "La toolchain vcpkg n'est pas active : les dépendances viendraient du système et non "
        "de la baseline figée dans vcpkg.json.\n"
        "  Utilise un preset : cmake --preset linux-debug\n"
        "  Le preset lit la variable d'environnement VCPKG_ROOT, qui doit être exportée :\n"
        "    set -Ux VCPKG_ROOT ~/vcpkg      # fish\n"
        "    export VCPKG_ROOT=~/vcpkg       # bash")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Garde-fou de conformité C++23 (ADR-0001, périmètre revu par ADR-0011).
#
# -pedantic-errors transforme toute extension post-C++23 en erreur de compilation : du
# code C++26 écrit par distraction casse le build au lieu de passer en douce.
#
# Le projet cible Linux et Clang uniquement (ADR-0011). Le bloc MSVC de la v0.3 — et son
# /Zc:__cplusplus, sans lequel MSVC prétend être en C++98 — est retiré. À remettre le jour
# où Windows redevient une cible, avec la CI qui va avec.
#
# Avertissements traités comme des erreurs. Sans -Wall ni -Wextra, la règle « zéro warning »
# (AGENTS.md, règle n°4) ne couvrait que les avertissements actifs par défaut : presque rien.
# Mesuré à l'activation (M1.1) : 4 avertissements, tous du même type, corrigés. Les en-têtes
# des dépendances vcpkg arrivent en -isystem et ne comptent pas.
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    # clang-cl lit -Wall comme /Wall, c'est-à-dire -Weverything : les options de clang y passent
    # par /clang:, pour avoir exactement les mêmes avertissements que sous Linux.
    add_compile_options(/clang:-pedantic-errors /clang:-Wall /clang:-Wextra /WX)
else()
    add_compile_options(-pedantic-errors -Wall -Wextra -Werror)
endif()

if(WIN32)
    # windows.h, qu'amènent Vulkan et SDL, définit des macros min et max qui cassent std::max et
    # numeric_limits<…>::max(). Et la CRT de Microsoft déclare « unsafe » des fonctions standard
    # (getenv, fopen) pour pousser ses variantes _s, hors de la norme : essai, à trancher dans l'ADR.
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
    # Chaque programme en UTF-8 (cmake/windows/utf8.manifest) : sans lui, argv et les chemins passés
    # aux fonctions C sont dans la page de code ANSI, et un nom accentué (un cas de test, un asset)
    # n'arrive pas intact.
    add_link_options(/MANIFEST:EMBED "/MANIFESTINPUT:${PROJECT_SOURCE_DIR}/cmake/windows/utf8.manifest")
endif()

# ADR-0001 dit « sans modules ». CMake active pourtant le scan de modules dès C++20 avec
# Ninja : deux étapes par fichier (scan + dyndep) pour un résultat toujours vide. On le coupe.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Le moteur se construit seul ou comme dépendance d'un jeu, récupéré par FetchContent (ADR-0018).
# Dans le second cas, le manifeste vcpkg lu est celui du jeu : il doit contenir le nôtre.
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(LevainPlugin)
if(NOT PROJECT_IS_TOP_LEVEL)
    include(CheckVcpkgManifest)
    levain_check_vcpkg_manifest("${PROJECT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}")
endif()

# Le code CPU se compile partout, navigateur compris (ADR-0023).
add_subdirectory(engine/core)
add_subdirectory(engine/scene)
add_subdirectory(engine/assets)
add_subdirectory(engine/animation)
add_subdirectory(engine/physics)
# Le GPU (dans le navigateur, son seul backend WebGPU, ADR-0023), le rendu et ses shaders.
add_subdirectory(shaders)
add_subdirectory(engine/gpu)
add_subdirectory(engine/render)

add_subdirectory(engine/platform)
add_subdirectory(engine/input)
add_subdirectory(engine/ui)
# Le socle des programmes, au-dessus de tous les modules (ADR-0029).
add_subdirectory(engine/app)
# Les plugins moteur (ADR-0018), après le moteur dont ils dépendent.
add_subdirectory(plugins/terrain)
add_subdirectory(plugins/water)
add_subdirectory(plugins/grass)
add_subdirectory(plugins/character)
# Le cuiseur est un outil de la machine de développement : les assets arrivent cuits au navigateur.
# L'éditeur aussi (ADR-0034) : la page web n'en a pas. Hors du bloc qui suit, un jeu qui récupère
# le moteur reçoit la bibliothèque, dont il construit son exécutable éditeur.
if(NOT EMSCRIPTEN)
    add_subdirectory(tools/cook)
    add_subdirectory(editor)
endif()

# La démo et les tests ne concernent que le moteur : un jeu qui l'inclut n'en paie pas la
# compilation.
if(PROJECT_IS_TOP_LEVEL)
    enable_testing()
    add_subdirectory(sandbox)
    add_subdirectory(tests)
endif()
