// Un programme qui ne relie que levain::gpu, comme le fait un jeu qui n'en veut pas plus (Rando) :
// gpu doit apporter tout ce qu'il appelle, et dans le bon ordre. Le reste du dépôt relie aussi
// render, qui ramène NVRHI une seconde fois et masquait un ordre faux (nvrhi avant nvrhi_vk).
// Compilé, jamais lancé : c'est l'édition de liens qui échoue.

#include "levain/gpu/device.hpp"

int main()
{
    // volatile : le compilateur garde la référence, donc l'éditeur de liens cherche le symbole.
    auto* volatile create = &levain::gpu::createGpuDevice;
    return create == nullptr ? 1 : 0;
}
