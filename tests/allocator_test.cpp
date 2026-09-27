#include <cstddef>
#include <cstdint>
#include <ostream>
#include <set>

#include <doctest/doctest.h>

#include "levain/core/linear_allocator.hpp"
#include "levain/core/pool_allocator.hpp"

using levain::core::alignUp;
using levain::core::isPowerOfTwo;
using levain::core::LinearAllocator;
using levain::core::PoolAllocator;

namespace
{

/// Vrai si `pointer` respecte `alignment`.
bool isAligned(const void* pointer, std::size_t alignment)
{
    return reinterpret_cast<std::uintptr_t>(pointer) % alignment == 0;
}

} // namespace

TEST_CASE("alignUp arrondit au multiple supérieur")
{
    CHECK(alignUp(0, 8) == 0);
    CHECK(alignUp(1, 8) == 8);
    CHECK(alignUp(8, 8) == 8);
    CHECK(alignUp(9, 8) == 16);
    CHECK(alignUp(17, 16) == 32);
}

TEST_CASE("isPowerOfTwo rejette zéro et les non-puissances")
{
    CHECK_FALSE(isPowerOfTwo(0));
    CHECK(isPowerOfTwo(1));
    CHECK(isPowerOfTwo(64));
    CHECK_FALSE(isPowerOfTwo(24));
}

TEST_CASE("l'allocateur linéaire respecte l'alignement demandé")
{
    LinearAllocator arena{1024};

    // Une allocation de 1 octet désaligne volontairement l'offset avant la suivante.
    CHECK(arena.allocate(1, 1) != nullptr);

    for (const std::size_t alignment : {2U, 4U, 8U, 16U, 64U})
    {
        void* const block = arena.allocate(8, alignment);

        REQUIRE(block != nullptr);
        CHECK(isAligned(block, alignment));
    }
}

TEST_CASE("l'allocateur linéaire renvoie nullptr au lieu de déborder")
{
    LinearAllocator arena{64};

    CHECK(arena.allocate(48, 1) != nullptr);
    CHECK(arena.allocate(32, 1) == nullptr); // ne tient pas
    CHECK(arena.usedBytes() == 48);          // un échec ne consomme rien
    CHECK(arena.allocate(16, 1) != nullptr); // ce qui tient passe encore
    CHECK(arena.remainingBytes() == 0);
}

TEST_CASE("le remplissage d'alignement compte dans le débordement")
{
    LinearAllocator arena{64};

    REQUIRE(arena.allocate(1, 1) != nullptr); // offset = 1

    // 64 octets tiendraient si l'offset était nul ; le remplissage jusqu'à 64 les pousse
    // dehors. C'est le cas que l'on rate en testant `size` seul.
    CHECK(arena.allocate(64, 64) == nullptr);
}

TEST_CASE("reset rend toute l'arène d'un coup")
{
    LinearAllocator arena{128};

    REQUIRE(arena.allocate(100, 1) != nullptr);
    CHECK(arena.usedBytes() == 100);

    arena.reset();

    CHECK(arena.usedBytes() == 0);
    CHECK(arena.allocate(128, 1) != nullptr);
}

TEST_CASE("le pool distribue des blocs distincts et alignés")
{
    PoolAllocator pool{32, 16, 4};
    std::set<void*> blocks;

    for (int i = 0; i < 4; ++i)
    {
        void* const block = pool.allocate();

        REQUIRE(block != nullptr);
        CHECK(isAligned(block, 16));
        CHECK(blocks.insert(block).second); // jamais deux fois le même
    }

    CHECK(pool.freeBlocks() == 0);
    CHECK(pool.allocate() == nullptr);
}

TEST_CASE("le pool aligne ses blocs au-delà de ce que garantit new")
{
    // new std::byte[] ne garantit que alignof(std::max_align_t) : 16 octets sur un PC 64 bits,
    // 8 en WebAssembly. Une ligne de cache en demande 64.
    PoolAllocator pool{64, 64, 8};

    for (int i = 0; i < 8; ++i)
    {
        CHECK(isAligned(pool.allocate(), 64));
    }
}

TEST_CASE("un bloc rendu au pool est redistribué")
{
    PoolAllocator pool{32, 16, 2};

    void* const first = pool.allocate();
    void* const second = pool.allocate();
    REQUIRE(pool.allocate() == nullptr);

    pool.deallocate(first);
    CHECK(pool.freeBlocks() == 1);

    void* const reused = pool.allocate();
    CHECK(reused == first); // LIFO : le dernier rendu repart en premier

    pool.deallocate(second);
    pool.deallocate(reused);
    CHECK(pool.freeBlocks() == 2);
}

TEST_CASE("rendre nullptr au pool ne fait rien")
{
    PoolAllocator pool{32, 16, 2};

    pool.deallocate(nullptr);

    CHECK(pool.freeBlocks() == 2);
}
