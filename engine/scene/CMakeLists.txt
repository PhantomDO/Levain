find_package(flecs CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

add_library(levain_scene STATIC src/camera_control.cpp src/reflection.cpp src/scene.cpp)
add_library(levain::scene ALIAS levain_scene)

target_include_directories(levain_scene PUBLIC include)

# flecs est PUBLIC : c'est l'API du modèle objet, visible dans scene/ et dans ce qui est au-dessus
# (SPECS §7). Jamais dans core/, platform/, gpu/ ni render/ : tests/check_flecs_visibility.cmake.
target_link_libraries(levain_scene PUBLIC
    levain::core glm::glm
    $<IF:$<TARGET_EXISTS:flecs::flecs>,flecs::flecs,flecs::flecs_static>)
