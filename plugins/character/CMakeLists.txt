# Le quatrième plugin moteur (ADR-0018, amendé par l'ADR-0028) : la marche du personnage, au-dessus du
# `CharacterVirtual` de `physics`. Le premier qui n'est pas du level design : une brique de gameplay
# que le sandbox et *Rando* reprennent. Il remplit le `CharacterMotion` de l'animation.
levain_add_plugin(character
    SOURCES src/walk.cpp
    DEPENDS levain::animation levain::physics levain::scene)
