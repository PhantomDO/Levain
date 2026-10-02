# Compilation des shaders Slang au build (ADR-0005, amendement du 2026-09-21). Une commande par
# point d'entrée, qui produit le SPIR-V, le DXIL et le WGSL (le navigateur, ADR-0023) : seul un
# shader modifié est recompilé, et une erreur de shader fait échouer le build avec le message de
# slangc. Un shader qui sort du sous-ensemble portable (un ByteAddressBuffer, par exemple) échoue
# donc dès le build natif, bien avant d'arriver dans un navigateur.

# Les compilateurs de shaders tournent sur la machine qui compile : ce sont des outils de l'hôte
# (`"host": true` dans vcpkg.json), installés sous son triplet. Dans le build web, le triplet cible
# est wasm32-emscripten, et ses outils n'existent pas.
# En natif, l'hôte est la cible : VCPKG_HOST_TRIPLET n'est posé que par le preset web.
if(VCPKG_HOST_TRIPLET)
    set(hostTriplet "${VCPKG_HOST_TRIPLET}")
else()
    set(hostTriplet "${VCPKG_TARGET_TRIPLET}")
endif()
set(vcpkgTools "${VCPKG_INSTALLED_DIR}/${hostTriplet}/tools")

# NO_DEFAULT_PATH : un slangc installé sur le système contournerait la baseline figée (ADR-0007),
# comme le spdlog d'Arch en M0.3.
find_program(LEVAIN_SLANGC slangc PATHS "${vcpkgTools}/shader-slang" NO_DEFAULT_PATH REQUIRED)
find_program(LEVAIN_DXC dxc PATHS "${vcpkgTools}/directx-dxc" NO_DEFAULT_PATH REQUIRED)

# slangc produit le DXIL en chargeant libdxcompiler.so, fournie par le port directx-dxc.
set(dxcLibraryDir "${VCPKG_INSTALLED_DIR}/${hostTriplet}/lib")

# Décalages de binding de NVRHI sous Vulkan (nvrhi::VulkanBindingOffsets) : les registres t, s, b
# et u de HLSL partagent un seul espace de bindings Vulkan, qu'on découpe en quatre plages.
set(vulkanShifts
    -fvk-t-shift 0 all -fvk-s-shift 128 all -fvk-b-shift 256 all -fvk-u-shift 384 all)

# GLM range ses matrices par colonnes. Imposer la même disposition aux deux formats plutôt que de
# dépendre du défaut de slangc : une matrice lue transposée déplace tout sans erreur de validation.
set(commonFlags -matrix-layout-column-major -warnings-as-errors all)

set(LEVAIN_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}" CACHE INTERNAL "Shaders compilés")
set(shaderOutputs)

# levain_add_shader(triangle vertexMain vertex) produit triangle.vertexMain.spv, .dxil et .wgsl.
function(levain_add_shader name entry stage)
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/${name}.slang")
    set(spirv "${CMAKE_CURRENT_BINARY_DIR}/${name}.${entry}.spv")
    set(dxil "${CMAKE_CURRENT_BINARY_DIR}/${name}.${entry}.dxil")
    set(wgsl "${CMAKE_CURRENT_BINARY_DIR}/${name}.${entry}.wgsl")

    add_custom_command(
        OUTPUT "${spirv}" "${dxil}" "${wgsl}"
        COMMAND "${LEVAIN_SLANGC}" "${source}" -entry ${entry} -stage ${stage}
                -target spirv ${vulkanShifts} ${commonFlags}
                -o "${spirv}" -depfile "${spirv}.d"
        COMMAND "${LEVAIN_SLANGC}" "${source}" -entry ${entry} -stage ${stage}
                -target dxil -profile sm_6_0 -dxc-path "${dxcLibraryDir}" ${commonFlags}
                -o "${dxil}"
        # Les mêmes décalages que sous Vulkan : WebGPU n'a qu'un espace de bindings par groupe,
        # comme un descriptor set, et le backend WebGPU de NVRHI les lira de même (ADR-0023).
        COMMAND "${LEVAIN_SLANGC}" "${source}" -entry ${entry} -stage ${stage}
                -target wgsl ${vulkanShifts} ${commonFlags}
                -o "${wgsl}"
        DEPENDS "${source}"
        DEPFILE "${spirv}.d"
        COMMENT "Slang : ${name}.${entry}"
        VERBATIM)

    # Le DXIL n'a pas encore de backend pour l'exécuter (ADR-0011). Le désassembler en test vérifie
    # au moins qu'il est bien formé, plutôt que de produire des fichiers que rien ne relit.
    add_test(NAME "dxil.${name}.${entry}" COMMAND "${LEVAIN_DXC}" -dumpbin "${dxil}")

    set(shaderOutputs ${shaderOutputs} "${spirv}" "${dxil}" "${wgsl}" PARENT_SCOPE)
endfunction()

levain_add_shader(triangle vertexMain vertex)
levain_add_shader(triangle fragmentMain fragment)
levain_add_shader(mesh vertexMain vertex)
levain_add_shader(mesh fragmentMain fragment)
levain_add_shader(skinning computeMain compute)
levain_add_shader(light_clusters computeMain compute)
levain_add_shader(shadow vertexMain vertex)
levain_add_shader(tonemap vertexMain vertex)
levain_add_shader(tonemap fragmentMain fragment)

add_custom_target(levain_shaders ALL DEPENDS ${shaderOutputs})
