#pragma once

#include <array>
#include <cstdint>

namespace levain::physics
{

/// Les couches de collision, une table fixe du moteur (ADR-0026). Un jeu qui en voudrait d'autres
/// demandera un ADR : les 32 couches nommées par le projet, à la Unity, ont été écartées.
enum class Layer : std::uint8_t
{
    Static,    ///< Le décor, qui ne bouge pas.
    Dynamic,   ///< Ce que la simulation déplace : caisses, rochers qui roulent.
    Character, ///< Le personnage (M6.3).
    Sensor,    ///< Les volumes déclencheurs : ils détectent sans arrêter (M6.2).
    Debris,    ///< Ce qui tombe pour le décor, sans gêner le personnage.
};

/// Le nombre de couches : la taille de la matrice.
inline constexpr std::uint8_t LayerCount = 5;
static_assert(static_cast<std::uint8_t>(Layer::Debris) + 1 == LayerCount,
              "une couche ajoutée doit agrandir la matrice");

/// Qui touche qui. La matrice est **symétrique** (un test le vérifie) : si A touche B sans que B
/// touche A, Jolt testerait la paire ou non selon l'ordre où il la rencontre.
///
/// Deux paires vides qui surprennent : le décor ne se teste pas contre lui-même (rien n'y bouge),
/// et un volume déclencheur n'arrête personne, il signale.
constexpr bool layersCollide(Layer first, Layer second)
{
    // Une ligne par couche, un bit par couche touchée, dans l'ordre de l'enum.
    constexpr std::array<std::uint8_t, LayerCount> Matrix = {
        0b10110, // Static    : Dynamic, Character, Debris
        0b11111, // Dynamic   : tout
        0b01011, // Character : Static, Dynamic, Sensor
        0b00110, // Sensor    : Dynamic, Character
        0b00011, // Debris    : Static, Dynamic
    };
    const auto row = static_cast<std::uint8_t>(first);
    const auto column = static_cast<std::uint8_t>(second);
    return ((static_cast<unsigned>(Matrix[row]) >> column) & 1u) != 0;
}

} // namespace levain::physics
