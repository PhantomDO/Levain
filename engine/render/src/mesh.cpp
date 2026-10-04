#include "levain/render/mesh.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace levain::render
{

Mesh createMesh(nvrhi::IDevice& device, nvrhi::ICommandList& commandList,
                std::span<const MeshVertex> vertices, std::span<const std::uint32_t> indices)
{
    // keepInitialState : NVRHI remet les buffers dans l'état où le GPU les lit après chaque command
    // list qui les a touchés, ici celle de l'envoi.
    Mesh mesh{
        .vertexBuffer =
            device.createBuffer(nvrhi::BufferDesc()
                                    .setByteSize(vertices.size_bytes())
                                    .setIsVertexBuffer(true)
                                    .setInitialState(nvrhi::ResourceStates::VertexBuffer)
                                    .setKeepInitialState(true)
                                    .setDebugName("mesh : sommets")),
        .indexBuffer = device.createBuffer(nvrhi::BufferDesc()
                                               .setByteSize(indices.size_bytes())
                                               .setIsIndexBuffer(true)
                                               .setInitialState(nvrhi::ResourceStates::IndexBuffer)
                                               .setKeepInitialState(true)
                                               .setDebugName("mesh : indices")),
        .indexCount = static_cast<std::uint32_t>(indices.size()),
        .bounds = {},
    };
    std::vector<glm::vec3> positions(vertices.size());
    std::ranges::transform(vertices, positions.begin(),
                           [](const MeshVertex& vertex) { return vertex.position; });
    mesh.bounds = boundsOf(positions);

    // writeBuffer passe par un buffer d'envoi interne à NVRHI, qui place aussi les barrières (E1,
    // §4).
    commandList.writeBuffer(mesh.vertexBuffer, vertices.data(), vertices.size_bytes());
    commandList.writeBuffer(mesh.indexBuffer, indices.data(), indices.size_bytes());
    return mesh;
}

namespace
{

// Le shader lit la pose par octets : position aux octets 0 à 11, quaternion (x, y, z, w) de 12
// à 27.
static_assert(sizeof(InstancePose) == 28);
static_assert(offsetof(InstancePose, rotation) == 12);
// glm range ses quaternions (x, y, z, w) en mémoire, sauf si on lui demande l'inverse : une telle
// définition, venue d'une dépendance, échangerait w et x dans le shader sans un message.
#if defined(GLM_FORCE_QUAT_DATA_WXYZ)
#error "InstancePose suppose glm::quat rangé (x, y, z, w) : GLM_FORCE_QUAT_DATA_WXYZ le casse"
#endif

/// La boîte des positions, et si une instance au moins tourne. Une boucle et non `boundsOf` : elle
/// tourne à chaque image sur toutes les instances, sans copier leurs positions ailleurs.
void measurePoses(std::span<const InstancePose> poses, Instances& instances)
{
    // Sans instance, une boîte réduite à l'origine, comme `boundsOf`.
    const glm::vec3 first = poses.empty() ? glm::vec3{0.0f} : poses.front().position;
    Box bounds{.min = first, .max = first};
    bool anyRotated = false;
    for (const InstancePose& pose : poses)
    {
        bounds.min = glm::min(bounds.min, pose.position);
        bounds.max = glm::max(bounds.max, pose.position);
        anyRotated = anyRotated || pose.rotation != glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
    }
    instances.positionBounds = bounds;
    instances.anyRotated = anyRotated;
}

} // namespace

Instances createInstances(nvrhi::IDevice& device, nvrhi::ICommandList& commandList,
                          std::span<const InstancePose> poses)
{
    Instances instances{
        .poses = device.createBuffer(nvrhi::BufferDesc()
                                         .setByteSize(poses.size_bytes())
                                         .setIsVertexBuffer(true)
                                         .setInitialState(nvrhi::ResourceStates::VertexBuffer)
                                         .setKeepInitialState(true)
                                         .setDebugName("instances : poses")),
        .count = static_cast<std::uint32_t>(poses.size()),
        .capacity = static_cast<std::uint32_t>(poses.size()),
    };
    measurePoses(poses, instances);
    commandList.writeBuffer(instances.poses, poses.data(), poses.size_bytes());
    return instances;
}

void updateInstances(nvrhi::ICommandList& commandList, Instances& instances,
                     std::span<const InstancePose> poses)
{
    // ponytail: capacité fixe ; des entités créées depuis l'explorer au-delà ne s'affichent pas.
    // Un buffer recréé plus grand le jour où la scène grandit pour de bon.
    const std::span<const InstancePose> drawn =
        poses.first(std::min(poses.size(), static_cast<std::size_t>(instances.capacity)));
    // writeBuffer se place dans la command list, avant le dessin qui lit ces poses : NVRHI met la
    // barrière entre les deux, et le GPU n'écrase pas ce qu'une frame précédente lit encore.
    commandList.writeBuffer(instances.poses, drawn.data(), drawn.size_bytes());
    instances.count = static_cast<std::uint32_t>(drawn.size());
    measurePoses(drawn, instances);
}

std::optional<Box> worldBoundsOf(const Mesh& mesh, const Instances& instances,
                                 const glm::mat4& model)
{
    if (!mesh.bounds)
    {
        return std::nullopt;
    }
    Box placed = transformed(*mesh.bounds, model);
    if (instances.anyRotated)
    {
        // La rotation de l'instance tourne le mesh placé autour de l'origine : il reste dans la
        // sphère qui passe par son coin le plus éloigné.
        const float radius = glm::length(glm::max(glm::abs(placed.min), glm::abs(placed.max)));
        placed = Box{.min = glm::vec3{-radius}, .max = glm::vec3{radius}};
    }
    return Box{.min = placed.min + instances.positionBounds.min,
               .max = placed.max + instances.positionBounds.max};
}

/// Les coordonnées de texture des quatre coins d'une face carrée, dans l'ordre où createCube et
/// createPlane les donnent : bas gauche, bas droite, haut droite, haut gauche.
constexpr std::array<glm::vec2, 4> FaceUvs{
    {{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}}};

glm::vec4 tangentOf(const MeshVertex& a, const MeshVertex& b, const MeshVertex& c)
{
    // Les deux arêtes du triangle s'écrivent dans la base (tangente, bitangente) avec leurs écarts
    // de coordonnées de texture : on inverse ce système 2 × 2.
    const glm::vec3 edge1 = b.position - a.position;
    const glm::vec3 edge2 = c.position - a.position;
    const glm::vec2 duv1 = b.uv - a.uv;
    const glm::vec2 duv2 = c.uv - a.uv;
    const float determinant = (duv1.x * duv2.y) - (duv2.x * duv1.y);
    const glm::vec3 tangent =
        glm::normalize((edge1 * duv2.y) - (edge2 * duv1.y)) * glm::sign(determinant);
    const glm::vec3 bitangent = ((edge2 * duv1.x) - (edge1 * duv2.x)) * glm::sign(determinant);
    const float handedness =
        glm::dot(glm::cross(a.normal, tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
    return {tangent, handedness};
}

Mesh createCube(nvrhi::IDevice& device, nvrhi::ICommandList& commandList)
{
    // Les huit coins, nommés par le signe de x, y et z (n : −0,5 ; p : +0,5).
    const glm::vec3 nnn{-0.5f, -0.5f, -0.5f};
    const glm::vec3 nnp{-0.5f, -0.5f, 0.5f};
    const glm::vec3 npn{-0.5f, 0.5f, -0.5f};
    const glm::vec3 npp{-0.5f, 0.5f, 0.5f};
    const glm::vec3 pnn{0.5f, -0.5f, -0.5f};
    const glm::vec3 pnp{0.5f, -0.5f, 0.5f};
    const glm::vec3 ppn{0.5f, 0.5f, -0.5f};
    const glm::vec3 ppp{0.5f, 0.5f, 0.5f};

    const glm::vec3 red{1.0f, 0.25f, 0.25f};
    const glm::vec3 cyan{0.25f, 1.0f, 1.0f};
    const glm::vec3 green{0.25f, 1.0f, 0.25f};
    const glm::vec3 magenta{1.0f, 0.25f, 1.0f};
    const glm::vec3 blue{0.25f, 0.25f, 1.0f};
    const glm::vec3 yellow{1.0f, 1.0f, 0.25f};

    // Une face par ligne, ses quatre coins dans le sens trigonométrique vu de l'extérieur : c'est
    // ce sens qui fait d'une face une face avant (MeshPass élimine les faces arrière).
    struct Face
    {
        std::array<glm::vec3, 4> corners;
        glm::vec3 normal;
        glm::vec3 color;
    };

    const std::array<Face, 6> faces{{
        {{pnp, pnn, ppn, ppp}, {1.0f, 0.0f, 0.0f}, red},
        {{nnn, nnp, npp, npn}, {-1.0f, 0.0f, 0.0f}, cyan},
        {{npp, ppp, ppn, npn}, {0.0f, 1.0f, 0.0f}, green},
        {{nnn, pnn, pnp, nnp}, {0.0f, -1.0f, 0.0f}, magenta},
        {{nnp, pnp, ppp, npp}, {0.0f, 0.0f, 1.0f}, blue},
        {{pnn, nnn, npn, ppn}, {0.0f, 0.0f, -1.0f}, yellow},
    }};

    // Chaque face commence par ses deux coins du bas (ou, pour ±y, par un bord), dans le même
    // sens : les quatre coins de chaque face reçoivent les mêmes coordonnées de texture.
    std::array<MeshVertex, 24> vertices{};
    for (std::size_t face = 0; face < faces.size(); ++face)
    {
        for (std::size_t corner = 0; corner < 4; ++corner)
        {
            vertices[(face * 4) + corner] = {.position = faces[face].corners[corner],
                                             .normal = faces[face].normal,
                                             .color = faces[face].color,
                                             .uv = FaceUvs[corner]};
        }
        const glm::vec4 tangent =
            tangentOf(vertices[face * 4], vertices[(face * 4) + 1], vertices[(face * 4) + 2]);
        for (std::size_t corner = 0; corner < 4; ++corner)
        {
            vertices[(face * 4) + corner].tangent = tangent;
        }
    }

    // Deux triangles par face : (0, 1, 2) et (0, 2, 3) sur ses quatre coins.
    std::array<std::uint32_t, 36> indices{};
    for (std::size_t face = 0; face < 6; ++face)
    {
        const auto first = static_cast<std::uint32_t>(face * 4);
        const std::array<std::uint32_t, 6> quad{first, first + 1, first + 2,
                                                first, first + 2, first + 3};
        std::ranges::copy(quad, indices.begin() + static_cast<std::ptrdiff_t>(face * 6));
    }
    return createMesh(device, commandList, vertices, indices);
}

Mesh createPlane(nvrhi::IDevice& device, nvrhi::ICommandList& commandList, float size,
                 float textureRepeat)
{
    const float h = size / 2.0f;
    const glm::vec3 white{1.0f};
    // Les coins dans le sens trigonométrique vu d'en haut, comme la face +y du cube.
    const std::array<glm::vec3, 4> corners{
        {{-h, 0.0f, h}, {h, 0.0f, h}, {h, 0.0f, -h}, {-h, 0.0f, -h}}};
    std::array<MeshVertex, 4> vertices{};
    for (std::size_t i = 0; i < vertices.size(); ++i)
    {
        // Au-delà de 1, le sampler en Wrap répète la texture : textureRepeat fois sur chaque côté.
        vertices[i] = {.position = corners[i],
                       .normal = {0.0f, 1.0f, 0.0f},
                       .color = white,
                       .uv = FaceUvs[i] * textureRepeat};
    }
    const glm::vec4 tangent = tangentOf(vertices[0], vertices[1], vertices[2]);
    for (MeshVertex& vertex : vertices)
    {
        vertex.tangent = tangent;
    }
    const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    return createMesh(device, commandList, vertices, indices);
}

} // namespace levain::render
