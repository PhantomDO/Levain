# Un moteur miniature, pour vérifier que levain_add_plugin fait échouer la configuration quand la
# frontière de l'ADR-0018 est enfreinte. CASE choisit le scénario (tests/CMakeLists.txt).
cmake_minimum_required(VERSION 3.28)
project(PluginBoundaries LANGUAGES CXX)

include("${LEVAIN_CMAKE_DIR}/LevainPlugin.cmake")

add_subdirectory(engine/core)
# L'éditeur, au-dessus du moteur (ADR-0034) ; sans lui, le contrôle refuse de ne rien vérifier.
if(NOT CASE STREQUAL "no-editor")
    add_subdirectory(editor)
endif()

# Deux plugins, dont l'un dépend de l'autre en le déclarant : c'est permis. La cible éditeur d'un
# plugin peut lier l'éditeur.
levain_add_plugin(terrain SOURCES empty.cxx DEPENDS fixture_core)
levain_add_plugin(grass SOURCES empty.cxx DEPENDS plugin::terrain)
if(TARGET levain_editor)
    add_library(terrain_editor STATIC empty.cxx)
    target_link_libraries(terrain_editor PUBLIC plugin::terrain levain_editor)
endif()

if(CASE STREQUAL "engine-links-plugin")
    target_link_libraries(fixture_core PRIVATE plugin::terrain)
elseif(CASE STREQUAL "undeclared-dependency")
    target_link_libraries(grass PUBLIC fixture_core)
elseif(CASE STREQUAL "editor-links-plugin")
    target_link_libraries(levain_editor PRIVATE plugin::terrain)
elseif(CASE STREQUAL "engine-links-editor")
    target_link_libraries(fixture_core PRIVATE levain_editor)
elseif(CASE STREQUAL "plugin-links-editor")
    levain_add_plugin(water SOURCES empty.cxx DEPENDS levain_editor)
elseif(NOT CASE STREQUAL "allowed" AND NOT CASE STREQUAL "no-editor")
    message(FATAL_ERROR "CASE inconnu : « ${CASE} »")
endif()
