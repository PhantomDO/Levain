# Les symboles publics d'un exécutable Windows, lus dans son PDB (ADR-0035) : l'équivalent de `nm -C`
# pour les contrôles build.no-tracy, build.no-editor et build.editor-symbols (levain_add_symbol_check,
# tests/CMakeLists.txt). Lancé par ctest :
#   cmake -DPDBUTIL=<llvm-pdbutil> -DUNDNAME=<llvm-undname> -DPDB=<fichier.pdb>
#         -DREQUIRE=<texte> [-DFORBID=<texte>] -P check_pdb_symbols.cmake
#
# Les noms du PDB sont décorés à la façon de MSVC, et le nom qualifié y est écrit à l'envers : `levain::editor::`
# s'y lit `@editor@levain@@`... sauf quand un nom déjà écrit dans le symbole est remplacé par son rang : llvm-undname
# lit `?f@app@levain@@YAXAEBUFoo@editor@2@@Z` comme `levain::app::f(levain::editor::Foo const &)`, où `@editor@levain@@`
# n'apparaît pas. Chercher la forme décorée laisserait passer ce cas. On démêle donc avec llvm-undname, et on
# cherche les mêmes textes que dans la sortie de nm.
#
# Le contrôle échoue bruyamment (règle n°7) : un PDB illisible ou sans symbole REQUIRE est une erreur, pas un
# succès muet.
foreach(variable PDBUTIL UNDNAME PDB REQUIRE)
    if(NOT ${variable})
        message(FATAL_ERROR "${variable} n'est pas donnée : le contrôle ne vérifierait rien")
    endif()
endforeach()
if(NOT EXISTS "${PDB}")
    message(FATAL_ERROR "${PDB} introuvable : le contrôle ne vérifierait rien")
endif()

# `dump -publics` écrit deux lignes par symbole ; sed garde le nom décoré de la première, entre apostrophes
# inversées. Le code de sortie de llvm-undname n'est pas lu : il vaut 1 dès qu'un nom n'est pas décoré, ce
# qui est le cas des symboles C (`ecs_vec_first`), qu'il n'y a rien à démêler.
execute_process(
    COMMAND "${PDBUTIL}" dump -publics "${PDB}"
    COMMAND sed -n "s/^.*S_PUB32 [^`]*`\\(.*\\)`$/\\1/p"
    COMMAND "${UNDNAME}"
    OUTPUT_VARIABLE symbols
    ERROR_QUIET
    RESULTS_VARIABLE results)
list(GET results 0 pdbutilResult)
list(GET results 1 sedResult)
if(NOT pdbutilResult EQUAL 0 OR NOT sedResult EQUAL 0)
    message(FATAL_ERROR "${PDBUTIL} dump -publics ${PDB} a échoué (${pdbutilResult}, sed ${sedResult})")
endif()

string(FIND "${symbols}" "${REQUIRE}" requiredAt)
if(requiredAt EQUAL -1)
    message(FATAL_ERROR "aucun symbole « ${REQUIRE} » dans ${PDB} : le contrôle ne lit rien, ou son motif est périmé")
endif()
set(absence "")
if(FORBID)
    set(absence ", aucun « ${FORBID} »")
    string(FIND "${symbols}" "${FORBID}" forbiddenAt)
    if(NOT forbiddenAt EQUAL -1)
        string(SUBSTRING "${symbols}" ${forbiddenAt} 160 forbidden)
        string(REGEX REPLACE "\n.*" "" forbidden "${forbidden}")
        message(FATAL_ERROR "${PDB} contient un symbole « ${FORBID} » : ${forbidden}")
    endif()
endif()
message(STATUS "${PDB} : des symboles « ${REQUIRE} »${absence}")
