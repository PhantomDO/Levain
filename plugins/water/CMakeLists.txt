# Le deuxième plugin moteur (ADR-0018) : l'eau de M5.7, un lac calme posé sur le terrain, dont il
# dépend (déclaré) : le fond du lac est la heightmap du terrain.
levain_add_plugin(water
    SOURCES src/lake.cpp src/water_pass.cpp
    DEPENDS levain::assets levain::core levain::render plugin::terrain)

# Ses shaders, dans le dossier de ceux du moteur, comme ceux du terrain.
add_subdirectory(shaders)
