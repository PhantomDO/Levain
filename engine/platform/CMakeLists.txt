find_package(SDL3 CONFIG REQUIRED)

add_library(levain_platform STATIC src/input.cpp src/process.cpp src/input_script.cpp src/window.cpp)
add_library(levain::platform ALIAS levain_platform)

target_include_directories(levain_platform PUBLIC include)

# core est PUBLIC : Result<T> fait partie de l'API. SDL3 est PRIVATE : ses en-têtes ne sortent
# pas du module (ADR-0003, invariant n°1 du README).
target_link_libraries(levain_platform
    PUBLIC levain::core
    PRIVATE SDL3::SDL3)
