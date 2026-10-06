# `engine/app`

## Rôle

Le socle des programmes du moteur ([ADR-0029](../../docs/adr/0029-module-app.md)) : ce que le sandbox, *Rando*
et plus tard l'éditeur partagent, au lieu de le réécrire chacun. Il est au-dessus de tous les autres modules
(SPECS §7), et sous les programmes.

**État en M6.4 (en cours)** : les **modèles glTF sur le GPU**, sortis du sandbox :

- leurs meshes, skinnés ou non, et leurs textures, cuites si possible (ADR-0020) ;
- un binding set par matériau ;
- l'animation des modèles skinnés, par leur clip ou leur animateur (ADR-0022), avec la mesure de son coût ;
- le hot-reload de leurs textures (ADR-0021).

La boucle, la fenêtre, la page web et la caméra du rendu y arrivent à la PR suivante (#300).

## Invariants

1. **Aucun plugin** : le module ne lie jamais un plugin (ADR-0018, vérifié par `cmake.plugins.*`). Les plugins
   s'inscrivent dans le renderer que le programme crée, comme ils le font déjà (ADR-0025).
2. **Un modèle par asset** : `ModelGpu` est rangé par GUID. Un même modèle instancié deux fois partage ses
   meshes, ses textures et son animateur ; deux renards qui courent chacun à leur vitesse demanderont un
   animateur par instance.
3. **Le mouvement d'un modèle animé vient de l'appelant** (`MotionOf`) : le module ne sait pas ce qu'est un
   joueur. Le sandbox donne au renard celui du joueur, aux autres une vitesse de démonstration.
4. **Un envoi qui échoue est soumis quand même** (`submitAbandonedUpload`) : une command list ouverte et
   détruite fuirait jusqu'à la destruction du device.

## Points d'entrée

| Fonction | Rôle |
|---|---|
| `uploadModel`, `bindModelMaterials` | Un modèle lu par `assets`, prêt à dessiner |
| `uploadTexture`, `textureTargetOf`, `textureLevelsOf` | Une texture, au format que le GPU échantillonne |
| `animateModels` | Les poses de l'image, puis le skinning sur le GPU |
| `startTextureReload`, `reloadChangedTextures` | Le hot-reload des textures |
| `isSkinned`, `clipIndexOf` | Ce qu'il faut savoir d'un modèle avant de l'envoyer |

## Dans les autres moteurs

- **Unreal** : le module `Launch` (`FEngineLoop`) et la classe `UGameEngine` portent la boucle ; les modèles
  sur le GPU sont les *render proxies* des `UStaticMeshComponent` et `USkeletalMeshComponent`.
- **Unity** : le *Player*, un exécutable précompilé, et sa boucle (`PlayerLoop`) ; un modèle se dessine par
  son `MeshRenderer` ou son `SkinnedMeshRenderer`.
- **Godot** : `Main` et `SceneTree`, la boucle par défaut ; un modèle est un `MeshInstance3D` sous un
  `Skeleton3D`.
