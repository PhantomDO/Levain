# Le socle des programmes du moteur (ADR-0029) : ce que le sandbox, le jeu et plus tard l'éditeur
# partagent. Au-dessus de tous les autres modules ; il ne lie jamais un plugin (ADR-0018).
add_library(levain_app STATIC src/models.cpp src/texture_reload.cpp)
add_library(levain::app ALIAS levain_app)

target_include_directories(levain_app PUBLIC include)

# Tout est PUBLIC : l'API rend les types de ces modules (les modèles, leurs textures, leur animation).
target_link_libraries(levain_app PUBLIC
    levain::animation levain::assets levain::core levain::render)
