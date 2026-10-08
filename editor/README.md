# `editor`

## Rôle

La bibliothèque de l'éditeur ([ADR-0018](../docs/adr/0018-moteur-plugins-et-jeu.md),
[ADR-0034](../docs/adr/0034-reflexion-des-composants.md), choix 6B) : ce que l'éditeur ajoute à un programme du
moteur, au-dessus d'`app` (SPECS §7). Le jeu en construit l'exécutable : son `main.cpp` compilé une seconde fois,
avec une définition qui enveloppe son démarrage par `withEditor`, et lié à `levain::editor`. Levain construit
ainsi `levain_sandbox_editor` (dans `sandbox/`) pour ses démos et sa CI ; *Rando* construira `rando_editor` à côté
de `rando`, dont la cible ne change pas.

**État en M7.2 (en cours)** : le branchement sur la boucle, l'option `--select` et le bilan que lit la CI. La
hiérarchie et l'inspecteur suivent.

## Invariants

1. **Le jeu livré n'embarque pas l'éditeur.** Ni une cible du moteur (`engine/`) ni la cible runtime d'un plugin
   ne lient `levain_editor` : `levain_check_plugin_boundaries` le refuse au configure (`cmake/LevainPlugin.cmake`,
   vérifié par les tests `cmake.plugins.*`). `build.no-editor` refuse un symbole `levain::editor::` dans
   `levain_sandbox`, et `build.editor-symbols` en exige dans `levain_sandbox_editor` : sans lui, un motif périmé
   laisserait le premier vert sans rien lire (règle n°7). La cible éditeur d'un plugin (M7.6) pourra le lier.
   Le configure ne lit que les liens directs : une cible intermédiaire qui lie l'éditeur lui échappe, et seul
   `build.no-editor` la rattrape, pour le sandbox.
2. **L'éditeur ne lie jamais un plugin** : la même fonction parcourt `editor/` comme `engine/`.
3. **Natif seulement**, hors du bloc `PROJECT_IS_TOP_LEVEL` : la page web n'a pas d'éditeur, et un jeu qui
   récupère Levain par `FetchContent` reçoit la bibliothèque.
4. **La boucle ne change pas** : `withEditor` enveloppe la fonction de démarrage du programme et ses points
   d'accroche (ADR-0029).
5. **L'exécutable éditeur ouvre les panneaux** : son `main` pose `settings.showUiPanels = true` avant de lire
   les options, que `--ui off` les ferme encore.
6. **Un `--select` introuvable fait échouer le démarrage**, en nommant l'entité (règle n°7).

## Points d'entrée

- `withEditor(start, options)` : la fonction de démarrage du programme, enveloppée. Elle choisit l'entité de
  `--select` et écrit à la fin le bilan que lit la CI, « éditeur : N entités ; sélection : chemin ».
- `takeEditorOptions(arguments, options)` : retire `--select chemin` de la ligne de commande avant que le programme
  ne lise ses options, qu'il refuserait sinon.
- `selectedIfAlive(world, selected)` : l'entité choisie si elle vit encore.

## Pièges connus

- **`selectedIfAlive`** : la sélection garde l'identifiant complet de l'entité, génération comprise, et ne le
  relit qu'à travers `is_alive`. Une entité détruite ne laisse pas de poignée morte, et flecs, qui recycle son
  index avec une autre génération, ne fait pas passer la nouvelle entité pour la sélection.
- **Les options vont par paires** : `takeEditorOptions` lit nom et valeur comme `app::parseCommonOption`. Un
  « --select » en valeur d'une autre option n'est pas pris ; seul, sans valeur, il reste dans la ligne de
  commande, et le programme la refuse.

## Équivalents ailleurs

| Moteur | Équivalent | Note |
|---|---|---|
| **Unreal** | Les modules `Editor` d'un plugin, et l'Unreal Editor | Un module Editor « will only be loaded when the editor is starting up » (**documenté**, ADR-0034 [12]) ; l'éditeur est un exécutable à part du jeu livré (**supposé**, d'après la structure des cibles `*Editor.Target.cs`). |
| **Unity** | Les scripts du dossier `Editor` | Ils « aren't available in Player builds at runtime » (**documenté**, ADR-0034 [13]) : rien de l'éditeur dans le jeu livré, comme ici. |
| **Godot** | L'éditeur, un programme du moteur | Il tourne sur le moteur et son UI (**documenté**, ADR-0032) ; le jeu s'exporte avec des modèles d'export compilés sans l'éditeur (**supposé**). |
