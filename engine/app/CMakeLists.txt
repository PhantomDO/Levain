# Le socle des programmes du moteur (ADR-0029) : ce que le sandbox, le jeu et plus tard l'éditeur
# partagent. Au-dessus de tous les autres modules ; il ne lie jamais un plugin (ADR-0018).
add_library(levain_app STATIC src/app.cpp src/camera.cpp src/load_model.cpp src/models.cpp
    src/panels.cpp src/player_input.cpp src/shader_reload.cpp src/texture_reload.cpp src/ui_layer.cpp)
add_library(levain::app ALIAS levain_app)

target_include_directories(levain_app PUBLIC include)

# Tout est PUBLIC : l'API rend les types de ces modules (la fenêtre, le device, le monde, l'input, les
# modèles et leur animation). La physique n'y est pas : rien ici ne s'en sert.
target_link_libraries(levain_app PUBLIC
    levain::animation levain::assets levain::core levain::gpu levain::input levain::platform
    levain::render levain::scene levain::ui)

# L'explorer web de flecs, en Debug seulement : en Release, le code qui l'active n'est pas compilé,
# et aucun serveur n'écoute.
target_compile_definitions(levain_app PRIVATE $<$<CONFIG:Debug>:LEVAIN_ENABLE_EXPLORER=1>)
