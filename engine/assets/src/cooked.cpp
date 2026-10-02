#include "levain/assets/cooked.hpp"

#include <array>
#include <bit>
#include <cstring>
#include <format>
#include <fstream>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include "levain/core/file.hpp"

namespace levain::assets
{

namespace
{

// Le format s'écrit tel qu'en mémoire : il ne tient que sur une machine petit-boutiste, et les
// structures copiées d'un bloc ne doivent contenir aucun pointeur.
static_assert(std::endian::native == std::endian::little, "le .lvmesh est petit-boutiste");
static_assert(std::is_trivially_copyable_v<ModelVertex>);
static_assert(std::is_trivially_copyable_v<scene::Transform>);

constexpr std::array<char, 4> Signature{'L', 'V', 'M', 'S'};
/// 2 : le skinning des sommets, les nœuds os (M4.5). 3 : les tangentes des sommets (M5.1).
constexpr std::uint32_t FormatVersion = 3;

/// Ce qu'on écrit, dans l'ordre : des valeurs simples, des chaînes et des tableaux, préfixés de
/// leur taille.
class Writer
{
public:
    template <typename T> void value(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto* bytes = reinterpret_cast<const std::byte*>(&value);
        m_buffer.insert(m_buffer.end(), bytes, bytes + sizeof(T));
    }

    template <typename T> void array(std::span<const T> values)
    {
        value(static_cast<std::uint64_t>(values.size()));
        const auto bytes = std::as_bytes(values);
        m_buffer.insert(m_buffer.end(), bytes.begin(), bytes.end());
    }

    void text(const std::string& text) { array(std::span{text.data(), text.size()}); }

    [[nodiscard]] const std::vector<std::byte>& bytes() const { return m_buffer; }

private:
    std::vector<std::byte> m_buffer;
};

/// La lecture symétrique, qui vérifie chaque borne : un fichier tronqué ou corrompu est un échec,
/// jamais une lecture hors du tampon.
class Reader
{
public:
    explicit Reader(std::span<const std::byte> bytes) : m_bytes{bytes} {}

    template <typename T> bool value(T& out)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        if (m_bytes.size() < sizeof(T))
        {
            return false;
        }
        std::memcpy(&out, m_bytes.data(), sizeof(T));
        m_bytes = m_bytes.subspan(sizeof(T));
        return true;
    }

    template <typename T> bool array(std::vector<T>& out)
    {
        std::uint64_t count = 0;
        if (!value(count) || count > m_bytes.size() / sizeof(T))
        {
            return false;
        }
        out.resize(static_cast<std::size_t>(count));
        // Un tableau vide a un data() nul, qu'aucun memcpy ne doit recevoir, même pour zéro octet
        // (UBSan, M4.3).
        if (!out.empty())
        {
            std::memcpy(out.data(), m_bytes.data(), out.size() * sizeof(T));
        }
        m_bytes = m_bytes.subspan(out.size() * sizeof(T));
        return true;
    }

    bool text(std::string& out)
    {
        std::vector<char> characters;
        if (!array(characters))
        {
            return false;
        }
        out.assign(characters.begin(), characters.end());
        return true;
    }

    [[nodiscard]] bool finished() const { return m_bytes.empty(); }

private:
    std::span<const std::byte> m_bytes;
};

/// Une valeur facultative : un octet de présence, puis la valeur.
template <typename T> void writeOptional(Writer& writer, const std::optional<T>& value)
{
    writer.value(static_cast<std::uint8_t>(value.has_value()));
    if (value)
    {
        writer.value(*value);
    }
}

template <typename T> bool readOptional(Reader& reader, std::optional<T>& out)
{
    std::uint8_t present = 0;
    if (!reader.value(present))
    {
        return false;
    }
    out.reset();
    if (present != 0)
    {
        T value{};
        if (!reader.value(value))
        {
            return false;
        }
        out = value;
    }
    return true;
}

std::unexpected<core::Error> cookedError(const std::filesystem::path& path, std::string_view what)
{
    return core::makeError(core::ErrorCode::InvalidData,
                           std::format("{} : {}", path.string(), what));
}

bool readBody(Reader& reader, Model& model)
{
    std::uint64_t meshCount = 0;
    if (!reader.value(meshCount))
    {
        return false;
    }
    for (std::uint64_t m = 0; m < meshCount; ++m)
    {
        ModelMesh& mesh = model.meshes.emplace_back();
        std::uint64_t primitiveCount = 0;
        if (!reader.text(mesh.name) || !reader.value(primitiveCount))
        {
            return false;
        }
        for (std::uint64_t p = 0; p < primitiveCount; ++p)
        {
            MeshPrimitive& primitive = mesh.primitives.emplace_back();
            if (!reader.array(primitive.vertices) || !reader.array(primitive.indices) ||
                !readOptional(reader, primitive.material) || !reader.array(primitive.joints) ||
                !reader.array(primitive.weights))
            {
                return false;
            }
        }
    }

    std::uint64_t nodeCount = 0;
    if (!reader.value(nodeCount))
    {
        return false;
    }
    for (std::uint64_t n = 0; n < nodeCount; ++n)
    {
        ModelNode& node = model.nodes.emplace_back();
        std::uint8_t joint = 0;
        if (!reader.text(node.name) || !reader.value(node.local) ||
            !readOptional(reader, node.mesh) || !readOptional(reader, node.parent) ||
            !reader.value(joint) || joint > 1)
        {
            return false;
        }
        node.joint = joint == 1;
    }

    std::uint64_t materialCount = 0;
    if (!reader.value(materialCount))
    {
        return false;
    }
    for (std::uint64_t i = 0; i < materialCount; ++i)
    {
        ModelMaterial& material = model.materials.emplace_back();
        if (!reader.value(material.baseColorFactor) ||
            !readOptional(reader, material.baseColorTexture))
        {
            return false;
        }
    }

    std::uint64_t imageCount = 0;
    if (!reader.value(imageCount))
    {
        return false;
    }
    for (std::uint64_t i = 0; i < imageCount; ++i)
    {
        std::uint32_t index = 0;
        Image image;
        if (!reader.value(index) || !reader.value(image.width) || !reader.value(image.height) ||
            !reader.array(image.rgba))
        {
            return false;
        }
        model.embeddedImages.emplace(index, std::move(image));
    }
    return reader.finished();
}

} // namespace

