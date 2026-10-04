#pragma once

#include "levain/scene/components.hpp"

namespace levain::scene
{

/// La matrice d'un `Transform` dans le repère de son parent : l'échelle d'abord, puis la rotation,
/// puis la translation. Écrite à la main plutôt qu'en trois produits de glm, parce que le système
/// la calcule pour chaque entité à chaque tour.
inline glm::mat4 localMatrix(const Transform& transform)
{
    glm::mat4 matrix = glm::mat4_cast(transform.rotation);
    matrix[0] *= transform.scale.x; // les colonnes 0 à 2 sont les axes tournés
    matrix[1] *= transform.scale.y;
    matrix[2] *= transform.scale.z;
    matrix[3] = glm::vec4(transform.position, 1.0f);
    return matrix;
}

/// La matrice monde d'une entité : celle de son parent, **puis** la sienne. L'ordre du produit est
/// le piège : `local * parent` ferait tourner le parent autour de l'enfant. Pour une racine,
/// `parentWorld` est l'identité.
///
/// Elle prend la matrice locale déjà composée, et non le `Transform` : mesuré, la version qui
/// appelait `localMatrix` elle-même coûtait 5 ns de plus par entité (0,45 ms sur 100 000), le
/// compilateur ne sachant plus éviter une copie de la matrice.
inline glm::mat4 worldMatrix(const glm::mat4& parentWorld, const glm::mat4& local)
{
    return parentWorld * local;
}

/// La rotation entre deux pas : une interpolation linéaire renormalisée (*nlerp*), et non une
/// *slerp*. Sur un pas de 16 ms, l'écart entre les deux est sous le millième de degré, pour deux
/// fonctions trigonométriques de moins par entité (ADR-0016).
///
/// Le signe choisit le chemin court : deux quaternions opposés décrivent la **même** rotation, et
/// sans ce test l'objet ferait parfois le tour dans l'autre sens.
inline glm::quat nlerpShortestPath(const glm::quat& from, const glm::quat& to, float alpha)
{
    const float direction = glm::dot(from, to) < 0.0f ? -1.0f : 1.0f;
    return glm::normalize(from * (1.0f - alpha) + to * (alpha * direction));
}

/// L'état affiché entre deux pas de simulation : `alpha` vaut 0 sur `previous`, 1 sur `current`.
inline Transform interpolate(const Transform& previous, const Transform& current, float alpha)
{
    return Transform{.position = glm::mix(previous.position, current.position, alpha),
                     .rotation = nlerpShortestPath(previous.rotation, current.rotation, alpha),
                     .scale = glm::mix(previous.scale, current.scale, alpha)};
}

/// Où l'entité se trouve dans le monde : la dernière colonne de sa matrice, celle de la
/// translation. Ce que le rendu envoie au GPU.
inline glm::vec3 worldPosition(const WorldTransform& transform)
{
    return glm::vec3(transform.matrix[3]);
}

/// Comment l'entité est tournée dans le monde, tirée de sa matrice. Le piège : la matrice porte
/// aussi l'échelle, dans la longueur de ses trois premières colonnes, et `quat_cast` d'une matrice
/// étirée rend un quaternion faux. On ramène chaque axe à une longueur de 1 d'abord ; une échelle
/// non uniforme (1, 2, 3) est donc retirée exactement. Trois cas restent faux, sans prévenir : un
/// cisaillement (une échelle non uniforme sous la rotation d'un parent) n'a pas de quaternion
/// exact ; une échelle nulle donne des NaN ; une échelle négative (un miroir) n'est pas une
/// rotation.
inline glm::quat worldRotation(const WorldTransform& transform)
{
    const glm::mat3 axes{glm::normalize(glm::vec3(transform.matrix[0])),
                         glm::normalize(glm::vec3(transform.matrix[1])),
                         glm::normalize(glm::vec3(transform.matrix[2]))};
    return glm::quat_cast(axes);
}

} // namespace levain::scene
