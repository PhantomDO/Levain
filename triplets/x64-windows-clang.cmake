# Windows x64, compilé depuis Linux par clang-cl (essai de l'ADR à venir) : bibliothèques statiques, CRT de
# Microsoft en DLL, comme le triplet x64-windows-static-md de vcpkg.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../cmake/toolchains/windows-clang-cl.cmake")
set(VCPKG_ENV_PASSTHROUGH LEVAIN_WINSYSROOT)
