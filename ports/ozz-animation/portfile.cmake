# Port overlay d'ozz-animation 0.17.0 (ADR-0022, amendement du 2026-09-25 ; issue #117).
#
# Pourquoi un port : vcpkg n'en a pas. Par ce port, l'archive (43,5 Mo, dont 118 Mo décompressés de données
# d'exemples) n'est téléchargée qu'une fois, et ozz se compile une fois : le cache binaire de vcpkg le garde, en
# local comme en CI. Un FetchContent le retéléchargerait et le recompilerait dans chaque dossier de build.
#
# Les bibliothèques seules : base, animation (l'exécution), animation_offline (les builders qu'utilise notre
# passerelle glTF). Ni outils (gltf2ozz embarque sa propre copie de tinygltf), ni exemples, ni tests.
#
# Une parade pour Windows compilé par clang-cl (ADR-0035) : sa raison et sa condition de retrait sont plus bas,
# après le téléchargement des sources qu'elle modifie.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO guillaumeblanc/ozz-animation
    REF 83b35f166a2a7891b17c9839e79ade7602720962 # 0.17.0
    SHA512 420de0a6f49b1a48c5d9d8815844ef6504b16a4808cca0094b48e0d9f81d116c3511b468b4eaec861ebb0ba21ffc5e6ca81f6f894038601fd7cb8cac7e086d84
    HEAD_REF master
)

# Parade pour Windows compilé depuis Linux par clang-cl (ADR-0035, amendement de l'ADR-0007 sur les ports overlay).
#
# Raison : le test d'ozz vise CMAKE_CXX_COMPILER_ID « MSVC », quand clang-cl s'identifie « Clang » avec la syntaxe
# de MSVC. ozz le prend pour GCC et passe -Wall, que clang-cl lit comme /Wall (-Weverything) : ses avertissements,
# traités en erreurs, arrêtent le build. On lui fait prendre sa branche MSVC, celle que ses auteurs ont écrite pour
# Windows, sans /MP, que clang-cl ignore en le signalant. Cette branche compile les sources d'ozz en /W4 /WX et
# définit _CRT_SECURE_NO_WARNINGS, dont ozz a besoin (fopen, strcpy) : la définition reste dans le build de ce port
# tiers et n'atteint jamais le code de Levain, qui ne la pose pas (règle n°4).
# Condition de retrait : ozz reconnaît clang-cl dans build-utils/cmake/compiler_settings.cmake (tester MSVC et non
# l'identifiant du compilateur), ou la CI de vcpkg compile un port ozz-animation officiel sous clang-cl.
if(VCPKG_TARGET_IS_WINDOWS)
    # vcpkg_replace_string ne fait qu'avertir quand le texte n'est plus là : une nouvelle version d'ozz casserait
    # plus loin, sur un message sans rapport. On le vérifie d'abord (règle n°7).
    file(READ "${SOURCE_PATH}/build-utils/cmake/compiler_settings.cmake" ozzSettings)
    foreach(expected [[if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")]] "add_compile_options(/MP)")
        string(FIND "${ozzSettings}" "${expected}" found)
        if(found EQUAL -1)
            message(FATAL_ERROR "ozz a changé : « ${expected} » manque dans compiler_settings.cmake ; "
                                "relire la parade clang-cl de ce port (ADR-0035).")
        endif()
    endforeach()
    vcpkg_replace_string("${SOURCE_PATH}/build-utils/cmake/compiler_settings.cmake"
        [[if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")]] [[if(MSVC)]])
    vcpkg_replace_string("${SOURCE_PATH}/build-utils/cmake/compiler_settings.cmake"
        "add_compile_options(/MP)" "")
endif()

# Sa branche MSVC choisit elle-même la CRT, statique par défaut, avec ozz_build_msvc_rt_dll : elle doit suivre le
# triplet, sans quoi lld-link refuse de lier ozz au reste de Levain (/failifmismatch sur RuntimeLibrary). Même
# condition de retrait que ci-dessus. Sans effet sous Linux : seule la branche MSVC lit l'option.
if(VCPKG_CRT_LINKAGE STREQUAL "dynamic")
    set(ozzCrtDll ON)
else()
    set(ozzCrtDll OFF)
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
        -Dozz_build_msvc_rt_dll=${ozzCrtDll}
)
vcpkg_cmake_install()

# ozz installe ses bibliothèques et ses en-têtes, mais aucune config CMake : on fournit la nôtre.
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/ozz-animation-config.cmake"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.md")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share"
     "${CURRENT_PACKAGES_DIR}/share/doc")
