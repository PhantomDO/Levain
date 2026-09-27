find_package(doctest CONFIG REQUIRED)

# Les tests du code CPU, qui tournent aussi en WebAssembly sous Node (ADR-0023, #182), puis ceux
# qui ont besoin d'une fenêtre, de l'entrée ou du GPU, natifs seulement jusqu'à leur portage (M4.6).
set(cpuTests
    allocator_test.cpp
    animation_test.cpp
    asset_ref_test.cpp
    assert_test.cpp
    cooked_test.cpp
    cooked_texture_test.cpp
    error_test.cpp
    file_test.cpp
    frame_time_test.cpp
    gltf_test.cpp
    image_test.cpp
    log_test.cpp
    registry_test.cpp
    scene_test.cpp
    version_test.cpp)
set(nativeTests
    camera_control_test.cpp
    camera_test.cpp
    input_test.cpp
    process_test.cpp
    texture_test.cpp
    window_test.cpp)
if(EMSCRIPTEN)
    add_executable(levain_tests doctest_main.cpp ${cpuTests})
    target_link_libraries(levain_tests PRIVATE
        levain::animation levain::assets levain::core levain::scene doctest::doctest)
    # NODERAWFS : Node lit le disque de la machine, où sont tests/data et les dossiers
    # temporaires des tests. La pile par défaut (64 Kio) est trop courte pour doctest et flecs.
    target_link_options(levain_tests PRIVATE
        -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1MB -sEXIT_RUNTIME=1)
else()
    add_executable(levain_tests doctest_main.cpp ${cpuTests} ${nativeTests})
    target_link_libraries(levain_tests PRIVATE
        levain::animation levain::assets levain::core levain::input levain::platform levain::render
        levain::scene doctest::doctest)
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
include(doctest)
# SDL_VIDEO_DRIVER=offscreen : window_test.cpp ouvre une vraie fenêtre, sans écran en CI.
doctest_discover_tests(levain_tests PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen")

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

# SPECS §7 : flecs n'apparaît pas sous scene/. Un contrôle des sources plutôt que du link : les
# en-têtes de vcpkg sont visibles de tous les modules, un #include <flecs.h> compilerait partout.
add_test(NAME deps.flecs-visibility
         COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/check_flecs_visibility.cmake)

add_test(NAME deps.asset-libraries-visibility
         COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/check_asset_libraries_visibility.cmake)

add_test(NAME assets.metas-committed
         COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/check_asset_metas.cmake)

# Tests de fumée du rendu (#16, M2.1) : dessinent une scène hors écran et comparent l'image à une
# référence. SDL_VIDEO_DRIVER=offscreen : pas d'écran en CI, la surface est « headless » (lavapipe).
add_executable(levain_smoke_render smoke_render.cpp)
target_link_libraries(levain_smoke_render PRIVATE
    levain::assets levain::gpu levain::render levain::platform)
target_compile_definitions(levain_smoke_render PRIVATE
    LEVAIN_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
foreach(scene triangle cube)
    add_test(NAME smoke.${scene} COMMAND levain_smoke_render ${scene})
    set_tests_properties(smoke.${scene} PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen")
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
