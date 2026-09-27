find_package(glm CONFIG REQUIRED)
find_package(nvrhi CONFIG REQUIRED)

add_library(levain_render STATIC
    src/camera.cpp
    src/gpu_timer.cpp
    src/mesh.cpp
    src/mesh_pass.cpp
    src/readback.cpp
    src/shader.cpp
    src/skinning.cpp
    src/texture.cpp
    src/triangle.cpp)
add_library(levain::render ALIAS levain_render)

target_include_directories(levain_render PUBLIC include)

# NVRHI seul, jamais son backend Vulkan ni gpu/ : render dessine sur un nvrhi::IDevice, quel que
# soit l'API en dessous (SPECS §7).
target_link_libraries(levain_render PUBLIC levain::core nvrhi glm::glm)

# Dans le navigateur, les shaders sont dans le système de fichiers d'Emscripten, où l'exécutable
# les précharge (--preload-file <build>/shaders@/shaders).
if(EMSCRIPTEN)
    target_compile_definitions(levain_render PRIVATE LEVAIN_SHADER_DIR="/shaders")
else()
    target_compile_definitions(levain_render PRIVATE LEVAIN_SHADER_DIR="${LEVAIN_SHADER_DIR}")
endif()
add_dependencies(levain_render levain_shaders)
