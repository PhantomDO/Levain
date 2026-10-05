# SPECS §7 : les bibliothèques d'import, de cuisson, d'animation et de physique ne sont incluses que là
# où elles servent.
#   - fastgltf : engine/assets/src, et engine/animation/src pour la passerelle glTF (ADR-0022) ;
#   - libktx : engine/assets/src (ADR-0020) ;
#   - ozz-animation : engine/animation/src (ADR-0022) ;
#   - Jolt : engine/physics/src (ADR-0026).
# Le reste du moteur, les plugins et le cuiseur compris, ne voit que nos types. Lancé par ctest :
#   cmake -DROOT=<dépôt> -P check_asset_libraries_visibility.cmake
#
# Le contrôle échoue bruyamment (règle n°7) : sans aucun fichier à lire, il ne vérifierait rien.
file(GLOB_RECURSE files "${ROOT}/engine/*.cpp" "${ROOT}/engine/*.hpp" "${ROOT}/plugins/*.cpp"
     "${ROOT}/plugins/*.hpp" "${ROOT}/sandbox/*.cpp" "${ROOT}/tests/*.cpp" "${ROOT}/tools/*.cpp")
if(NOT files)
    message(FATAL_ERROR "aucune source sous ${ROOT} : le contrôle ne vérifierait rien")
endif()

# Rend vrai dans `out` si `file` est sous l'un des dossiers `ARGN`, relatifs à ROOT.
function(is_under out file)
    foreach(directory IN LISTS ARGN)
        string(FIND "${file}" "${ROOT}/${directory}/" position)
        if(position EQUAL 0)
            set(${out} TRUE PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out} FALSE PARENT_SCOPE)
endfunction()

set(failures 0)
foreach(file IN LISTS files)
    is_under(fastgltfAllowed "${file}" engine/assets/src engine/animation/src)
    is_under(ktxAllowed "${file}" engine/assets/src)
    is_under(ozzAllowed "${file}" engine/animation/src)
    is_under(joltAllowed "${file}" engine/physics/src)
    set(forbidden "")
    if(NOT fastgltfAllowed)
        list(APPEND forbidden fastgltf)
    endif()
    if(NOT ktxAllowed)
        list(APPEND forbidden ktx)
    endif()
    if(NOT ozzAllowed)
        list(APPEND forbidden ozz)
    endif()
    if(NOT joltAllowed)
        list(APPEND forbidden Jolt)
    endif()
    if(NOT forbidden)
        continue()
    endif()
    list(JOIN forbidden "|" pattern)
    file(STRINGS "${file}" hits REGEX "#include [<\"](${pattern})")
    if(hits)
        message(SEND_ERROR "${file} inclut ${hits}, hors des dossiers permis (SPECS §7)")
        math(EXPR failures "${failures} + 1")
    endif()
endforeach()
if(failures EQUAL 0)
    message(STATUS "fastgltf, libktx, ozz-animation et Jolt seulement là où SPECS §7 les permet")
endif()
