#pragma once

#include <nvrhi/nvrhi.h>

namespace levain::gpu
{

/// Les messages de NVRHI et de sa couche de validation, pour tous les backends : dans nos logs
/// (catégorie `nvrhi`), et une assertion sur toute erreur en Debug (règle n°4). Sans état, et avec
/// une durée de vie statique : il survit à tous les devices qui le référencent.
[[nodiscard]] nvrhi::IMessageCallback& nvrhiMessages();

} // namespace levain::gpu
