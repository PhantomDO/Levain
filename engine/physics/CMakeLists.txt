find_package(glm CONFIG REQUIRED)
find_package(Jolt CONFIG REQUIRED)

add_library(levain_physics STATIC src/physics_world.cpp)
add_library(levain::physics ALIAS levain_physics)

target_include_directories(levain_physics PUBLIC include)

# Jolt est PRIVATE : seul src/ l'inclut, l'API ne rend que nos types et glm (ADR-0026, SPECS §7,
# tests/check_asset_libraries_visibility.cmake). Ses options de compilation (`JPH_*`, AVX2) viennent
# avec sa cible et ne s'appliquent qu'à nos sources : un écart avec la bibliothèque est rattrapé au
# démarrage par `JPH::VerifyJoltVersionID`.
target_link_libraries(levain_physics PUBLIC levain::core levain::scene glm::glm PRIVATE Jolt::Jolt)
