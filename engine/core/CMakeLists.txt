find_package(spdlog CONFIG REQUIRED)

add_library(levain_core STATIC
    src/assert.cpp
    src/environment.cpp
    src/error.cpp
    src/file.cpp
    src/frame_time.cpp
    src/linear_allocator.cpp
    src/log.cpp
    src/pool_allocator.cpp
    src/version.cpp)
add_library(levain::core ALIAS levain_core)

target_include_directories(levain_core PUBLIC include)

# Windows, en Debug : plus de fenêtre de la CRT quand un programme échoue (crt_report.hpp). Le fichier ne contient rien
# qu'un exécutable appellerait : une bibliothèque statique ne livre que les objets qu'on lui demande, donc un
# `/INCLUDE:` force la référence à son ancre. INTERFACE : il suit `levain::core` dans l'édition de liens de tout
# exécutable qui la lie, celui d'un jeu compris ; le CMakeLists racine le pose aussi sur les exécutables du moteur,
# pour qu'un nouvel exécutable qui ne lierait pas core échoue au lieu d'être oublié.
if(WIN32)
    target_sources(levain_core PRIVATE src/crt_report.cpp)
    target_link_options(levain_core INTERFACE /INCLUDE:LevainCrtReportRouted)
endif()

# spdlog est PRIVATE : ses en-têtes ne fuient pas dans l'API de core, et les modules qui
# lient levain::core ne le voient pas. C'est ce qui fait tenir l'invariant n°1 du README.
target_link_libraries(levain_core PRIVATE spdlog::spdlog)

# La version vient du project() racine : une seule source de vérité.
target_compile_definitions(levain_core PRIVATE LEVAIN_VERSION="${PROJECT_VERSION}")

# Assertions actives en Debug uniquement. PUBLIC : la macro est compilée chez l'appelant,
# donc la définition doit le suivre, sinon sandbox et tests la verraient désactivée.
target_compile_definitions(levain_core PUBLIC
    $<$<CONFIG:Debug>:LEVAIN_ASSERTIONS_ENABLED=1>)

# Profilage désactivé par défaut, y compris en Debug : un client Tracy ouvre un socket et
# démarre un thread, ce qui n'a rien à faire dans un build qu'on lance sans profiler.
# Activation explicite : cmake --preset linux-release -DLEVAIN_PROFILING=ON
option(LEVAIN_PROFILING "Activer l'instrumentation Tracy" OFF)

if(LEVAIN_PROFILING)
    # Tracy n'existe pas dans le navigateur : vcpkg ne l'installe pas pour Emscripten (ADR-0023).
    if(EMSCRIPTEN)
        message(FATAL_ERROR "LEVAIN_PROFILING n'est pas disponible dans le build web")
    endif()
    find_package(Tracy CONFIG REQUIRED)
    # Garde-fou : sans TRACY_ENABLE, les macros de Tracy se compilent en rien. Le build réussit,
    # le binaire ne profile rien, et TRACY_NO_EXIT n'attend personne. C'est arrivé au passage à
    # Tracy 0.14, qui a changé la valeur par défaut (ports/tracy/portfile.cmake).
    get_target_property(tracyDefinitions Tracy::TracyClient INTERFACE_COMPILE_DEFINITIONS)
    if(NOT "TRACY_ENABLE" IN_LIST tracyDefinitions)
        message(FATAL_ERROR
            "Tracy::TracyClient ne définit pas TRACY_ENABLE : le profilage serait compilé en rien.")
    endif()

    # PUBLIC des deux côtés : les macros sont compilées chez l'appelant, donc sandbox et
    # tests doivent voir la définition et les en-têtes de Tracy.
    target_compile_definitions(levain_core PUBLIC LEVAIN_PROFILING_ENABLED=1)
    target_link_libraries(levain_core PUBLIC Tracy::TracyClient)
endif()
