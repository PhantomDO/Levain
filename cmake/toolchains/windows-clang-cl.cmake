# Compiler pour Windows depuis Linux (ADR-0035) : clang-cl et lld-link de LLVM, avec la STL et le SDK de
# Microsoft lus dans un « winsysroot », la disposition de Visual Studio (VC/ et Windows Kits/).
#
# Chargé par vcpkg (VCPKG_CHAINLOAD_TOOLCHAIN_FILE) pour chaque port comme pour Levain, et relu à chaque
# try_compile : rien ici ne doit dépendre d'un état déjà posé.

# La racine passe par l'environnement. Le triplet la déclare dans VCPKG_ENV_PASSTHROUGH : sous Linux, vcpkg ne
# vide pas l'environnement d'un port (il ne le fait que sous Windows), mais le chemin entre ainsi dans l'ABI de
# chaque port, et un autre winsysroot les recompile.
if(NOT DEFINED ENV{LEVAIN_WINSYSROOT})
    message(FATAL_ERROR
        "LEVAIN_WINSYSROOT n'est pas posée : le dossier qui contient VC/Tools/MSVC et "
        "« Windows Kits/10 », comme une installation de Visual Studio (docs/SETUP.md, tools/wsl/README.md).")
endif()
set(levainWinsysroot "$ENV{LEVAIN_WINSYSROOT}")
# Les options de vcpkg sont une chaîne coupée aux espaces : un chemin qui en contient se couperait en route.
if(levainWinsysroot MATCHES " ")
    message(FATAL_ERROR "LEVAIN_WINSYSROOT=${levainWinsysroot} contient une espace : passer par un dossier "
                        "de liens sans espace, comme ~/winsysroot (tools/wsl/README.md).")
endif()

# Les versions de la STL et du SDK, figées (ADR-0035, décision 2) : celles des Build Tools du portable de
# Donnovan, et de xwin en CI. Écrites ici, elles entrent dans l'ABI des ports (vcpkg hache ce fichier) ; sans
# elles, clang-cl prendrait la plus récente du winsysroot, et une mise à jour des Build Tools changerait la STL
# sans recompiler les ports. Les changer, c'est changer aussi le manifeste figé de tools/winsysroot.sh (vsmanUrl,
# vsmanSha256) pour un manifeste qui les propose, et sur lequel ses deux corrections s'appliquent encore.
set(levainMsvcVersion 14.51.36231)
set(levainWinSdkVersion 10.0.26100.0)
if(NOT IS_DIRECTORY "${levainWinsysroot}/VC/Tools/MSVC/${levainMsvcVersion}"
   OR NOT IS_DIRECTORY "${levainWinsysroot}/Windows Kits/10/Include/${levainWinSdkVersion}")
    message(FATAL_ERROR
        "LEVAIN_WINSYSROOT=${levainWinsysroot} n'est pas un winsysroot de MSVC ${levainMsvcVersion} et du SDK "
        "${levainWinSdkVersion} : il y manque VC/Tools/MSVC/${levainMsvcVersion} ou « Windows Kits/10/Include/"
        "${levainWinSdkVersion} ». Sous Windows, les ajouter par Visual Studio Installer (composants "
        "individuels), ou changer ces versions ici, pour la CI aussi (ADR-0035).")
endif()
# _MSC_VER que clang-cl annonce : hors de Windows, il ne lit pas celui de cl.exe et vaut 19.33 par défaut. Celui
# de la STL figée (14.xy correspond à 19.xy), pour que la STL et les ports voient le compilateur qu'elle attend.
string(REGEX REPLACE "^14\\.([0-9]+)\\..*$" "19.\\1" levainMsvcCompatibility "${levainMsvcVersion}")

