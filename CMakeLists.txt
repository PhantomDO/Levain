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

# Garde-fou de conformité C++23 (ADR-0001, périmètre revu par ADR-0011 et ADR-0035).
#
# -pedantic-errors transforme toute extension post-C++23 en erreur de compilation : du
# code C++26 écrit par distraction casse le build au lieu de passer en douce.
#
# Clang partout, Windows compris : clang-cl y reçoit -clang:-std=c++23 de CMake (vérifié plus bas) et annonce
# __cplusplus 202302, sans le /Zc:__cplusplus de MSVC (mesuré par l'essai de l'ADR-0035).
#
# Avertissements traités comme des erreurs. Sans -Wall ni -Wextra, la règle « zéro warning »
# (AGENTS.md, règle n°4) ne couvrait que les avertissements actifs par défaut : presque rien.
# Mesuré à l'activation (M1.1) : 4 avertissements, tous du même type, corrigés. Les en-têtes
# des dépendances vcpkg arrivent en -isystem et ne comptent pas.
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    # clang-cl lit -Wall comme /Wall, c'est-à-dire -Weverything : les options de clang y passent par
    # /clang:, pour avoir exactement les mêmes avertissements que sous Linux.
    add_compile_options(/clang:-pedantic-errors /clang:-Wall /clang:-Wextra /WX)
    # Un CMake plus ancien passe /std:c++latest à clang-cl pour C++23 : le brouillon d'après C++26
    # (__cplusplus 202700, ADR-0035), où -pedantic-errors ne garde plus rien.
    if(NOT CMAKE_CXX23_STANDARD_COMPILE_OPTION STREQUAL "-clang:-std=c++23")
        message(FATAL_ERROR "CMake ${CMAKE_VERSION} passe « ${CMAKE_CXX23_STANDARD_COMPILE_OPTION} » à clang-cl "
                            "pour C++23, pas -clang:-std=c++23 : un CMake plus récent est nécessaire (ADR-0035).")
    endif()
else()
    add_compile_options(-pedantic-errors -Wall -Wextra -Werror)
endif()

# La distro WSL où l'on configure un build Windows, pour le hot-reload des shaders (ADR-0035, décision 5) : un exe
# Windows lancé de la distro ne reçoit pas WSL_DISTRO_NAME, il faut donc l'écrire dans le binaire (engine/app).
# Vide partout ailleurs, et pour l'exe d'une CI, qui n'a pas de distro.
set(LEVAIN_WSL_DISTRO "")
if(WIN32)
    # windows.h, qu'amènent Vulkan et SDL, définit des macros min et max qui cassent std::max et
    # numeric_limits<…>::max(). Ce n'est pas un avertissement coupé : jamais _CRT_SECURE_NO_WARNINGS
    # ici (règle n°4), les appels que la CRT de Microsoft juge « unsafe » passent par une fonction nommée.
    # WIN32_LEAN_AND_MEAN l'allège (ni Winsock ni OLE) : posé ici, pas dans chaque fichier, où clang-tidy
    # refuse une macro qui ne commence pas par LEVAIN_ (ADR-0009).
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
    # Chaque programme en UTF-8 (cmake/windows/utf8.manifest) : sans lui, argv et les chemins passés
    # aux fonctions C sont dans la page de code ANSI, et un nom accentué (un cas de test, un asset)
    # n'arrive pas intact.
    add_link_options(/MANIFEST:EMBED "/MANIFESTINPUT:${PROJECT_SOURCE_DIR}/cmake/windows/utf8.manifest")
    # Le routage des rapports de la CRT (engine/core/src/crt_report.cpp) : chaque exécutable du moteur référence son
    # ancre, que core définit. Un exécutable qui ne lie pas core s'arrête à l'édition de liens sur « undefined
    # symbol: LevainCrtReportRouted » : l'oubli ne passe pas. L'ancre installe la ligne de chaque échec, en Debug comme
    # en Release.
    add_link_options(/INCLUDE:LevainCrtReportRouted)
    # En Debug, CMake ajoute /INCREMENTAL (le défaut de MSVC), et lie alors en deux passes pour y mettre le
    # manifeste : son /MANIFEST /MANIFESTFILE, passé après le nôtre, annule /MANIFEST:EMBED, et lld-link s'arrête
    # sur « /manifestinput: requires /manifest:embed ». lld-link ne lie jamais en incrémental : on le dit, pour
    # chaque configuration qui le demande.
    foreach(config DEBUG RELWITHDEBINFO)
        string(REGEX REPLACE "/INCREMENTAL(:YES)?( |$)" "/INCREMENTAL:NO\\2" CMAKE_EXE_LINKER_FLAGS_${config}
               "${CMAKE_EXE_LINKER_FLAGS_${config}}")
    endforeach()
    # Compilé dans une distro WSL, un PDB nomme les sources par leur chemin dans la distro (/home/…), que Visual
    # Studio, RenderDoc et PIX ne trouvent pas sous Windows : on le réécrit en chemin UNC de la distro. Avec des
    # barres obliques : LLVM récrit un \\ initial en \ dans le PDB (mesuré), et perd le préfixe UNC, quand // y
    # reste, que Windows lit comme \\. Seul le débogage est touché, pas __FILE__ (ce serait -ffile-prefix-map).
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND DEFINED ENV{WSL_DISTRO_NAME})
        set(LEVAIN_WSL_DISTRO "$ENV{WSL_DISTRO_NAME}")
        foreach(root IN ITEMS "${PROJECT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
            add_compile_options("/clang:-fdebug-prefix-map=${root}=//wsl.localhost/${LEVAIN_WSL_DISTRO}${root}")
        endforeach()
    endif()
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

# enable_testing() vient AVANT tout add_subdirectory qui déclare des tests : il ne vaut que pour son dossier et ceux
# qu'on ajoute après. Dans un dossier configuré avant, add_test réussit sans mener nulle part (pas de
# CTestTestfile.cmake) : les dxil.* de shaders/ et des plugins sont restés invisibles jusqu'à #354, et
# levain_require_testing_enabled (cmake/LevainShaders.cmake) refuse désormais ce cas. Le moteur inclus par un jeu
# laisse au jeu le soin d'activer ses tests.
if(PROJECT_IS_TOP_LEVEL)
    enable_testing()
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
    add_subdirectory(sandbox)
    add_subdirectory(tests)
endif()
