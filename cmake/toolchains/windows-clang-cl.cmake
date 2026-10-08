# Compiler pour Windows depuis Linux : clang-cl et lld-link de LLVM, avec la STL et le SDK de Microsoft lus dans
# un « winsysroot », la disposition de Visual Studio (VC/ et Windows Kits/). Essai de l'ADR à venir.
#
# Chargé par vcpkg (VCPKG_CHAINLOAD_TOOLCHAIN_FILE) pour chaque port comme pour Levain, et relu à chaque
# try_compile : rien ici ne doit dépendre d'un état déjà posé.

# La racine passe par l'environnement : le triplet la laisse entrer dans les builds des ports
# (VCPKG_ENV_PASSTHROUGH), où vcpkg vide le reste de l'environnement.
if(NOT DEFINED ENV{LEVAIN_WINSYSROOT})
    message(FATAL_ERROR
        "LEVAIN_WINSYSROOT n'est pas posée : le dossier qui contient VC/Tools/MSVC et "
        "« Windows Kits/10 », comme une installation de Visual Studio.")
endif()
set(levainWinsysroot "$ENV{LEVAIN_WINSYSROOT}")
if(NOT IS_DIRECTORY "${levainWinsysroot}/VC/Tools/MSVC" OR NOT IS_DIRECTORY "${levainWinsysroot}/Windows Kits/10")
    message(FATAL_ERROR "LEVAIN_WINSYSROOT=${levainWinsysroot} n'a ni VC/Tools/MSVC ni « Windows Kits/10 ».")
endif()

# Le même LLVM que celui de Linux : LLVM_VERSION de la CI.
set(levainLlvm "/usr/lib/llvm-23/bin")
set(CMAKE_C_COMPILER "${levainLlvm}/clang-cl")
set(CMAKE_CXX_COMPILER "${levainLlvm}/clang-cl")
set(CMAKE_LINKER "${levainLlvm}/lld-link")
set(CMAKE_AR "${levainLlvm}/llvm-lib")
set(CMAKE_RC_COMPILER "${levainLlvm}/llvm-rc")
set(CMAKE_MT "${levainLlvm}/llvm-mt")
set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)

# Les options de vcpkg pour Windows (CRT, /utf-8, /EHsc, /Z7…), lues dans son propre fichier pour que les ports
# et Levain se compilent pareil. Il les construit à partir des VCPKG_*_FLAGS : c'est là qu'entre le winsysroot.
if(NOT DEFINED VCPKG_CRT_LINKAGE)
    set(VCPKG_CRT_LINKAGE dynamic) # Levain lui-même : le triplet n'est lu que par les ports.
    set(VCPKG_TARGET_ARCHITECTURE x64)
    set(VCPKG_SET_CHARSET_FLAG ON)
endif()
string(PREPEND VCPKG_C_FLAGS "/winsysroot ${levainWinsysroot} ")
string(PREPEND VCPKG_CXX_FLAGS "/winsysroot ${levainWinsysroot} ")
string(PREPEND VCPKG_LINKER_FLAGS "/winsysroot:${levainWinsysroot} ")
# Ce fichier est inclus par scripts/buildsystems/vcpkg.cmake, y compris dans un try_compile, où
# CMAKE_TOOLCHAIN_FILE n'est pas défini : c'est par lui qu'on trouve le dossier de vcpkg.
cmake_path(GET CMAKE_PARENT_LIST_FILE PARENT_PATH levainVcpkgBuildsystems)
if(NOT EXISTS "${levainVcpkgBuildsystems}/../toolchains/windows.cmake")
    message(FATAL_ERROR "windows-clang-cl.cmake se charge par vcpkg (VCPKG_CHAINLOAD_TOOLCHAIN_FILE), "
                        "pas directement : inclus par ${CMAKE_PARENT_LIST_FILE}.")
endif()
include("${levainVcpkgBuildsystems}/../toolchains/windows.cmake")

# windows.cmake n'ôte /MP (la compilation parallèle de MSVC) que pour un compilateur nommé
# « clang-cl.exe » : sous Linux, il s'appelle clang-cl. clang-cl ignore /MP en le signalant, et un port
# compilé en -Werror (ktx) s'arrête sur cet avertissement.
foreach(lang C CXX)
    string(REPLACE "/MP " "" levainFlags "${CMAKE_${lang}_FLAGS}")
    set(CMAKE_${lang}_FLAGS "${levainFlags}" CACHE STRING "" FORCE)
endforeach()
