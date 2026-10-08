# Port overlay de spirv-reflect 1.4.350.1 : le port officiel de la baseline, plus une parade pour Windows compilé
# depuis Linux par clang-cl (ADR-0035, décision 6 ; amendement de l'ADR-0007 sur les ports overlay).
#
# Pourquoi : spirv-reflect est une dépendance du port vulkan-validationlayers, que Windows prend de vcpkg
# (décision 6). Son CMakeLists.txt teste CMAKE_CXX_COMPILER_ID « Clang » pour ajouter -Wall -Wextra -Wpedantic
# -Werror, et clang-cl s'identifie « Clang » avec la syntaxe de MSVC : il lit -Wall comme /Wall, c'est-à-dire
# -Weverything. Les avertissements les plus pointilleux (-Wc++98-compat-pedantic sur spirv.h), traités en erreurs,
# arrêtent le build des deux programmes du port (spirv-reflect, spirv-reflect-pp), seuls compilés en -Werror. Ni les
# couches de validation ni le moteur ne s'en servent : sous Windows, on ne les compile pas (overlay minimal,
# ADR-0007). La bibliothèque, elle, se compile comme dans le port officiel.
#
# Seule cette option diffère du port officiel (vcpkg 9e593bb, ports/spirv-reflect) : même version, même patch.
#
# Condition de retrait : spirv-reflect ne teste plus l'identifiant « Clang » sans distinguer clang-cl, ou la CI de
# vcpkg compile le port officiel sous clang-cl. Alors supprimer ce dossier et revenir au port officiel.
# À chaque changement de baseline : recopier le port officiel de la nouvelle baseline. spirv-reflect suit la version
# du SDK Vulkan, comme vulkan-validationlayers, et un overlay passe toujours devant la baseline ;
# cmake/LevainVulkanLayers.cmake refuse deux versions différentes (règle n°7).
vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO KhronosGroup/SPIRV-Reflect
    REF "vulkan-sdk-${VERSION}"
    SHA512 d48fd41e3d431bb22742ed507e465424f606957d7decd791ca240e8129ecdf817fc41954586953de1ff3a0a76c89a6166a74f689b98bb71d32f0277da0cd4cc9
    HEAD_REF main
    PATCHES
        export-targets.patch
)

set(reflectExecutables ON)
if(VCPKG_TARGET_IS_WINDOWS)
    set(reflectExecutables OFF)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DSPIRV_REFLECT_STATIC_LIB=ON
        -DSPIRV_REFLECT_EXAMPLES=OFF
        -DSPIRV_REFLECT_BUILD_TESTS=OFF
        -DSPIRV_REFLECT_EXECUTABLE=${reflectExecutables}
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(PACKAGE_NAME unofficial-spirv-reflect)

vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/spirv-reflect/spirv_reflect.h" "./include/spirv/unified1/spirv.h" "spirv.h")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")

if(reflectExecutables)
    vcpkg_copy_tools(TOOL_NAMES spirv-reflect-pp spirv-reflect AUTO_CLEAN)
endif()
