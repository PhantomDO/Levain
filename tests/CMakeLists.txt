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
    described_components_test.cpp
    environment_variable_test.cpp
    error_test.cpp
    file_test.cpp
    frame_time_test.cpp
    gltf_test.cpp
    image_test.cpp
    log_test.cpp
    physics_test.cpp
    reflection_test.cpp
    registry_test.cpp
    scene_test.cpp
    version_test.cpp
    walk_test.cpp)
set(nativeTests
    app_models_test.cpp
    app_options_test.cpp
    app_test.cpp
    app_ui_test.cpp
    camera_control_test.cpp
    camera_test.cpp
    culling_test.cpp
    editor_test.cpp
    environment_test.cpp
    grass_test.cpp
    input_test.cpp
    light_clusters_test.cpp
    shadows_test.cpp
    stages_test.cpp
    terrain_test.cpp
    tangent_test.cpp
    process_test.cpp
    shader_reload_test.cpp
    texture_test.cpp
    ui_test.cpp
    water_test.cpp
    webgpu_test.cpp
    window_test.cpp)
if(WIN32)
    # Les fonctions pures du routage des rapports de la CRT (engine/core/include/levain/core/crt_report.hpp).
    list(APPEND nativeTests crt_report_test.cpp)
endif()
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
        levain::animation levain::app levain::assets levain::core levain::editor levain::gpu levain::input
        levain::physics levain::platform levain::render levain::scene levain::ui plugin::character plugin::grass
        plugin::terrain plugin::water doctest::doctest)
    # La scène du lac de la démo (sandbox/src/lake_shore.hpp), que terrain_test.cpp simule sans GPU.
    target_include_directories(levain_tests PRIVATE ${PROJECT_SOURCE_DIR}/sandbox/src)
    # Le WGSL produit par le build, que webgpu_test.cpp fait compiler au backend WebGPU.
    target_compile_definitions(levain_tests PRIVATE LEVAIN_SHADER_DIR="${LEVAIN_SHADER_DIR}")
    add_dependencies(levain_tests levain_shaders)

    # Le programme que lancent les tests de runProcess : de la cible, comme levain_tests, donc le même sous
    # Windows que sous Linux (process_helper.cpp). Le cmake de l'hôte, qu'ils lançaient, est un binaire Linux.
    # Son chemin est compilé dans les tests, comme les dossiers de données : sous Windows, il ne marche que lancé
    # depuis la distro, jusqu'à ce que ctest donne les chemins (ADR-0035, #346).
    add_executable(levain_process_helper process_helper.cpp)
    target_link_libraries(levain_process_helper PRIVATE levain::core) # environmentVariable
    add_dependencies(levain_tests levain_process_helper)
    target_compile_definitions(levain_tests PRIVATE
        LEVAIN_PROCESS_HELPER="$<TARGET_FILE:levain_process_helper>")
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
    LEVAIN_DATA_DIR="${CMAKE_SOURCE_DIR}/data")

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
# Le pilote vidéo de SDL des tests qui ouvrent une fenêtre (window_test.cpp) ou une surface Vulkan. Sous Linux,
# offscreen : pas d'écran en CI, et sa surface Vulkan est VK_EXT_headless_surface, que lavapipe offre. Sous Windows,
# ni le lavapipe de mesa-dist-win (le runner de la CI) ni le pilote NVIDIA ne l'offrent : une vraie fenêtre, sur le
# bureau du runner (#346). Lancé depuis la distro, l'exe ne reçoit pas cette variable (WSLENV, build/GOTCHA.md).
if(WIN32)
    set(testVideo "SDL_VIDEO_DRIVER=windows")
else()
    set(testVideo "SDL_VIDEO_DRIVER=offscreen")
