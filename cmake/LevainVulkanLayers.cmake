# Les couches de validation Vulkan sous Windows (ADR-0035, décision 6) : celles du port vcpkg
# vulkan-validationlayers, copiées à côté de chaque exécutable qui crée un device Vulkan. Sous Linux, ce sont les
# couches du système (apt) et le chargeur les trouve seul : ce fichier ne sert qu'à Windows.
#
#   levain_ship_vulkan_layers()
#
# est appelé une fois par engine/gpu, et ne demande rien aux exécutables : à la fin de la configuration, sur tout
# le projet, jeu compris, chaque exécutable qui lie levain_gpu (même par un autre module) reçoit la copie.
# Une liste à tenir à la main s'oublierait, et l'oubli ne se verrait qu'au lancement. Le chargeur ne cherche pas
# à côté de l'exécutable : engine/gpu lui désigne ce dossier par VK_ADD_LAYER_PATH (device_vk.cpp).

include_guard(GLOBAL)
include(LevainPlugin) # levain_targets_under, levain_linked_targets

# Les fichiers du port, ou une configuration qui échoue en les nommant (règle n°7) : sans eux, le build irait
# jusqu'au premier lancement. Les couches de la configuration Release, aussi pour un exécutable Debug : le chargeur
# les charge dans leur propre CRT, et celles du Debug ralentissent la validation sans rien vérifier de plus.
function(levain_ship_vulkan_layers)
    set(layerDir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
    set(layerFiles "")
    foreach(name VkLayer_khronos_validation.dll VkLayer_khronos_validation.json)
        if(NOT EXISTS "${layerDir}/${name}")
            message(FATAL_ERROR "${layerDir}/${name} est introuvable : le port vcpkg vulkan-validationlayers "
                                "(vcpkg.json, plateforme windows) ne l'a pas installé (ADR-0035).")
        endif()
        list(APPEND layerFiles "${layerDir}/${name}")
    endforeach()
    set_property(GLOBAL PROPERTY LEVAIN_VULKAN_LAYER_FILES "${layerFiles}")

    get_property(scheduled GLOBAL PROPERTY LEVAIN_VULKAN_LAYERS_SCHEDULED)
    if(NOT scheduled)
        set_property(GLOBAL PROPERTY LEVAIN_VULKAN_LAYERS_SCHEDULED TRUE)
        cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL levain_copy_vulkan_layers)
    endif()
endfunction()

# Vrai si `target` lie `wanted`, directement ou à travers d'autres cibles.
function(levain_links_target target wanted out)
    set(pending ${target})
    set(seen "")
    while(pending)
        list(POP_FRONT pending current)
        if(current STREQUAL wanted)
            set(${out} TRUE PARENT_SCOPE)
            return()
        endif()
        if(NOT current IN_LIST seen)
            list(APPEND seen ${current})
            levain_linked_targets(${current} linked)
            list(APPEND pending ${linked})
        endif()
    endwhile()
    set(${out} FALSE PARENT_SCOPE)
endfunction()

# Une cible de copie par dossier de sortie, dont dépendent les exécutables qui y sont : add_custom_command(TARGET)
# n'accepte que les cibles du dossier courant, add_dependencies, toutes. Les exécutables d'un même dossier (les
# tests) la partagent, sans quoi deux copies simultanées écriraient les mêmes fichiers.
function(levain_copy_vulkan_layers)
    get_property(layerFiles GLOBAL PROPERTY LEVAIN_VULKAN_LAYER_FILES)
    levain_targets_under("${CMAKE_SOURCE_DIR}" targets)
    set(directories "")
    set(shipped "")
    foreach(target IN LISTS targets)
        get_target_property(type ${target} TYPE)
        if(NOT type STREQUAL "EXECUTABLE")
            continue()
        endif()
        levain_links_target(${target} levain_gpu linksGpu)
        if(NOT linksGpu)
            continue()
        endif()
        get_target_property(directory ${target} RUNTIME_OUTPUT_DIRECTORY)
        if(NOT directory)
            get_target_property(directory ${target} BINARY_DIR)
        endif()
        list(FIND directories "${directory}" index)
        if(index EQUAL -1)
            list(LENGTH directories index)
            list(APPEND directories "${directory}")
            add_custom_target(levain_vulkan_layers_${index}
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${layerFiles} "$<TARGET_FILE_DIR:${target}>"
                COMMENT "Couches de validation Vulkan à côté de ${target}"
                VERBATIM)
        endif()
        add_dependencies(${target} levain_vulkan_layers_${index})
        list(APPEND shipped ${target})
    endforeach()
    message(STATUS "Couches de validation Vulkan livrées à côté de : ${shipped}")
    # Un parcours qui ne trouve plus aucun exécutable livrerait rien, sans un mot (règle n°7).
    if(TARGET levain_sandbox AND NOT "levain_sandbox" IN_LIST shipped)
        message(FATAL_ERROR "levain_sandbox ne reçoit pas les couches de validation Vulkan : "
                            "levain_links_target ne voit plus qu'il lie levain_gpu (cmake/LevainVulkanLayers.cmake).")
    endif()
endfunction()
