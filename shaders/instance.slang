// La pose d'une instance (InstancePose, engine/render/include/levain/render/mesh.hpp), partagée par
// la passe des meshes et celle des ombres, qui doivent placer les sommets exactement pareil.

#pragma once

/// Tourne `v` par le quaternion unitaire `q`, rangé (x, y, z, w) comme glm le range. La forme
/// développée de q·v·q⁻¹ : deux produits vectoriels, sans construire de matrice.
float3 rotateByQuaternion(float4 q, float3 v)
{
    const float3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

/// Un point du mesh, déjà placé par la matrice du modèle, tourné autour de l'origine de l'instance
/// puis posé à sa position.
float3 placeInInstance(float3 modelPosition, float4 rotation, float3 position)
{
    return rotateByQuaternion(rotation, modelPosition) + position;
}
