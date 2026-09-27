#include "levain/core/pool_allocator.hpp"

#include <cstdint>
#include <cstring>

#include "levain/core/assert.hpp"
#include "levain/core/linear_allocator.hpp"

namespace levain::core
{
namespace
{

/// Lit et écrit le chaînon stocké dans un bloc libre.
///
/// `std::memcpy` plutôt qu'un `reinterpret_cast` déréférencé : écrire un `void*` dans des
/// octets bruts et le relire autrement viole les règles d'aliasing. Le compilateur ramène
/// ces deux fonctions à un simple accès mémoire.
void* readNext(const void* block) noexcept
{
    void* next = nullptr;
    std::memcpy(static_cast<void*>(&next), block, sizeof(next));

    return next;
}

void writeNext(void* block, void* next) noexcept
{
    std::memcpy(block, static_cast<const void*>(&next), sizeof(next));
}

} // namespace

PoolAllocator::PoolAllocator(std::size_t blockSize, std::size_t blockAlignment,
                             std::size_t blockCount)
    : m_blockSize{alignUp(blockSize, blockAlignment)}, m_blockCount{blockCount},
      m_freeBlocks{blockCount}
{
    LEVAIN_ASSERT(blockSize >= sizeof(void*),
                  "un bloc doit pouvoir contenir le chaînon de la liste des libres");
    LEVAIN_ASSERT(isPowerOfTwo(blockAlignment), "l'alignement doit être une puissance de deux");
    LEVAIN_ASSERT(blockAlignment >= alignof(void*),
                  "l'alignement doit convenir au chaînon stocké dans le bloc");
    LEVAIN_ASSERT(blockCount > 0, "un pool vide n'a pas de sens");

    // new std::byte[] ne garantit que alignof(std::max_align_t) : 16 octets sur un PC 64 bits,
    // 8 en WebAssembly. On réserve de quoi décaler le premier bloc jusqu'à l'alignement demandé,
    // comme LinearAllocator aligne ses adresses réelles.
    m_buffer = std::make_unique<std::byte[]>((m_blockSize * m_blockCount) + blockAlignment - 1);
    const auto base = reinterpret_cast<std::uintptr_t>(m_buffer.get());
    m_blocks = m_buffer.get() + (alignUp(base, blockAlignment) - base);

    // Chaînage initial : chaque bloc pointe vers le suivant, le dernier vers nullptr.
    // Parcouru à l'envers pour que la liste sorte dans l'ordre croissant des adresses,
    // ce qui rend les premières allocations contiguës et donc amies du cache.
    for (std::size_t index = m_blockCount; index-- > 0;)
    {
        std::byte* const block = m_blocks + (index * m_blockSize);
        writeNext(block, m_freeList);
        m_freeList = block;
    }
}

void* PoolAllocator::allocate() noexcept
{
    if (m_freeList == nullptr)
    {
        return nullptr;
    }

    void* const block = m_freeList;
    m_freeList = readNext(block);
    --m_freeBlocks;

    return block;
}

void PoolAllocator::deallocate(void* block) noexcept
{
    if (block == nullptr)
    {
        return;
    }

    LEVAIN_ASSERT(owns(block), "ce bloc ne vient pas de ce pool");

    writeNext(block, m_freeList);
    m_freeList = block;
    ++m_freeBlocks;
}

bool PoolAllocator::owns(const void* block) const noexcept
{
    const auto* const bytes = static_cast<const std::byte*>(block);
    const std::byte* const begin = m_blocks;
    const std::byte* const end = begin + (m_blockSize * m_blockCount);

    if (bytes < begin || bytes >= end)
    {
        return false;
    }

    // Dans les bornes ne suffit pas : un pointeur au milieu d'un bloc passerait, et le
    // rendre corromprait silencieusement la liste des libres.
    return static_cast<std::size_t>(bytes - begin) % m_blockSize == 0;
}

} // namespace levain::core
