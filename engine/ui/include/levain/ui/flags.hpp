#pragma once

// Les drapeaux d'ImGui (M7.7, morceau 17b, revue) : un seul `withFlags`, au lieu d'une copie par
// fichier (context.cpp, bars.cpp, hierarchy.cpp, inspector.cpp, mode.cpp).

namespace levain::ui
{

/// Combine deux jeux de drapeaux d'ImGui, qui sont des `int` : en non signé, comme les bits qu'ils
/// sont. L'`|` sur des `int` signés échoue à `bugprone-signed-bitwise` (clang-tidy), seulement à
/// l'analyse, jamais au build.
[[nodiscard]] constexpr int withFlags(int flags, int added)
{
    return static_cast<int>(static_cast<unsigned>(flags) | static_cast<unsigned>(added));
}

} // namespace levain::ui
