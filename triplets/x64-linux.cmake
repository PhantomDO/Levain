# Le triplet x64-linux de vcpkg, avec la cible processeur écrite (ADR-0033) : x86-64, ce que la
# distrobox de référence et les runners Ubuntu 24.04 compilaient déjà sans le dire.
include("${VCPKG_ROOT_DIR}/triplets/x64-linux.cmake")

# Le GCC d'Ubuntu 26.04 en variante amd64v3, celle des runners GitHub, vise x86-64-v3 par défaut :
# basisu (dans ktx) y refuse de compiler ses noyaux SSE (« Please check your compiler options »), et
# la CI produirait d'autres binaires que la machine de référence.
set(VCPKG_C_FLAGS "-march=x86-64")
set(VCPKG_CXX_FLAGS "-march=x86-64")
