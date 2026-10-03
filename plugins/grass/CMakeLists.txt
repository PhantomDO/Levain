# Le troisième plugin moteur (ADR-0018) : l'herbe de M5.7, répartie sur le terrain, dont elle
# dépend (déclaré) : ses brins se posent sur sa heightmap, là où sa couche d'herbe domine.
levain_add_plugin(grass
    SOURCES src/grass.cpp src/grass_pass.cpp
    DEPENDS levain::core levain::render plugin::terrain)

# Ses shaders, dans le dossier de ceux du moteur, comme ceux du terrain.
add_subdirectory(shaders)
