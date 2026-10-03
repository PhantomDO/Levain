# Le premier plugin moteur (ADR-0018) : le terrain de M5.6. Il dessine dans les étapes du renderer
# (ADR-0025), sans que le moteur le connaisse.
levain_add_plugin(terrain
    SOURCES src/heightmap.cpp src/layers.cpp src/patches.cpp
    DEPENDS levain::core levain::render)
