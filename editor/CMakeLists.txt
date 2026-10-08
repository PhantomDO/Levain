# La bibliothèque de l'éditeur (ADR-0018, ADR-0034, choix 6B) : au-dessus d'`app`, jamais un
# plugin. Seuls les exécutables éditeur la lient.
add_library(levain_editor STATIC src/editor.cpp)
add_library(levain::editor ALIAS levain_editor)

target_include_directories(levain_editor PUBLIC include)

# PUBLIC : l'API rend les types d'`app` (App, StartFunction) et de flecs.
target_link_libraries(levain_editor PUBLIC levain::app)
