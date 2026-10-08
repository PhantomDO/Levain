# Windows x64, compilé depuis Linux par clang-cl (ADR-0035) : bibliothèques statiques, CRT de Microsoft en DLL,
# comme le triplet x64-windows-static-md de vcpkg. La toolchain lit LEVAIN_WINSYSROOT ; le passe-plat met ce
# chemin dans l'ABI des ports (le chemin ; les versions de MSVC et du SDK, la toolchain les fige).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../cmake/toolchains/windows-clang-cl.cmake")
set(VCPKG_ENV_PASSTHROUGH LEVAIN_WINSYSROOT)
# La toolchain inclut scripts/toolchains/windows.cmake de vcpkg (CRT, /utf-8, /EHsc, /Z7) : vcpkg ne hache que le
# fichier de VCPKG_CHAINLOAD_TOOLCHAIN_FILE, pas ce qu'il inclut. Sans cette ligne, une version de vcpkg qui
# change ces options laisserait le cache binaire rendre des ports compilés avec les anciennes.
set(VCPKG_HASH_ADDITIONAL_FILES "${VCPKG_ROOT_DIR}/scripts/toolchains/windows.cmake")
