# Le troisième plugin moteur (ADR-0018) : l'herbe de M5.7, répartie sur le terrain, dont elle
# dépend (déclaré) : ses brins se posent sur sa heightmap, là où sa couche d'herbe domine.
levain_add_plugin(grass
    SOURCES src/grass.cpp
    DEPENDS plugin::terrain)
