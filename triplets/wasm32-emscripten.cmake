# Le triplet WebAssembly de Levain (ADR-0023) : celui de la communauté vcpkg, plus une parade.
include("${VCPKG_ROOT_DIR}/triplets/community/wasm32-emscripten.cmake")

# ktx 4.4.2 compile, sous Emscripten seulement, ses liaisons JavaScript (embind) en C++11, alors
# qu'Emscripten 6 exige C++17 pour embind : le build échoue sur interface/js_binding. Nous
# n'utilisons pas ces liaisons, mais ktx ne permet pas de les couper. À retirer dès qu'une version de
# ktx dans la baseline compile sans.
if(PORT STREQUAL "ktx")
    set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DCMAKE_CXX_STANDARD=17)
endif()
