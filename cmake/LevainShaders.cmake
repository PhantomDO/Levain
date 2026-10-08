# La compilation des shaders Slang, pour le moteur (shaders/CMakeLists.txt) et pour ses plugins.
include_guard(GLOBAL)

# Tous les shaders, ceux du moteur comme ceux des plugins, vont dans ce dossier : `render::loadShader`
# les y trouve par leur nom, et la page web le précharge en entier.
set(LEVAIN_SHADER_DIR "${PROJECT_BINARY_DIR}/shaders" CACHE INTERNAL "Shaders compilés")
# Un plugin inclut le code d'éclairage du moteur (`#include "lighting.slang"`, ADR-0025).
set(LEVAIN_ENGINE_SHADERS "${PROJECT_SOURCE_DIR}/shaders" CACHE INTERNAL "Sources des shaders du moteur")

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

# Les réglages suivants vont dans le cache, préfixés : le module n'est lu qu'une fois, et la fonction
# sert aussi dans le dossier d'un plugin, qui n'hérite pas des variables de celui des shaders.

# slangc produit le DXIL en chargeant libdxcompiler.so, fournie par le port directx-dxc.
set(LEVAIN_DXC_LIBRARY_DIR "${VCPKG_INSTALLED_DIR}/${hostTriplet}/lib" CACHE INTERNAL "")

# Décalages de binding de NVRHI sous Vulkan (nvrhi::VulkanBindingOffsets) : les registres t, s, b
# et u de HLSL partagent un seul espace de bindings Vulkan, qu'on découpe en quatre plages.
set(LEVAIN_VULKAN_SHIFTS
    -fvk-t-shift 0 all -fvk-s-shift 128 all -fvk-b-shift 256 all -fvk-u-shift 384 all
    CACHE INTERNAL "")

# GLM range ses matrices par colonnes. Imposer la même disposition aux deux formats plutôt que de
# dépendre du défaut de slangc : une matrice lue transposée déplace tout sans erreur de validation.
set(LEVAIN_SHADER_FLAGS -matrix-layout-column-major -warnings-as-errors all -I "${LEVAIN_ENGINE_SHADERS}"
    CACHE INTERNAL "")

# levain_add_shader(triangle vertexMain vertex) produit triangle.vertexMain.spv, .dxil et .wgsl, depuis
# le fichier triangle.slang du dossier qui l'appelle, et ajoute ces fichiers à `shaderOutputs`, dont
# l'appelant fait une cible.
function(levain_add_shader name entry stage)
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/${name}.slang")
    set(spirv "${LEVAIN_SHADER_DIR}/${name}.${entry}.spv")
    set(dxil "${LEVAIN_SHADER_DIR}/${name}.${entry}.dxil")
    set(wgsl "${LEVAIN_SHADER_DIR}/${name}.${entry}.wgsl")

    add_custom_command(
        OUTPUT "${spirv}" "${dxil}" "${wgsl}"
        COMMAND "${LEVAIN_SLANGC}" "${source}" -entry ${entry} -stage ${stage}
                -target spirv ${LEVAIN_VULKAN_SHIFTS} ${LEVAIN_SHADER_FLAGS}
                -o "${spirv}" -depfile "${spirv}.d"
        COMMAND "${LEVAIN_SLANGC}" "${source}" -entry ${entry} -stage ${stage}
                -target dxil -profile sm_6_0 -dxc-path "${LEVAIN_DXC_LIBRARY_DIR}" ${LEVAIN_SHADER_FLAGS}
                -o "${dxil}"
        # Les mêmes décalages que sous Vulkan : WebGPU n'a qu'un espace de bindings par groupe,
        # comme un descriptor set, et le backend WebGPU de NVRHI les lira de même (ADR-0023).
        COMMAND "${LEVAIN_SLANGC}" "${source}" -entry ${entry} -stage ${stage}
                -target wgsl ${LEVAIN_VULKAN_SHIFTS} ${LEVAIN_SHADER_FLAGS}
                -o "${wgsl}"
        DEPENDS "${source}"
        DEPFILE "${spirv}.d"
        COMMENT "Slang : ${name}.${entry}"
        VERBATIM)

    # Le DXIL n'a pas encore de backend pour l'exécuter (ADR-0011). Le désassembler en test vérifie
    # au moins qu'il est bien formé, plutôt que de produire des fichiers que rien ne relit.
    add_test(NAME "dxil.${name}.${entry}" COMMAND "${LEVAIN_DXC}" -dumpbin "${dxil}")
    # dxc est un outil de l'hôte (ci-dessus) : le label de levain_add_host_test (tests/CMakeLists.txt).
    set_tests_properties("dxil.${name}.${entry}" PROPERTIES LABELS host)

    set(shaderOutputs ${shaderOutputs} "${spirv}" "${dxil}" "${wgsl}" PARENT_SCOPE)
endfunction()
