# Le premier plugin moteur (ADR-0018) : le terrain de M5.6. Il dessine dans les étapes du renderer
# (ADR-0025), sans que le moteur le connaisse.
levain_add_plugin(terrain
    SOURCES src/heightmap.cpp src/layer_textures.cpp src/layers.cpp src/patches.cpp src/terrain_pass.cpp
    DEPENDS levain::assets levain::core levain::render)

# Ses shaders, dans le dossier de ceux du moteur, où `render::loadShader` les trouve. Ils incluent
# l'éclairage du moteur (lighting.slang).
add_subdirectory(shaders)
