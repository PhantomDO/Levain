# Lance une commande et échoue si elle sort avec un code non nul. ctest ne juge que la sortie
# quand PASS_REGULAR_EXPRESSION est posé (le code de sortie est alors ignoré) : un éditeur qui plante
# après avoir écrit la ligne attendue resterait vert. La sortie est relayée telle quelle, pour que
# les motifs du test la lisent.
#
# Lancé par ctest : cmake -DCOMMAND="<programme>;<argument>;..." -P run_expect_exit_zero.cmake.
cmake_minimum_required(VERSION 3.28) # un script n'a sinon aucune politique

if(NOT COMMAND_LINE)
    message(FATAL_ERROR "COMMAND_LINE manque : la liste de la commande à lancer")
endif()
execute_process(COMMAND ${COMMAND_LINE}
    RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
message(NOTICE "${out}")
message(NOTICE "${err}")
if(NOT code EQUAL 0)
    message(FATAL_ERROR "la commande est sortie avec le code ${code}")
endif()
