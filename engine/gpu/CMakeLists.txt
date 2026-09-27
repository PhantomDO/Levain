find_package(nvrhi CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(VulkanHeaders CONFIG REQUIRED)
find_package(vk-bootstrap CONFIG REQUIRED)

add_library(levain_gpu STATIC src/device_vk.cpp src/nvrhi_messages.cpp src/swapchain_vk.cpp)
add_library(levain::gpu ALIAS levain_gpu)

target_include_directories(levain_gpu PUBLIC include)

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
