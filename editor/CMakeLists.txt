# La bibliothèque de l'éditeur (ADR-0018, ADR-0034, choix 6B) : au-dessus d'`app`, jamais un
# plugin. Seuls les exécutables éditeur la lient : levain_check_plugin_boundaries refuse qu'une
# cible du moteur ou la cible runtime d'un plugin le fasse, et build.no-editor vérifie que le
# sandbox n'en contient rien.
add_library(levain_editor STATIC src/editor.cpp src/hierarchy.cpp src/inspector.cpp)
add_library(levain::editor ALIAS levain_editor)

target_include_directories(levain_editor PUBLIC include)

# PUBLIC : l'API rend les types d'`app` (App, StartFunction), de flecs et d'ImGui.
target_link_libraries(levain_editor PUBLIC levain::app)
