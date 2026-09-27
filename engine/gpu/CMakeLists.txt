find_package(Dawn CONFIG REQUIRED)
find_package(nvrhi CONFIG REQUIRED)

# Le backend WebGPU de NVRHI (src/webgpu/, ADR-0023) se compile partout : dans le navigateur, et en
# natif sur Dawn pour le développer et le tester.
add_library(levain_gpu STATIC src/nvrhi_messages.cpp src/webgpu/bindings.cpp src/webgpu/conversions.cpp
                              src/webgpu/commandlist.cpp src/webgpu/device.cpp
                              src/webgpu/pipelines.cpp)
add_library(levain::gpu ALIAS levain_gpu)

target_include_directories(levain_gpu PUBLIC include)

# Dawn (webgpu.h) est PRIVATE comme Vulkan : le backend WebGPU ne sort du module que par des
# interfaces NVRHI.
target_link_libraries(levain_gpu PRIVATE levain::core dawn::webgpu_dawn PUBLIC nvrhi)

if(EMSCRIPTEN)
    return()
endif()

# En natif : le device Vulkan et sa swapchain, et la création du device WebGPU hors écran, qui
# attend Dawn sur place (le navigateur aura la sienne, asynchrone, #184 partie C).
find_package(SDL3 CONFIG REQUIRED)
find_package(VulkanHeaders CONFIG REQUIRED)
find_package(vk-bootstrap CONFIG REQUIRED)

target_sources(levain_gpu PRIVATE src/device_vk.cpp src/swapchain_vk.cpp src/webgpu/create.cpp)

# platform et nvrhi sont PUBLIC : ils font partie de l'API (device.hpp). Le backend Vulkan de
# NVRHI, vk-bootstrap, les en-têtes Vulkan et SDL restent PRIVATE : ils ne sortent pas du module.
# nvrhi_vk est listé avant nvrhi, dont il appelle le code : l'éditeur de liens ne parcourt les
# bibliothèques statiques qu'une fois, dans l'ordre.
target_link_libraries(levain_gpu
    PRIVATE nvrhi_vk vk-bootstrap::vk-bootstrap Vulkan::Headers SDL3::SDL3
    PUBLIC levain::platform nvrhi)

# Doit correspondre à NVRHI, qui compile Vulkan-Hpp en mode dispatcher dynamique
# (src/vulkan/vulkan-backend.h) : sinon le dispatcher défini dans device_vk.cpp n'est pas le sien.
target_compile_definitions(levain_gpu PRIVATE VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
