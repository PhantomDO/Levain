find_package(doctest CONFIG REQUIRED)

# Les tests du code CPU, qui tournent aussi en WebAssembly sous Node (ADR-0023, #182), puis ceux
# qui ont besoin d'une fenêtre, de l'entrée ou du GPU, natifs seulement jusqu'à leur portage (M4.6).
set(cpuTests
    allocator_test.cpp
    animation_test.cpp
    asset_ref_test.cpp
    assert_test.cpp
    character_ecs_test.cpp
    character_test.cpp
    collision_mesh_test.cpp
    cooked_test.cpp
    cooked_texture_test.cpp
    error_test.cpp
    file_test.cpp
    frame_time_test.cpp
    gltf_test.cpp
    image_test.cpp
    log_test.cpp
    physics_test.cpp
    registry_test.cpp
    scene_test.cpp
    version_test.cpp
    walk_test.cpp)
set(nativeTests
    app_models_test.cpp
    app_options_test.cpp
    app_test.cpp
    camera_control_test.cpp
    camera_test.cpp
    culling_test.cpp
    environment_test.cpp
    grass_test.cpp
    input_test.cpp
    light_clusters_test.cpp
    shadows_test.cpp
    stages_test.cpp
    terrain_test.cpp
    tangent_test.cpp
    process_test.cpp
    texture_test.cpp
    ui_test.cpp
    water_test.cpp
    webgpu_test.cpp
    window_test.cpp)
