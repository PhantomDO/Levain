# Port overlay d'ozz-animation 0.17.0 (ADR-0022, amendement du 2026-09-25 ; issue #117).
#
# Pourquoi un port : vcpkg n'en a pas. Par ce port, l'archive (43,5 Mo, dont 118 Mo décompressés de données
# d'exemples) n'est téléchargée qu'une fois, et ozz se compile une fois : le cache binaire de vcpkg le garde, en
# local comme en CI. Un FetchContent le retéléchargerait et le recompilerait dans chaque dossier de build.
#
# Les bibliothèques seules : base, animation (l'exécution), animation_offline (les builders qu'utilise notre
# passerelle glTF). Ni outils (gltf2ozz embarque sa propre copie de tinygltf), ni exemples, ni tests.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO guillaumeblanc/ozz-animation
    REF 83b35f166a2a7891b17c9839e79ade7602720962 # 0.17.0
    SHA512 420de0a6f49b1a48c5d9d8815844ef6504b16a4808cca0094b48e0d9f81d116c3511b468b4eaec861ebb0ba21ffc5e6ca81f6f894038601fd7cb8cac7e086d84
    HEAD_REF master
)

# Sous clang-cl, ozz se croit chez GCC (son test vise CMAKE_CXX_COMPILER_ID « MSVC ») et passe -Wall,
# que clang-cl lit comme /Wall, c'est-à-dire -Weverything ; ses avertissements traités en erreurs, il
# s'arrêtait sur ses propres identifiants (« _Ty » est réservé). /clang:-Wall donne le -Wall de Linux.
if(VCPKG_TARGET_IS_WINDOWS)
    vcpkg_replace_string("${SOURCE_PATH}/build-utils/cmake/compiler_settings.cmake"
        "add_compile_options(-Wall)" "add_compile_options(/clang:-Wall)")
endif()

# ozz_build_postfix : sans lui, la version Debug s'appelle libozz_base_d.a, et la config ci-dessous devrait
# connaître les deux noms.
vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -Dozz_build_tools=OFF
        -Dozz_build_fbx=OFF
        -Dozz_build_gltf=OFF
        -Dozz_build_samples=OFF
        -Dozz_build_howtos=OFF
        -Dozz_build_tests=OFF
        -Dozz_build_postfix=OFF
)
vcpkg_cmake_install()

# ozz installe ses bibliothèques et ses en-têtes, mais aucune config CMake : on fournit la nôtre.
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/ozz-animation-config.cmake"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.md")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share"
     "${CURRENT_PACKAGES_DIR}/share/doc")
