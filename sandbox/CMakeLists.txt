add_executable(levain_sandbox src/main.cpp src/shader_reload.cpp)
target_link_libraries(levain_sandbox PRIVATE
    levain::animation levain::assets levain::core levain::platform levain::gpu levain::input
    levain::render levain::scene)

# L'explorer web de flecs, en Debug seulement : en Release, le code qui l'active n'est pas compilé,
# et aucun serveur n'écoute.
target_compile_definitions(levain_sandbox PRIVATE $<$<CONFIG:Debug>:LEVAIN_ENABLE_EXPLORER=1>)

# Dans le navigateur (ADR-0023, #186) : une page HTML, et un système de fichiers préchargé. data/,
# les shaders WGSL, et les deux modèles de test que leur licence permet de publier (le renard et le
# camion, tools/assets.lock). Jamais Sponza, dont la licence interdit la redistribution.
if(EMSCRIPTEN)
    set(testAssets "${PROJECT_SOURCE_DIR}/assets-cache")
    foreach(model Fox CesiumMilkTruck)
        if(NOT EXISTS "${testAssets}/Models/${model}")
            message(FATAL_ERROR "assets-cache/Models/${model} absent : lancer tools/fetch-assets.sh")
        endif()
        target_link_options(levain_sandbox PRIVATE
            "--preload-file=${testAssets}/Models/${model}@/assets-cache/Models/${model}")
    endforeach()
    # LINK_DEPENDS : la page se régénère quand son modèle change, pas seulement le code.
    set_target_properties(levain_sandbox PROPERTIES SUFFIX ".html" LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
    target_link_options(levain_sandbox PRIVATE
        -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1MB
        "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
        "--preload-file=${PROJECT_SOURCE_DIR}/data@/data"
        "--preload-file=${LEVAIN_SHADER_DIR}@/shaders")
    add_dependencies(levain_sandbox levain_shaders)
    # Les chemins que le programme lit, dans le système de fichiers préchargé. Rien à recompiler à
    # chaud : le hot-reload ne trouve pas de sources à surveiller, et ne fait rien.
    target_compile_definitions(levain_sandbox PRIVATE
        LEVAIN_DATA_DIR="/data"
        LEVAIN_TEST_ASSETS_DIR="/assets-cache"
        LEVAIN_CMAKE_COMMAND="" LEVAIN_BUILD_DIR="" LEVAIN_SHADER_SOURCE_DIR="")
    return()
endif()

# Les textures sont lues dans le dépôt, comme les shaders compilés dans le build : pas de copie à
# tenir à jour.
target_compile_definitions(levain_sandbox PRIVATE LEVAIN_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
# Les assets de test tiers, téléchargés par tools/fetch-assets.sh et jamais versionnés (ADR-0018) :
# une seconde racine d'assets, facultative.
target_compile_definitions(levain_sandbox PRIVATE
    LEVAIN_TEST_ASSETS_DIR="${PROJECT_SOURCE_DIR}/assets-cache")

# Le hot-reload des shaders relance le build qui a produit ce binaire (ADR-0014) : le CMake qui l'a
# configuré, son dossier de build, et les sources à surveiller.
target_compile_definitions(levain_sandbox PRIVATE
    LEVAIN_CMAKE_COMMAND="${CMAKE_COMMAND}"
    LEVAIN_BUILD_DIR="${CMAKE_BINARY_DIR}"
    LEVAIN_SHADER_SOURCE_DIR="${PROJECT_SOURCE_DIR}/shaders")