if(EMSCRIPTEN)
    add_executable(levain_tests doctest_main.cpp ${cpuTests})
    target_link_libraries(levain_tests PRIVATE
        levain::animation levain::assets levain::core levain::physics levain::scene plugin::character
        doctest::doctest)
    # NODERAWFS : Node lit le disque de la machine, où sont tests/data et les dossiers
    # temporaires des tests. La pile par défaut (64 Kio) est trop courte pour doctest et flecs.
    target_link_options(levain_tests PRIVATE
        -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1MB -sEXIT_RUNTIME=1)

    # Le cube du test de fumée dans un canvas, par le backend WebGPU (ADR-0023, #184) : la page
    # web_cube.html, que tools/web-smoke.sh ouvre dans Firefox et compare à data/cube.ppm. Les
    # shaders WGSL et la texture sont préchargés dans le système de fichiers d'Emscripten.
    add_executable(levain_web_cube web/web_cube.cpp)
    # LINK_DEPENDS : la page se régénère quand son modèle change, pas seulement le code.
    set_target_properties(levain_web_cube PROPERTIES SUFFIX ".html" LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
    target_link_libraries(levain_web_cube PRIVATE levain::assets levain::gpu levain::render)
    target_link_options(levain_web_cube PRIVATE
        -sALLOW_MEMORY_GROWTH=1
        "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
        "--preload-file=${LEVAIN_SHADER_DIR}@/shaders"
        "--preload-file=${CMAKE_CURRENT_SOURCE_DIR}/data/rgbw-2x2.png@/data/rgbw-2x2.png")
    add_dependencies(levain_web_cube levain_shaders)
else()
    add_executable(levain_tests doctest_main.cpp ${cpuTests} ${nativeTests})
    target_link_libraries(levain_tests PRIVATE
        levain::animation levain::app levain::assets levain::core levain::gpu levain::input levain::physics
        levain::platform levain::render levain::scene levain::ui plugin::character plugin::grass plugin::terrain
        plugin::water doctest::doctest)
    # La scène du lac de la démo (sandbox/src/lake_shore.hpp), que terrain_test.cpp simule sans GPU.
    target_include_directories(levain_tests PRIVATE ${PROJECT_SOURCE_DIR}/sandbox/src)
    # Le WGSL produit par le build, que webgpu_test.cpp fait compiler au backend WebGPU.
    target_compile_definitions(levain_tests PRIVATE LEVAIN_SHADER_DIR="${LEVAIN_SHADER_DIR}")
    add_dependencies(levain_tests levain_shaders)
endif()

# La macro qui génère le main de doctest ne doit s'appliquer qu'à une seule unité de
# compilation, sinon chaque fichier de test en produirait un. On la pose donc sur ce fichier
# précis plutôt que sur la cible entière, et depuis le build plutôt que depuis les sources.
set_source_files_properties(doctest_main.cpp
    PROPERTIES COMPILE_DEFINITIONS DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN)

# La version attendue vient du project() racine, pas d'une constante recopiée : le test
# échoue si le CMakeLists et le binaire divergent.
target_compile_definitions(levain_tests PRIVATE
    LEVAIN_EXPECTED_VERSION="${PROJECT_VERSION}"
    LEVAIN_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    LEVAIN_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
    LEVAIN_CMAKE_COMMAND="${CMAKE_COMMAND}")

# doctest_discover_tests lance le binaire à la compilation pour lister ses cas, et en
# déclare un par TEST_CASE à ctest. Une régression pointe donc le cas précis plutôt qu'un
# « levain_tests a échoué ». Fourni par le port vcpkg de doctest.
#
# Un cas que le filtre de ctest ne retrouve pas n'exécute rien, et doctest sort quand même sans
# erreur. C'est arrivé : un « ; » dans le nom d'un TEST_CASE est le séparateur des listes de CMake,
# et le cas devenait deux entrées vertes qui ne testaient rien (règle n°7). D'où l'échec quand
# doctest annonce zéro cas exécuté : `[|]` et non `|`, qui est une alternance en regex CMake et
# reconnaîtrait toute sortie. Le garde-fou lit le rapporteur console de doctest : un autre
# rapporteur (`JUNIT_OUTPUT_DIR`) ou `--quiet` le rendraient muet.
include(doctest)
# SDL_VIDEO_DRIVER=offscreen : window_test.cpp ouvre une vraie fenêtre, sans écran en CI.
doctest_discover_tests(levain_tests PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen"
    FAIL_REGULAR_EXPRESSION "test cases: 0 [|]")

# Le reste (benchmarks, contrôles des sources, rendu hors écran) ne concerne que le build natif.
if(EMSCRIPTEN)
    return()
endif()

# Benchmark : compilé par la CI pour qu'il ne pourrisse pas, mais pas lancé par ctest.
# Un chiffre de performance dépend de la machine ; il se mesure à la main sur la machine
# de référence et se consigne dans le journal (AGENTS.md, règle non négociable n°6).
add_executable(levain_bench allocator_bench.cpp)
target_link_libraries(levain_bench PRIVATE levain::core)

add_executable(levain_scene_bench scene_bench.cpp)
target_link_libraries(levain_scene_bench PRIVATE levain::scene)

# Le critère de M6.1 : 1 000 caisses en chute libre, aucun pas au-dessus de 4 ms (ADR-0026).
# Sa scène est celle de `levain_sandbox --view physics`, dans un en-tête du sandbox : le chiffre du
# banc est celui de la démo.
add_executable(levain_physics_bench physics_bench.cpp)
target_include_directories(levain_physics_bench PRIVATE ${PROJECT_SOURCE_DIR}/sandbox/src)
target_link_libraries(levain_physics_bench PRIVATE levain::physics levain::scene)

# SPECS §7 : flecs n'apparaît pas sous scene/. Un contrôle des sources plutôt que du link : les
# en-têtes de vcpkg sont visibles de tous les modules, un #include <flecs.h> compilerait partout.
add_test(NAME deps.flecs-visibility
         COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/check_flecs_visibility.cmake)

add_test(NAME deps.asset-libraries-visibility
         COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/check_asset_libraries_visibility.cmake)

# Sans LEVAIN_PROFILING, Tracy n'est pas dans le binaire (engine/core/include/levain/core/profile.hpp) :
# ni symbole, ni thread, ni socket. Le contrôle exige aussi des symboles levain::, sans quoi un nm
# muet le laisserait vert (règle n°7).
if(NOT LEVAIN_PROFILING AND NOT EMSCRIPTEN)
    add_test(NAME build.no-tracy COMMAND ${CMAKE_NM} -C $<TARGET_FILE:levain_tests>)
    set_tests_properties(build.no-tracy PROPERTIES
        FAIL_REGULAR_EXPRESSION "tracy::" PASS_REGULAR_EXPRESSION "levain::")
endif()

add_test(NAME assets.metas-committed
         COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/check_asset_metas.cmake)

# Le tri des lumières en clusters sur le GPU, comparé à sa référence CPU (ADR-0024), sur les deux
# backends.
add_executable(levain_light_clusters light_clusters_gpu.cpp)
target_link_libraries(levain_light_clusters PRIVATE levain::gpu levain::platform levain::render)
add_test(NAME gpu.light-clusters.vulkan COMMAND levain_light_clusters vulkan)
set_tests_properties(gpu.light-clusters.vulkan PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen")
add_test(NAME gpu.light-clusters.webgpu COMMAND levain_light_clusters webgpu)

# L'HDRI converti en cubemap, mips comprises (IBL, #130), sur les deux backends.
add_executable(levain_environment environment_gpu.cpp)
target_link_libraries(levain_environment PRIVATE levain::gpu levain::platform levain::render)
add_test(NAME gpu.environment.vulkan COMMAND levain_environment vulkan)
set_tests_properties(gpu.environment.vulkan PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen")
add_test(NAME gpu.environment.webgpu COMMAND levain_environment webgpu)

# L'UI d'ImGui dessinée par NVRHI, couleurs et découpe relues (ADR-0032), sur les deux backends.
add_executable(levain_ui_gpu ui_gpu.cpp)
target_link_libraries(levain_ui_gpu PRIVATE levain::gpu levain::platform levain::render levain::ui)
add_test(NAME gpu.ui.vulkan COMMAND levain_ui_gpu vulkan)
set_tests_properties(gpu.ui.vulkan PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen")
add_test(NAME gpu.ui.webgpu COMMAND levain_ui_gpu webgpu)

# ImGui reste dans ui/ et au-dessus, et SDL hors de ui/ (SPECS §7, ADR-0032).
add_test(NAME deps.imgui-visibility
         COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/check_imgui_visibility.cmake)

# Un programme qui ne relie que levain::gpu (gpu_alone.cpp) : une dépendance manquante ou mal ordonnée
# casse le build, comme elle casserait celui d'un jeu.
add_executable(levain_gpu_alone gpu_alone.cpp)
target_link_libraries(levain_gpu_alone PRIVATE levain::gpu)

# Tests de fumée du rendu (#16, M2.1) : dessinent une scène hors écran et comparent l'image à une
# référence. SDL_VIDEO_DRIVER=offscreen : pas d'écran en CI, la surface est « headless » (lavapipe).
add_executable(levain_smoke_render smoke_render.cpp)
target_link_libraries(levain_smoke_render PRIVATE
    levain::assets levain::gpu levain::render levain::platform)
target_compile_definitions(levain_smoke_render PRIVATE
    LEVAIN_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
# cube-instance : le cube de « cube », tourné par la pose de son instance ; même référence.
# lines : le cube de « cube » et trois lignes de debug, dont une cachée en partie par le cube.
foreach(scene triangle cube cube-instance lines shadow)
    add_test(NAME smoke.${scene} COMMAND levain_smoke_render ${scene})
    set_tests_properties(smoke.${scene} PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen")
endforeach()
# Les mêmes scènes par le backend WebGPU de NVRHI, sur Dawn : mêmes références (ADR-0023, #184).
foreach(scene triangle cube cube-instance lines shadow)
    add_test(NAME smoke.webgpu.${scene} COMMAND levain_smoke_render ${scene} webgpu)
endforeach()

# ADR-0018 : le moteur vu d'un jeu. La frontière des plugins, sur un moteur miniature qu'on
# configure trois fois ; les deux scénarios interdits doivent échouer avec le bon message, et non
# pour une autre raison.
foreach(case allowed engine-links-plugin undeclared-dependency)
    add_test(NAME cmake.plugins.${case}
             COMMAND ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_SOURCE_DIR}/cmake/plugin_boundaries
                     -B ${CMAKE_CURRENT_BINARY_DIR}/plugin_boundaries/${case} -G Ninja
                     -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -DCASE=${case}
                     -DLEVAIN_CMAKE_DIR=${PROJECT_SOURCE_DIR}/cmake)
endforeach()
set_tests_properties(cmake.plugins.engine-links-plugin PROPERTIES
    PASS_REGULAR_EXPRESSION "la cible du moteur fixture_core lie le plugin terrain")
set_tests_properties(cmake.plugins.undeclared-dependency PROPERTIES
    PASS_REGULAR_EXPRESSION "le plugin grass lie fixture_core sans l'avoir déclaré")

# Le manifeste vcpkg d'un jeu doit contenir celui du moteur.
add_test(NAME cmake.vcpkg-manifest
         COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR}
                 -DWORK=${CMAKE_CURRENT_BINARY_DIR}/vcpkg_manifest
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_vcpkg_manifest_test.cmake)