endif()
set(testProperties ENVIRONMENT "${testVideo}" FAIL_REGULAR_EXPRESSION "test cases: 0 [|]")
if(CMAKE_CROSSCOMPILING AND NOT EMSCRIPTEN)
    # Compilé pour Windows depuis Linux (ADR-0035), l'exécutable ne se lance pas sur la machine de build : seule
    # l'interop de WSL, ou un runner Windows, sait le faire. La découverte ne peut donc pas être un pas du build :
    # elle se fait au moment des tests, quand ctest lit sa liste, sur la machine qui lance l'exécutable (le mode
    # PRE_TEST de gtest_discover_tests, que doctest_discover_tests n'a pas). ctest inclut un fichier qui inclut à
    # son tour le script de doctest, copié à côté : il ne nomme ainsi aucun programme de la machine de build (pas le
    # cmake de Linux), et le même arbre se teste depuis la distro ou sur le runner Windows de la CI (#346). Ce
    # script s'arrête en FATAL_ERROR si l'exécutable ne répond pas, ce qui arrêterait ctest entier, `-L host` du
    # job Linux compris : la liste est donc demandée une fois avant, et un refus laisse le test rouge
    # levain_tests_NOT_DISCOVERED (zéro cas exécuté), sans quoi ctest annoncerait « No tests were found » et
    # sortirait en 0 (règle n°7). Le web, lui, a node pour émulateur : la découverte du build suffit.
    # Les TEST_* reprennent ceux que doctest_discover_tests passe à doctestAddTests.cmake, et
    # _DOCTEST_DISCOVER_TESTS_SCRIPT est une variable interne de doctest.cmake (doctest 2.5.3) : relire ce bloc
    # à chaque mise à jour de doctest. Une variable renommée se dit dès la configuration.
    if(NOT EXISTS "${_DOCTEST_DISCOVER_TESTS_SCRIPT}")
        message(FATAL_ERROR "doctest.cmake ne donne plus _DOCTEST_DISCOVER_TESTS_SCRIPT : relire la découverte "
                            "au moment des tests (tests/CMakeLists.txt).")
    endif()
    configure_file("${_DOCTEST_DISCOVER_TESTS_SCRIPT}" doctestAddTests.cmake COPYONLY)
    set(discoveredTests "${CMAKE_CURRENT_BINARY_DIR}/levain_tests_discovered.cmake")
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/levain_tests_discover.cmake" CONTENT
"execute_process(COMMAND \"$<TARGET_FILE:levain_tests>\" --list-test-cases
    RESULT_VARIABLE listed OUTPUT_QUIET ERROR_VARIABLE reason)
