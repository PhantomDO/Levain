# SPECS §7 et ADR-0032 : Dear ImGui est dans ui/ et au-dessus (app, l'éditeur, le sandbox, le jeu),
# jamais en dessous ; et ui/ ne voit pas SDL, que seul platform/ inclut.
# Lancé par ctest : cmake -DROOT=<dépôt> -P check_imgui_visibility.cmake
#
# Le contrôle échoue bruyamment (règle n°7) : un dossier absent est une erreur, pas un succès muet.
set(failures 0)
function(check_absent module pattern what)
    set(directory "${ROOT}/engine/${module}")
    if(NOT IS_DIRECTORY "${directory}")
        message(FATAL_ERROR "${directory} introuvable : le contrôle ne vérifierait rien")
    endif()
    file(GLOB_RECURSE files "${directory}/*.cpp" "${directory}/*.hpp" "${directory}/CMakeLists.txt")
    foreach(file IN LISTS files)
        file(STRINGS "${file}" hits REGEX "${pattern}")
        if(hits)
            message(SEND_ERROR "${file} utilise ${what}, interdit dans ${module}/ (ADR-0032) : ${hits}")
            math(EXPR failures "${failures} + 1")
            set(failures ${failures} PARENT_SCOPE)
        endif()
    endforeach()
endfunction()
foreach(module core platform gpu render scene assets animation physics input)
    check_absent(${module} "#include [<\"]imgui|imgui::|ImGui::" "ImGui")
endforeach()
check_absent(ui "#include [<\"]SDL|SDL3::" "SDL")
if(failures EQUAL 0)
    message(STATUS "ImGui absent sous ui/, et SDL absent de ui/")
endif()
