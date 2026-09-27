#pragma once

#include <cstddef>
#include <memory>

namespace levain::core
{

/// Allocateur de blocs de taille fixe, rendus dans n'importe quel ordre.
///
/// Là où `LinearAllocator` ne sait vider que tout d'un coup, celui-ci reprend un bloc à la
/// fois. Le prix est que tous les blocs font la même taille ; le gain est qu'allouer et
/// rendre sont en temps constant, sans recherche ni fragmentation. C'est ce qu'il faut pour
/// des objets nombreux, de même type et de durée de vie irrégulière : particules,
/// composants, nœuds.
///
/// La liste des blocs libres est chaînée **dans les blocs eux-mêmes** : un bloc libre
/// stocke l'adresse du suivant. Zéro métadonnée à côté, d'où la contrainte
/// `blockSize >= sizeof(void*)`.
class PoolAllocator
{
public:
    /// `blockSize` doit valoir au moins `sizeof(void*)` et `blockAlignment` être une
    /// puissance de deux au moins égale à `alignof(void*)` : ce sont des bugs sinon.
    PoolAllocator(std::size_t blockSize, std::size_t blockAlignment, std::size_t blockCount);

    /// Renvoie un bloc, ou **`nullptr`** s'il n'en reste aucun. Même raisonnement que pour
    /// `LinearAllocator::allocate` : pas de `Result` sur le chemin d'échec d'un allocateur.
    [[nodiscard]] void* allocate() noexcept;

    /// Rend un bloc obtenu de **ce** pool. Rendre autre chose est un bug, vérifié en Debug.
    void deallocate(void* block) noexcept;

    [[nodiscard]] std::size_t freeBlocks() const noexcept { return m_freeBlocks; }

    [[nodiscard]] std::size_t blockCount() const noexcept { return m_blockCount; }

    [[nodiscard]] std::size_t blockSize() const noexcept { return m_blockSize; }

private:
    [[nodiscard]] bool owns(const void* block) const noexcept;

    std::unique_ptr<std::byte[]> m_buffer;
    std::byte* m_blocks = nullptr; ///< Le premier bloc, aligné dans `m_buffer`.
    std::size_t m_blockSize;
    std::size_t m_blockCount;
    std::size_t m_freeBlocks;
    void* m_freeList = nullptr;
};

} // namespace levain::core