core::Result<void> writeCookedModel(const std::filesystem::path& path, const Model& model,
                                    std::uint64_t sourceHash)
{
    Writer writer;
    writer.value(Signature);
    writer.value(FormatVersion);
    writer.value(MeshEncoding::Raw);
    writer.value(CookerVersion);
    writer.value(sourceHash);

    writer.value(static_cast<std::uint64_t>(model.meshes.size()));
    for (const ModelMesh& mesh : model.meshes)
    {
        writer.text(mesh.name);
        writer.value(static_cast<std::uint64_t>(mesh.primitives.size()));
        for (const MeshPrimitive& primitive : mesh.primitives)
        {
            writer.array(std::span{primitive.vertices});
            writer.array(std::span{primitive.indices});
            writeOptional(writer, primitive.material);
            writer.array(std::span{primitive.joints});
            writer.array(std::span{primitive.weights});
        }
    }
    writer.value(static_cast<std::uint64_t>(model.nodes.size()));
    for (const ModelNode& node : model.nodes)
    {
        writer.text(node.name);
        writer.value(node.local);
        writeOptional(writer, node.mesh);
        writeOptional(writer, node.parent);
        // Un octet plutôt qu'un bool copié tel quel : relire un octet qui ne vaudrait ni 0 ni 1
        // dans un bool serait un comportement indéfini.
        writer.value(static_cast<std::uint8_t>(node.joint));
    }
    writer.value(static_cast<std::uint64_t>(model.materials.size()));
    for (const ModelMaterial& material : model.materials)
    {
        writer.value(material.baseColorFactor);
        writeOptional(writer, material.baseColorTexture);
    }
    writer.value(static_cast<std::uint64_t>(model.embeddedImages.size()));
    for (const auto& [index, image] : model.embeddedImages)
    {
        writer.value(index);
        writer.value(image.width);
        writer.value(image.height);
        writer.array(std::span{image.rgba});
    }

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream file{path, std::ios::binary};
    file.write(reinterpret_cast<const char*>(writer.bytes().data()),
               static_cast<std::streamsize>(writer.bytes().size()));
    if (!file)
    {
        return cookedError(path, "écriture impossible");
    }
    return {};
}

core::Result<Model> readCookedModel(const std::filesystem::path& path, std::uint64_t sourceHash)
{
    auto bytes = core::readFile(path);
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    Reader reader{*bytes};

    std::array<char, 4> signature{};
    std::uint32_t formatVersion = 0;
    MeshEncoding encoding{};
    std::uint32_t cookerVersion = 0;
    std::uint64_t cookedFrom = 0;
    if (!reader.value(signature) || signature != Signature || !reader.value(formatVersion) ||
        !reader.value(encoding) || !reader.value(cookerVersion) || !reader.value(cookedFrom))
    {
        return cookedError(path, "pas un .lvmesh");
    }
    if (formatVersion != FormatVersion || encoding != MeshEncoding::Raw)
    {
        return cookedError(path, std::format("format {} ou encodage {} inconnus", formatVersion,
                                             static_cast<std::uint32_t>(encoding)));
    }
    if (cookerVersion != CookerVersion || cookedFrom != sourceHash)
    {
        return cookedError(path, "périmé : la source ou le cuiseur ont changé");
    }

    Model model;
    if (!readBody(reader, model))
    {
        return cookedError(path, "tronqué ou corrompu");
    }
    return model;
}

} // namespace levain::assets
