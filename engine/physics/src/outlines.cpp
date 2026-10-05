#include "levain/physics/outlines.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <variant>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

namespace levain::physics
{

namespace
{

/// Un point du repère du corps, dans le monde.
glm::vec3 placed(const BodyPose& pose, const glm::vec3& local)
{
    return pose.position + pose.rotation * local;
}

void addSegment(const BodyPose& pose, const glm::vec3& from, const glm::vec3& to,
                std::vector<Segment>& out)
{
    out.push_back({.from = placed(pose, from), .to = placed(pose, to)});
}

/// Un arc de `center`, dans le plan des axes `u` et `v` (de longueur `radius`), de `start` à `end`
/// radians.
void addArc(const BodyPose& pose, const glm::vec3& center, const glm::vec3& u, const glm::vec3& v,
            float start, float end, int segments, std::vector<Segment>& out)
{
    const auto pointAt = [&](int i)
    {
        const float angle =
            start + (end - start) * static_cast<float>(i) / static_cast<float>(segments);
        return center + u * std::cos(angle) + v * std::sin(angle);
    };
    for (int i = 0; i < segments; ++i)
    {
        addSegment(pose, pointAt(i), pointAt(i + 1), out);
    }
}

void appendBox(const Box& box, const BodyPose& pose, std::vector<Segment>& out)
{
    const glm::vec3 h = box.halfExtents;
    // Les huit coins, le bit 0 pour x, 1 pour y, 2 pour z ; une arête relie deux coins qui ne
    // diffèrent que d'un bit.
    std::array<glm::vec3, 8> corners{};
    for (std::uint32_t i = 0; i < 8; ++i)
    {
        corners[i] = {(i & 1u) != 0 ? h.x : -h.x, (i & 2u) != 0 ? h.y : -h.y,
                      (i & 4u) != 0 ? h.z : -h.z};
    }
    for (std::uint32_t i = 0; i < 8; ++i)
    {
        for (const std::uint32_t bit : {1u, 2u, 4u})
        {
            if ((i & bit) == 0)
            {
                addSegment(pose, corners[i], corners[i | bit], out);
            }
        }
    }
}

void appendSphere(const Sphere& sphere, const BodyPose& pose, std::vector<Segment>& out)
{
    const float r = sphere.radius;
    const float full = glm::two_pi<float>();
    addArc(pose, {}, {r, 0, 0}, {0, r, 0}, 0.0f, full, CircleSegments, out);
    addArc(pose, {}, {0, r, 0}, {0, 0, r}, 0.0f, full, CircleSegments, out);
    addArc(pose, {}, {r, 0, 0}, {0, 0, r}, 0.0f, full, CircleSegments, out);
}

void appendCapsule(const Capsule& capsule, const BodyPose& pose, std::vector<Segment>& out)
{
    const float r = capsule.radius;
    const float h = capsule.halfHeight;
    const float full = glm::two_pi<float>();
    const float half = glm::pi<float>();
    // Les deux cercles où le cylindre rejoint ses demi-sphères, et quatre génératrices.
    addArc(pose, {0, h, 0}, {r, 0, 0}, {0, 0, r}, 0.0f, full, CircleSegments, out);
    addArc(pose, {0, -h, 0}, {r, 0, 0}, {0, 0, r}, 0.0f, full, CircleSegments, out);
    for (const glm::vec3 side :
         {glm::vec3{r, 0, 0}, glm::vec3{-r, 0, 0}, glm::vec3{0, 0, r}, glm::vec3{0, 0, -r}})
    {
        addSegment(pose, side + glm::vec3{0, h, 0}, side - glm::vec3{0, h, 0}, out);
    }
    // Les demi-sphères : deux demi-cercles par bout, dans les plans XY et ZY.
    addArc(pose, {0, h, 0}, {r, 0, 0}, {0, r, 0}, 0.0f, half, CircleSegments / 2, out);
    addArc(pose, {0, h, 0}, {0, 0, r}, {0, r, 0}, 0.0f, half, CircleSegments / 2, out);
    addArc(pose, {0, -h, 0}, {r, 0, 0}, {0, -r, 0}, 0.0f, half, CircleSegments / 2, out);
    addArc(pose, {0, -h, 0}, {0, 0, r}, {0, -r, 0}, 0.0f, half, CircleSegments / 2, out);
}

void appendMesh(const MeshShape& shape, const BodyPose& pose, std::vector<Segment>& out)
{
    if (shape.mesh == nullptr)
    {
        return;
    }
    const TriangleMesh& mesh = *shape.mesh;
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        const glm::vec3& a = mesh.vertices.at(mesh.indices[i]);
        const glm::vec3& b = mesh.vertices.at(mesh.indices[i + 1]);
        const glm::vec3& c = mesh.vertices.at(mesh.indices[i + 2]);
        addSegment(pose, a, b, out);
        addSegment(pose, b, c, out);
        addSegment(pose, c, a, out);
    }
}

/// Le bord de la grille, qui suit le relief : quatre lignes brisées, un segment par échantillon.
void appendHeightField(const HeightFieldShape& shape, const BodyPose& pose,
                       std::vector<Segment>& out)
{
    if (shape.field == nullptr || shape.field->size < 2)
    {
        return;
    }
    const HeightField& field = *shape.field;
    const std::uint32_t last = field.size - 1;
    const auto sample = [&field](std::uint32_t x, std::uint32_t z)
    {
        return glm::vec3{static_cast<float>(x) * field.spacing,
                         field.heights.at(std::size_t{z} * field.size + x),
                         static_cast<float>(z) * field.spacing};
    };
    for (std::uint32_t i = 0; i < last; ++i)
    {
        addSegment(pose, sample(i, 0), sample(i + 1, 0), out);
        addSegment(pose, sample(i, last), sample(i + 1, last), out);
        addSegment(pose, sample(0, i), sample(0, i + 1), out);
        addSegment(pose, sample(last, i), sample(last, i + 1), out);
    }
}

} // namespace

void appendOutline(const Collider& collider, const BodyPose& pose, std::vector<Segment>& out)
{
    std::visit(
        [&](const auto& shape)
        {
            using T = std::decay_t<decltype(shape)>;
            if constexpr (std::is_same_v<T, Box>)
            {
                appendBox(shape, pose, out);
            }
            else if constexpr (std::is_same_v<T, Sphere>)
            {
                appendSphere(shape, pose, out);
            }
            else if constexpr (std::is_same_v<T, Capsule>)
            {
                appendCapsule(shape, pose, out);
            }
            else if constexpr (std::is_same_v<T, MeshShape>)
            {
                appendMesh(shape, pose, out);
            }
            else
            {
                appendHeightField(shape, pose, out);
            }
        },
        collider.shape);
}

} // namespace levain::physics
