#pragma once

// La collision du terrain (M6.2, ADR-0027) : sa heightmap donnée à la physique, en grille de
// hauteurs partagée. Le terrain est un plugin moteur ; sa collision en fait partie (ADR-0018).

#include <memory>

#include "levain/physics/components.hpp"
#include "levain/terrain/heightmap.hpp"

namespace levain::terrain
{

/// La grille de hauteurs de la physique, copiée de la heightmap : mêmes échantillons, même pas,
/// même rangement. Une copie et non un partage : la heightmap appartient au terrain, que l'éditeur
/// sculptera (M7.6) ; la grille de la physique est immuable, et une heightmap sculptée en donnera
/// une nouvelle (ADR-0027).
[[nodiscard]] std::shared_ptr<const physics::HeightField> heightFieldOf(const Heightmap& heightmap);

/// Le `Collider` du terrain : sa grille de hauteurs, sur la couche du décor. L'entité qui le porte
/// est placée à l'origine du terrain.
[[nodiscard]] physics::Collider colliderOf(const Heightmap& heightmap);

} // namespace levain::terrain
