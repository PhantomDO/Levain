# Compilation des shaders Slang au build (ADR-0005, amendement du 2026-09-21). Une commande par
# point d'entrée, qui produit le SPIR-V, le DXIL et le WGSL (le navigateur, ADR-0023) : seul un
# shader modifié est recompilé, et une erreur de shader fait échouer le build avec le message de
# slangc. Un shader qui sort du sous-ensemble portable (un ByteAddressBuffer, par exemple) échoue
# donc dès le build natif, bien avant d'arriver dans un navigateur.

include(LevainShaders)
set(shaderOutputs)

levain_add_shader(triangle vertexMain vertex)
levain_add_shader(triangle fragmentMain fragment)
levain_add_shader(mesh vertexMain vertex)
levain_add_shader(mesh fragmentMain fragment)
levain_add_shader(skinning computeMain compute)
levain_add_shader(light_clusters computeMain compute)
levain_add_shader(shadow vertexMain vertex)
levain_add_shader(tonemap vertexMain vertex)
levain_add_shader(tonemap fragmentMain fragment)
levain_add_shader(sky vertexMain vertex)
levain_add_shader(sky fragmentMain fragment)
levain_add_shader(environment cubeFromEquirect compute)
levain_add_shader(environment downsample compute)
levain_add_shader(environment irradiance compute)
levain_add_shader(environment prefilterSpecular compute)
levain_add_shader(environment integrateBrdf compute)

add_custom_target(levain_shaders ALL DEPENDS ${shaderOutputs})