# Le même LLVM que celui de Linux : la version suit LLVM_VERSION de .github/workflows/ci.yml et de
# tools/wsl/provision-levain.sh, à changer ensemble.
set(levainLlvmVersion 23)
set(levainLlvm "/usr/lib/llvm-${levainLlvmVersion}/bin")
foreach(tool clang-cl lld-link llvm-lib llvm-rc llvm-mt)
    # Sans clang-cl, CMake chercherait un autre compilateur et le build échouerait plus loin, sur un message
    # qui ne dit rien de la cause (règle n°7).
    if(NOT EXISTS "${levainLlvm}/${tool}")
        message(FATAL_ERROR "${levainLlvm}/${tool} est introuvable : installer llvm-${levainLlvmVersion} "
                            "(tools/wsl/provision-levain.sh, ou apt.llvm.org).")
    endif()
endforeach()
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
# Dans un try_compile, windows.cmake transmet les VCPKG_*_FLAGS déjà complétés
# (CMAKE_TRY_COMPILE_PLATFORM_VARIABLES) : ne pas y remettre le winsysroot.
if(NOT VCPKG_CXX_FLAGS MATCHES "/winsysroot ")
    set(levainSysroot "/winsysroot ${levainWinsysroot} /vctoolsversion ${levainMsvcVersion}")
    string(APPEND levainSysroot " /winsdkversion ${levainWinSdkVersion}")
    string(APPEND levainSysroot " -fms-compatibility-version=${levainMsvcCompatibility}")
    string(PREPEND VCPKG_C_FLAGS "${levainSysroot} ")
    string(PREPEND VCPKG_CXX_FLAGS "${levainSysroot} ")
    string(PREPEND VCPKG_LINKER_FLAGS "/winsysroot:${levainWinsysroot} /vctoolsversion:${levainMsvcVersion} "
                                      "/winsdkversion:${levainWinSdkVersion} ")
endif()
# Ce fichier est inclus par scripts/buildsystems/vcpkg.cmake, y compris dans un try_compile, où
# CMAKE_TOOLCHAIN_FILE n'est pas défini : c'est par lui qu'on trouve le dossier de vcpkg.
cmake_path(GET CMAKE_PARENT_LIST_FILE PARENT_PATH levainVcpkgBuildsystems)
if(NOT EXISTS "${levainVcpkgBuildsystems}/../toolchains/windows.cmake")
    message(FATAL_ERROR "windows-clang-cl.cmake se charge par vcpkg (VCPKG_CHAINLOAD_TOOLCHAIN_FILE), "
                        "pas directement : inclus par ${CMAKE_PARENT_LIST_FILE}.")
endif()
# windows.cmake pose ses options en cache sans FORCE : dans un dossier de build qui existe déjà, une toolchain
# modifiée (une version figée qui change) ne les changerait pas, sans un mot. On les reprend à chaque
# configuration ; les ports, eux, se compilent toujours dans un dossier neuf. Une seule fois par configuration,
# comme windows.cmake lui-même (_VCPKG_WINDOWS_TOOLCHAIN) : ce fichier est inclus plusieurs fois, et effacer ces
# options quand windows.cmake ne les repose plus ferait perdre le winsysroot (« 'windows.h' file not found »).
if(NOT _VCPKG_WINDOWS_TOOLCHAIN)
    foreach(config "" _DEBUG _RELEASE)
        foreach(kind C_FLAGS CXX_FLAGS EXE_LINKER_FLAGS SHARED_LINKER_FLAGS MODULE_LINKER_FLAGS STATIC_LINKER_FLAGS)
            unset(CMAKE_${kind}${config} CACHE)
        endforeach()
    endforeach()
    unset(CMAKE_RC_FLAGS CACHE)
endif()
include("${levainVcpkgBuildsystems}/../toolchains/windows.cmake")

# windows.cmake n'ôte /MP (la compilation parallèle de MSVC) que pour un compilateur nommé
# « clang-cl.exe » : sous Linux, il s'appelle clang-cl. clang-cl ignore /MP en le signalant, et un port
# compilé en -Werror (ktx) s'arrête sur cet avertissement.
foreach(lang C CXX)
    string(REPLACE "/MP " "" levainFlags "${CMAKE_${lang}_FLAGS}")
    set(CMAKE_${lang}_FLAGS "${levainFlags}" CACHE STRING "" FORCE)
endforeach()
