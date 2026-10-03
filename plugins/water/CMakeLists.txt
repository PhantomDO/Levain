# Le deuxième plugin moteur (ADR-0018) : l'eau de M5.7, un lac calme posé sur le terrain.
levain_add_plugin(water
    SOURCES src/lake.cpp
    DEPENDS levain::assets)
