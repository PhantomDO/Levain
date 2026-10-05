#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nvrhi/nvrhi.h>

#include "levain/render/culling.hpp"

namespace levain::render
{

/// Un sommet : position, normale, tangente, couleur et coordonnées de texture. Doit correspondre à
/// `VertexInput` dans `shaders/mesh.slang`, et à `SkinnedStride` dans `shaders/skinning.slang`.
struct MeshVertex
{
    glm::vec3 position;
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    /// La direction où u croît, pour les normal maps ; w (±1) dit dans quel sens croît v.
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
    glm::vec3 color{1.0f};
    glm::vec2 uv{0.0f}; ///< (0, 0) en haut à gauche de la texture, comme sous Direct3D et Vulkan.
};

/// La tangente d'un triangle plat : la direction où u croît, et le sens où croît v (w = ±1), tirés
/// de ses positions et de ses coordonnées de texture. Ce que glTF appelle TANGENT.
[[nodiscard]] glm::vec4 tangentOf(const MeshVertex& a, const MeshVertex& b, const MeshVertex& c);

/// Un mesh indexé en mémoire GPU : chaque sommet n'est stocké qu'une fois, les triangles le
/// désignent par son indice. Un cube : 24 sommets au lieu de 36.
struct Mesh
{
    nvrhi::BufferHandle vertexBuffer;
    nvrhi::BufferHandle indexBuffer;
    std::uint32_t indexCount = 0;
    /// La boîte de ses sommets, pour le culling. Absente pour un mesh skinné, que l'animation
    /// déforme : il n'est jamais écarté.
    std::optional<Box> bounds;
};

/// Où est placée une copie d'un mesh : sa rotation propre, autour de son origine, puis sa position.
/// Doit correspondre à `INSTANCE_POSITION` et `INSTANCE_ROTATION` dans `shaders/mesh.slang` et
/// `shaders/shadow.slang` : 28 octets, le quaternion rangé (x, y, z, w) comme glm le range.
struct InstancePose
{
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; ///< (w, x, y, z) au constructeur : l'identité.
};

/// Les copies d'un mesh dessinées en un seul appel (instancing) : une pose par instance, lue une
/// fois par instance et non une fois par sommet. Chacune tourne sur elle-même, ce que demandent
/// les caisses de la physique (M6.1) et, plus tard, les rochers et les arbres placés au pinceau.
struct Instances
{
    nvrhi::BufferHandle poses;
    std::uint32_t count = 0;    ///< Instances dessinées.
    std::uint32_t capacity = 0; ///< Poses que le buffer peut contenir.
    Box positionBounds{};       ///< La boîte des positions dessinées.
    bool anyRotated = false;    ///< Une instance au moins tourne : la boîte du mesh ne suffit plus.
};

/// Crée le buffer des poses, à la taille de `poses`, et enregistre son envoi dans `commandList`.
[[nodiscard]] Instances createInstances(nvrhi::IDevice& device, nvrhi::ICommandList& commandList,
                                        std::span<const InstancePose> poses);

/// Remplace les poses des instances, par exemple à chaque frame depuis le monde flecs. Au-delà de
/// la capacité du buffer, les poses en trop ne sont pas dessinées.
void updateInstances(nvrhi::ICommandList& commandList, Instances& instances,
                     std::span<const InstancePose> poses);

/// La boîte, dans le monde, de toutes les instances de `mesh` placées par `model` : la boîte du
/// mesh transformée, puis étirée de l'écart entre les positions des instances, que le shader ajoute
/// après la matrice. Si une instance tourne, la boîte du mesh devient celle de la sphère qui le
/// contient quelle que soit sa rotation : plus large, jamais fausse. Absente si le mesh n'a pas de
/// boîte.
[[nodiscard]] std::optional<Box> worldBoundsOf(const Mesh& mesh, const Instances& instances,
                                               const glm::mat4& model);

/// Crée les buffers et enregistre l'envoi des données dans `commandList`, que l'appelant a ouverte
/// et exécutera avant le premier dessin.
[[nodiscard]] Mesh createMesh(nvrhi::IDevice& device, nvrhi::ICommandList& commandList,
                              std::span<const MeshVertex> vertices,
                              std::span<const std::uint32_t> indices);

/// Un cube de côté 1, centré sur l'origine, une couleur par face, la texture entière sur chaque
/// face.
[[nodiscard]] Mesh createCube(nvrhi::IDevice& device, nvrhi::ICommandList& commandList);

/// Un sol carré de côté `size`, horizontal, centré sur l'origine et tourné vers le haut, sur lequel
/// la texture se répète `textureRepeat` fois dans chaque direction.
[[nodiscard]] Mesh createPlane(nvrhi::IDevice& device, nvrhi::ICommandList& commandList, float size,
                               float textureRepeat);

} // namespace levain::render
