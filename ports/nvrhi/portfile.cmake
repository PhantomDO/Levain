# Port overlay de NVRHI (ADR-0023) : celui de vcpkg, figé sur le même commit, avec deux changements.
#
#   1. add-webgpu-api.patch ajoute nvrhi::GraphicsAPI::WEBGPU : le rendu choisit le format de ses
#      shaders (SPIR-V, DXIL, WGSL) d'après l'API du device, et NVRHI ne connaît que les trois
#      siennes. Notre backend WebGPU, écrit dans engine/gpu, rend cette valeur.
#   2. Sous Emscripten, seul le cœur de NVRHI se compile (interfaces, suivi des états, couche de
#      validation) : ni Vulkan ni Direct3D n'existent dans le navigateur.
#
# À remettre au port officiel si NVRHI apprend un jour WebGPU (issue #37, close « not planned »).
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO NVIDIA-RTX/NVRHI
    REF 54100464714de88a5a5059d25808f5ccb914ad7d
    SHA512 56d5de1cc0840e29d8df976a5fe7b13d676c110ba24c09ff5e0caaa73f4aa56cc78d2ec2c31b1cb8da9f5b099c8b8598410792f8343a77ba928da28ba8146b1f
    HEAD_REF main
    PATCHES
        fix-vcpkg-deps.patch
        add-webgpu-api.patch
)

set(backendOptions "")
if(VCPKG_TARGET_IS_EMSCRIPTEN)
    list(APPEND backendOptions -DNVRHI_WITH_VULKAN=OFF)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DNVRHI_INSTALL=ON
        -DNVRHI_INSTALL_EXPORTS=ON
        -DNVRHI_WITH_NVAPI=OFF
        -DNVRHI_WITH_AFTERMATH=OFF
        -DNVRHI_WITH_RTXMU=OFF
        ${backendOptions}
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH "lib/cmake/nvrhi")
vcpkg_copy_pdbs()

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