if(listed EQUAL 0)
    block()
        set(TEST_TARGET levain_tests)
        # Sous Windows, « /home/… » n'a pas de lecteur : ctest et l'exe le prennent sur celui du dossier courant,
        # mais execute_process le refuse en WORKING_DIRECTORY (« no such file or directory »), et en exécutable sous
        # un dossier courant \\wsl.localhost. REAL_PATH donne le lecteur, ou le partage.
        file(REAL_PATH \"$<TARGET_FILE:levain_tests>\" TEST_EXECUTABLE)
        file(REAL_PATH \"${CMAKE_CURRENT_BINARY_DIR}\" TEST_WORKING_DIR)
        # ctest lit ce fichier sans politique posée : sous Windows, execute_process décoderait la liste des cas
        # dans la page de code de la console, et les noms accentués ne désigneraient plus aucun cas.
        if(POLICY CMP0176)
            cmake_policy(SET CMP0176 NEW) # sortie des processus en UTF-8, celle du manifeste de l'exe
        endif()
        set(TEST_PROPERTIES \"${testProperties}\")
        set(TEST_LIST levain_tests_TESTS)
        set(CTEST_FILE \"${discoveredTests}\")
        include(\"${CMAKE_CURRENT_BINARY_DIR}/doctestAddTests.cmake\")
    endblock()
    include(\"${discoveredTests}\")
else()
    message(WARNING \"levain_tests : la découverte des cas a échoué : \${listed} \${reason}\")
    add_test(levain_tests_NOT_DISCOVERED \"$<TARGET_FILE:levain_tests>\" --test-case=levain_tests_NOT_DISCOVERED)
    set_tests_properties(levain_tests_NOT_DISCOVERED PROPERTIES FAIL_REGULAR_EXPRESSION \"test cases: 0 [|]\")
endif()
")
    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${CMAKE_CURRENT_BINARY_DIR}/levain_tests_discover.cmake")
else()
    doctest_discover_tests(levain_tests PROPERTIES ${testProperties})
endif()

# Le reste (benchmarks, contrôles des sources, rendu hors écran) ne concerne que le build natif.
if(EMSCRIPTEN)
    return()
endif()

# Le label « host » : les tests dont la COMMANDE est un outil de l'hôte (cmake -P, Python, la lecture des symboles
# par nm ou llvm-pdbutil, les cmake.plugins.* qui configurent un projet). Compilé pour Windows depuis Linux
# (ADR-0035), ils ne se lancent pas sur un runner Windows, et seul l'hôte les comprend : le job Linux les passe
# (`ctest -L host`), le runner Windows lance tout le reste, les programmes de la cible (`ctest -LE host`). Un test
# qui lance un programme de la cible, même compilé ici, n'a pas ce label. Passer par cette fonction plutôt que par
# add_test : un test de l'hôte sans label serait lancé, et rouge, sur le runner Windows. Les dxil.* de
# levain_add_shader (cmake/LevainShaders.cmake) y sont aussi : dxc est un outil de l'hôte.
function(levain_add_host_test name)
    add_test(NAME ${name} COMMAND ${ARGN})
    set_tests_properties(${name} PROPERTIES LABELS host)
endfunction()

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

# Les refus de la réflexion à l'import (ADR-0034), un cas par lancement : la sortie doit nommer la
# struct et le champ. PASS_REGULAR_EXPRESSION, jamais WILL_FAIL, qui passerait sur n'importe quel
# plantage ; et FAIL_REGULAR_EXPRESSION, car CTest ignore alors le code de sortie : un refus qui
# laisserait continuer le programme irait jusqu'à « aucun refus » (règle n°7).
add_executable(levain_reflection_refusals reflection_refusals.cpp)
target_link_libraries(levain_reflection_refusals PRIVATE levain::scene)
set(reflectionRefusals optional glm-leaf byte empty-range inverted-range nan-range two-ranges
    range-unknown-field read-only-then-authored authored-then-read-only)
set(reflectionRefusalOutputs
    "« maybe » de refusals[.]WithOptional a un type sans réflexion"
    "« offset » de refusals[.]WithLeaf a un type sans réflexion"
    "« raw » de refusals[.]WithByte a un type sans réflexion"
    "borne \\[1, 1\\] du champ « speed » de refusals[.]Ranged est vide ou inversée"
    "borne \\[2, 1\\] du champ « speed » de refusals[.]Ranged est vide ou inversée"
    "borne \\[nan, 1\\] du champ « speed » de refusals[.]Ranged est vide ou inversée"
    "« speed » de refusals[.]Ranged a deux bornes"
    "range sur le champ au décalage 4 de refusals[.]HandMade, absent de sa description"
    "refusals[.]Ranged est déclaré par describe et par describeAuthored"
    "refusals[.]Ranged est déclaré par describe et par describeAuthored")
foreach(refusal output IN ZIP_LISTS reflectionRefusals reflectionRefusalOutputs)
    add_test(NAME scene.reflection-refuses.${refusal}
             COMMAND levain_reflection_refusals ${refusal})
    set_tests_properties(scene.reflection-refuses.${refusal} PROPERTIES
        PASS_REGULAR_EXPRESSION "${output}" FAIL_REGULAR_EXPRESSION "aucun refus")
endforeach()

# L'échelle de liaisons de la réflexion est écrite par un script : le fichier versionné doit rester
# sa sortie, sinon le test échoue (règle n°7).
find_package(Python3 REQUIRED COMPONENTS Interpreter)
levain_add_host_test(scene.field-ladder
    ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/generate_field_ladder.py --check
    ${PROJECT_SOURCE_DIR}/engine/scene/include/levain/scene/detail/field_ladder.inc)

# SPECS §7 : flecs n'apparaît pas sous scene/. Un contrôle des sources plutôt que du link : les
# en-têtes de vcpkg sont visibles de tous les modules, un #include <flecs.h> compilerait partout.
levain_add_host_test(deps.flecs-visibility
    ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/check_flecs_visibility.cmake)

levain_add_host_test(deps.asset-libraries-visibility
    ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/check_asset_libraries_visibility.cmake)

# Les symboles d'un exécutable. Hors de Windows, `nm -C` ; sous Windows (ADR-0035), ils sont dans le PDB, que lit
# llvm-pdbutil, celui du LLVM du compilateur (lld-link a écrit le PDB), et check_pdb_symbols.cmake les démêle.
# FORBID : un symbole qui contient ce texte fait échouer le contrôle. REQUIRE : il en faut au moins un, sans quoi
# un nm ou un PDB muet laisserait le contrôle vert sans rien lire (règle n°7). Des textes simples, sans caractère
# spécial de regex : nm les passe à ctest, le script les cherche tels quels.
if(WIN32)
    cmake_path(GET CMAKE_CXX_COMPILER PARENT_PATH llvmBinDir)
    find_program(LEVAIN_PDBUTIL llvm-pdbutil HINTS "${llvmBinDir}" NO_DEFAULT_PATH REQUIRED)
    find_program(LEVAIN_UNDNAME llvm-undname HINTS "${llvmBinDir}" NO_DEFAULT_PATH REQUIRED)
endif()
function(levain_add_symbol_check name target)
    cmake_parse_arguments(PARSE_ARGV 2 check "" "FORBID;REQUIRE" "")
    if(WIN32)
        levain_add_host_test(${name}
            ${CMAKE_COMMAND} -DPDBUTIL=${LEVAIN_PDBUTIL} -DUNDNAME=${LEVAIN_UNDNAME}
            -DPDB=$<TARGET_PDB_FILE:${target}> -DFORBID=${check_FORBID} -DREQUIRE=${check_REQUIRE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_pdb_symbols.cmake)
    else()
        levain_add_host_test(${name} ${CMAKE_NM} -C $<TARGET_FILE:${target}>)
        set_tests_properties(${name} PROPERTIES PASS_REGULAR_EXPRESSION "${check_REQUIRE}")
        if(check_FORBID)
            set_tests_properties(${name} PROPERTIES FAIL_REGULAR_EXPRESSION "${check_FORBID}")
        endif()
    endif()
endfunction()

# Sans LEVAIN_PROFILING, Tracy n'est pas dans le binaire (engine/core/include/levain/core/profile.hpp) :
# ni symbole, ni thread, ni socket. Le contrôle exige aussi des symboles levain::, sans quoi un contrôle
# muet le laisserait vert (règle n°7).
if(NOT LEVAIN_PROFILING AND NOT EMSCRIPTEN)
    levain_add_symbol_check(build.no-tracy levain_tests FORBID "tracy::" REQUIRE "levain::")
endif()

# Le jeu livré n'embarque pas l'éditeur (ADR-0034) : aucun symbole levain::editor:: dans le sandbox.
# Le contrôle exige des symboles levain::app::, et le même contrôle sur l'exécutable éditeur doit en
# trouver : sans quoi un motif périmé le laisserait vert sans rien lire (règle n°7).
if(NOT EMSCRIPTEN)
    levain_add_symbol_check(build.no-editor levain_sandbox FORBID "levain::editor::" REQUIRE "levain::app::")
    levain_add_symbol_check(build.editor-symbols levain_sandbox_editor REQUIRE "levain::editor::")
endif()

levain_add_host_test(assets.metas-committed
    ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/check_asset_metas.cmake)

# Le tri des lumières en clusters sur le GPU, comparé à sa référence CPU (ADR-0024), sur les deux
# backends.
add_executable(levain_light_clusters light_clusters_gpu.cpp)
target_link_libraries(levain_light_clusters PRIVATE levain::gpu levain::platform levain::render)
add_test(NAME gpu.light-clusters.vulkan COMMAND levain_light_clusters vulkan)
set_tests_properties(gpu.light-clusters.vulkan PROPERTIES ENVIRONMENT "${testVideo}")
add_test(NAME gpu.light-clusters.webgpu COMMAND levain_light_clusters webgpu)

# L'HDRI converti en cubemap, mips comprises (IBL, #130), sur les deux backends.
add_executable(levain_environment environment_gpu.cpp)
target_link_libraries(levain_environment PRIVATE levain::gpu levain::platform levain::render)
add_test(NAME gpu.environment.vulkan COMMAND levain_environment vulkan)
set_tests_properties(gpu.environment.vulkan PROPERTIES ENVIRONMENT "${testVideo}")
add_test(NAME gpu.environment.webgpu COMMAND levain_environment webgpu)

# L'UI d'ImGui dessinée par NVRHI, couleurs et découpe relues (ADR-0032), sur les deux backends.
add_executable(levain_ui_gpu ui_gpu.cpp)
target_link_libraries(levain_ui_gpu PRIVATE levain::gpu levain::platform levain::render levain::ui)
add_test(NAME gpu.ui.vulkan COMMAND levain_ui_gpu vulkan)
set_tests_properties(gpu.ui.vulkan PROPERTIES ENVIRONMENT "${testVideo}")
add_test(NAME gpu.ui.webgpu COMMAND levain_ui_gpu webgpu)

# ImGui reste dans ui/ et au-dessus, et SDL hors de ui/ (SPECS §7, ADR-0032).
levain_add_host_test(deps.imgui-visibility
    ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/check_imgui_visibility.cmake)

# Un programme qui ne relie que levain::gpu (gpu_alone.cpp) : une dépendance manquante ou mal ordonnée
# casse le build, comme elle casserait celui d'un jeu.
add_executable(levain_gpu_alone gpu_alone.cpp)
target_link_libraries(levain_gpu_alone PRIVATE levain::gpu)

# En Debug, plus de fenêtre de la CRT ni d'une exception quand un programme échoue sous Windows
# (engine/core/src/crt_report.cpp, ADR-0035). Le probe ne lie que core et n'appelle rien : chaque scénario doit
# s'arrêter vite, avec un code non nul et son message sur stderr, ce que seul le routage de core, tiré par l'éditeur
# de liens, lui donne (le piège des bibliothèques statiques). En Release, rien ne change : `error-mode` vérifie
# qu'aucun réglage n'y est posé. Les autres systèmes n'ont pas ces fenêtres : rien à y contrôler, ce n'est pas un test
# sauté en silence. Un scénario absent de la table du probe pour ce build est une erreur (usage, code 64).
# `debugger.*` : le probe, débogueur de l'enfant, exige que le routage lui rende la main par un point d'arrêt.
if(WIN32)
    add_executable(levain_crt_report_probe crt_report_probe.cpp)
    target_link_libraries(levain_crt_report_probe PRIVATE levain::core)
    set(crtReportTests error-mode)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND crtReportTests stl-subscript crt-assert c-assert abort uncaught-throw raise-sigabrt null-write
             breakpoint fastfail debugger.crt-assert debugger.abort)
    endif()
    foreach(test IN LISTS crtReportTests)
        # debugger.<scénario> : `--debugger <scénario>`.
        string(REGEX REPLACE "^debugger[.](.*)$" "--debugger;\\1" arguments "${test}")
        add_test(NAME crt.report.${test} COMMAND levain_crt_report_probe ${arguments})
        # Le probe juge à 10 s ; ce délai-ci n'est que le filet d'un probe lui-même bloqué.
        set_tests_properties(crt.report.${test} PROPERTIES TIMEOUT 30)
    endforeach()
endif()

# Tests de fumée du rendu (#16, M2.1) : dessinent une scène hors écran et comparent l'image à une
# référence, sans écran en CI Linux (testVideo, plus haut).
add_executable(levain_smoke_render smoke_render.cpp)
target_link_libraries(levain_smoke_render PRIVATE
    levain::assets levain::gpu levain::render levain::platform)
target_compile_definitions(levain_smoke_render PRIVATE
    LEVAIN_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
# cube-instance : le cube de « cube », tourné par la pose de son instance ; même référence.
# lines : le cube de « cube » et trois lignes de debug, dont une cachée en partie par le cube.
foreach(scene triangle cube cube-instance lines shadow)
    add_test(NAME smoke.${scene} COMMAND levain_smoke_render ${scene})
    set_tests_properties(smoke.${scene} PROPERTIES ENVIRONMENT "${testVideo}")
endforeach()
# Les mêmes scènes par le backend WebGPU de NVRHI, sur Dawn : mêmes références (ADR-0023, #184).
foreach(scene triangle cube cube-instance lines shadow)
    add_test(NAME smoke.webgpu.${scene} COMMAND levain_smoke_render ${scene} webgpu)
endforeach()

# ADR-0018 et ADR-0034 : le moteur vu d'un jeu. La frontière des plugins et de l'éditeur, sur un
# moteur miniature qu'on configure une fois par scénario ; les scénarios interdits doivent échouer
# avec le bon message, et non pour une autre raison.
#
# Compilé pour Windows depuis Linux (ADR-0035), le mini-moteur reçoit la toolchain de ce build : sans elle, il
# retomberait sur clang-cl nu, qui ne trouve ni la STL ni le SDK de Microsoft, et la configuration échouerait avant
# d'avoir vérifié quoi que ce soit. La toolchain de vcpkg, avec son triplet et sa toolchain chaînée, lit
# LEVAIN_WINSYSROOT dans l'environnement que ctest transmet. Sans compilation croisée, rien ne change.
set(pluginBoundariesToolchain)
# Un arbre déjà configuré garde son compilateur et ses options : changer de toolchain n'y change rien, et la
# détection du compilateur échoue (vu en passant d'un build sans toolchain à celui-ci). Ils repartent de zéro.
file(REMOVE_RECURSE "${CMAKE_CURRENT_BINARY_DIR}/plugin_boundaries")
if(CMAKE_CROSSCOMPILING)
    set(pluginBoundariesToolchain
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE} -DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}
        -DVCPKG_HOST_TRIPLET=${VCPKG_HOST_TRIPLET}
        -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=${VCPKG_CHAINLOAD_TOOLCHAIN_FILE})
endif()
foreach(case allowed engine-links-plugin undeclared-dependency editor-links-plugin engine-links-editor
             plugin-links-editor no-editor)
    levain_add_host_test(cmake.plugins.${case}
        ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_SOURCE_DIR}/cmake/plugin_boundaries
        -B ${CMAKE_CURRENT_BINARY_DIR}/plugin_boundaries/${case} -G Ninja
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} ${pluginBoundariesToolchain} -DCASE=${case}
        -DLEVAIN_CMAKE_DIR=${PROJECT_SOURCE_DIR}/cmake)
endforeach()
set_tests_properties(cmake.plugins.engine-links-plugin PROPERTIES
    PASS_REGULAR_EXPRESSION "la cible du moteur fixture_core lie le plugin terrain")
set_tests_properties(cmake.plugins.undeclared-dependency PROPERTIES
    PASS_REGULAR_EXPRESSION "le plugin grass lie fixture_core sans l'avoir déclaré")
set_tests_properties(cmake.plugins.editor-links-plugin PROPERTIES
    PASS_REGULAR_EXPRESSION "la cible de l'éditeur levain_editor lie le plugin terrain")
set_tests_properties(cmake.plugins.engine-links-editor PROPERTIES
    PASS_REGULAR_EXPRESSION "la cible du moteur fixture_core lie l'éditeur levain_editor")
set_tests_properties(cmake.plugins.plugin-links-editor PROPERTIES
    PASS_REGULAR_EXPRESSION "le plugin water lie l'éditeur levain_editor")
# CMake replie son message selon la longueur du chemin, différente en CI : chaque espace peut y
# devenir un retour à la ligne.
set_tests_properties(cmake.plugins.no-editor PROPERTIES
    PASS_REGULAR_EXPRESSION "aucune cible sous[\n ]+[^\n ]*/editor[\n ]+:[\n ]+le[\n ]+contrôle")

# Le manifeste vcpkg d'un jeu doit contenir celui du moteur.
levain_add_host_test(cmake.vcpkg-manifest
    ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR} -DWORK=${CMAKE_CURRENT_BINARY_DIR}/vcpkg_manifest
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_vcpkg_manifest_test.cmake)
