# ADR-0011 — Retour au C++, Linux d'abord, écrit pour la lisibilité

- **Statut** : accepté le 2026-09-20 ; § 1 modifié par [ADR-0035](0035-windows-compile-depuis-linux.md) : Windows
  revient, compilé depuis Linux par clang-cl
- **Date** : 2026-09-20
- **Milestone** : M0.5
- **Remplace** : [ADR-0010](0010-passage-a-rust.md) (passage à Rust)
- **Remet en vigueur** : [ADR-0001](0001-langage-cpp23.md), [ADR-0002](0002-nvrhi.md),
  [ADR-0003](0003-plateforme-sdl3.md), [ADR-0004](0004-ecs-flecs.md), [ADR-0005](0005-shaders-slang.md),
  [ADR-0007](0007-build-cmake-vcpkg.md), et la partie nommage de [ADR-0009](0009-style-cpp.md)

## Contexte

L'ADR-0010 faisait passer le projet à Rust, sur la foi de deux mesures. **Les deux étaient biaisées contre le
C++**, et il faut l'écrire clairement.

### Biais n°1 — la manche 1 comparait flecs à bevy_ecs, pas C++ à Rust

Donnovan a trouvé le Rust plus lisible parce que les dépendances d'un système y sont dans la signature
(`Res<InputState>`, `Query<(&mut Transform, &FpsController)>`), là où le C++ les cachait dans le corps
(`it.world().get<InputState>()`).

Mais cette différence vient de **bevy_ecs**, pas de Rust. Le C++ que j'avais écrit était handicapé par la glu
flecs : signature de lambda `[](flecs::iter& it, size_t, …)` avec son paramètre anonyme, et dépendance
récupérée à la main dans le corps. Rien n'obligeait à écrire ça. Une fonction libre dont toutes les dépendances
sont des paramètres a exactement la propriété que Donnovan a aimée.

**Je n'ai pas écrit le meilleur C++ possible, et j'ai laissé la comparaison pencher.**

### Biais n°2 — la manche 3 facturait au C++ un Windows dont le projet n'a pas besoin

Les 435 lignes d'infrastructure C++ comptaient les presets `windows-*`, `vcvars`/`vswhere`, le bloc
`/Zc:__cplusplus` et deux jobs de CI. Donnovan développe sous Linux et n'a pas de machine Windows.

Plus net encore : **les deux seuls bugs rencontrés pendant M0.2 étaient des bugs Windows** — `__cplusplus` figé
à 199711 par MSVC, et `<ostream>` non inclus en cascade par la STL de Microsoft. En périmètre Linux, ni l'un ni
l'autre n'existe.

### Ce qui reste vrai de l'ADR-0010

- Le Rust s'est bien lu. Ce n'est pas remis en cause : c'est l'étalon que le C++ doit maintenant atteindre.
- `cargo` est plus simple que CMake + vcpkg. Reste vrai, mais l'écart se réduit en périmètre Linux.
- La méthode — **choisir en lisant du code, pas en argumentant** — est validée deux fois et conservée.

## Décision

**Retour au C++23**, avec trois changements par rapport à la v0.3.

### 1. Linux d'abord

Le projet cible **Linux et Clang**. Les presets et les jobs de CI Windows sont retirés, ainsi que le bloc MSVC du
`CMakeLists.txt`. Le C++ reste portable : Windows se réajoute quand une machine sera disponible, avec sa CI.
Le milestone M1.4 (backend Direct3D 12) reste hors périmètre pour la même raison.

**Quand Windows reviendra, le compilateur sera à rechoisir.** L'ADR-0001 posait « MSVC sous Windows » sans le
justifier ; c'était le choix par défaut. `clang-cl` est la première option à évaluer : il ne règle ni le piège
`__cplusplus` (qu'il reproduit volontairement) ni la divergence de STL (il consomme celle de Microsoft), mais il
donne **exactement C++23 sur les deux plateformes** au lieu du sur-ensemble `/std:c++latest`, et unifie
diagnostics, clang-tidy et clang-format. Analyse complète et sources dans `docs/QA.md`.

Conséquence mesurée après application : l'infrastructure de build et CI passe de **435 à 347 lignes**
(`wc -l` sur les neuf fichiers). Le gain est plus modeste que les « environ 300 » que j'avais annoncés avant de
mesurer — la moitié des 435 lignes était du format et du lint, indépendants de la plateforme. **Ce qui disparaît
vraiment, ce n'est pas du volume, c'est la part qui causait les pannes** : `vcvars`, `vswhere`,
`/Zc:__cplusplus` et les deux bugs de M0.2.

### 2. Style de la logique — variante C

L'ADR-0009 fixait le nommage et la mise en forme. Il lui manquait la **forme du code**, qui est ce qui pèse
réellement sur la lecture. Trois règles, choisies par Donnovan sur lecture de trois variantes :

**a. La logique s'écrit en fonctions libres, toutes dépendances dans la signature.**

```cpp
void applyFpsInput(Transform& transform, const FpsController& controller,
                   const InputState& input, float deltaSeconds);
```

Pas de singleton récupéré dans le corps, pas d'état caché, pas de `world` implicite. Ce que la fonction touche
se lit dans sa déclaration. C'est la propriété que Donnovan a trouvée supérieure dans bevy_ecs, et elle
n'appartient pas à Rust.

**b. La glu ECS est confinée à une ligne, loin de la logique.**

```cpp
world.system<Transform, const FpsController>("FpsMovement")
    .each([](flecs::iter& it, size_t, Transform& t, const FpsController& c)
          { applyFpsInput(t, c, *it.world().get<InputState>(), it.delta_time()); });
```

Le bruit de l'API existe toujours ; il est simplement rangé là où on ne le relit jamais. Le fichier de logique
ne contient pas un seul symbole flecs.

**c. Chaque piège porte son nom.** Un calcul dont l'oubli serait un bug devient une fonction nommée.

```cpp
const auto [forward, right] = horizontalBasisFrom(transform.yaw);
const glm::vec3 direction = normalizeOrZero(forward * input.moveAxis.y + right * input.moveAxis.x);
```

`normalizeOrZero` dit qu'on ne divise pas par zéro et que la diagonale n'est pas plus rapide ; `clampPitch` dit
qu'on ne passe pas par-dessus la tête ; `horizontalBasisFrom` dit que le tangage ne participe pas au
déplacement. Un relecteur qui saute ces lignes ne rate rien — c'est le but. Le prix est de quelques
déclarations supplémentaires dans l'en-tête, et il est jugé négligeable devant le gain de relecture.

### 3. Le reste est inchangé

C++23, NVRHI, SDL3, flecs, Slang, CMake + vcpkg, doctest, clang-format, clang-tidy : tout reprend comme en
v0.3. L'ADR-0009 retrouve sa partie nommage, et garde sa partie « commentaires en français ».

## Conséquences

- **Deux migrations en une journée.** Le coût réel est le temps passé, pas le code : le dépôt n'a jamais dépassé
  107 lignes de moteur. Les deux tags `m0.2` et `m0.4` restent dans l'historique, ainsi que leurs mesures.
- **Ce que le détour par Rust a rapporté** : la preuve que le C++ que j'écrivais n'était pas le C++ le plus
  lisible possible. Sans la comparaison, la variante C n'aurait jamais été écrite. L'ADR-0009 seul n'avait pas
  suffi à la produire.
- **Le parcours de lecture Donut revient**, ainsi que l'objectif « comprendre Unreal, Unity, Godot », qui
  redevient direct puisqu'ils sont tous en C++.
- **Ce qui revient aussi** : vcpkg, les presets, la baseline à tenir à jour, clang-format et clang-tidy à
  épingler en version. Assumé.
- **Règle de conduite pour la suite** : avant de conclure qu'un langage ou une bibliothèque est plus lisible,
  vérifier qu'on a écrit la meilleure version de l'alternative. La manche 1 de l'ADR-0010 ne l'avait pas fait.

## Ce que font les autres moteurs

Inchangé depuis l'ADR-0001 : Unreal, Godot, REEngine, Anvil et Frostbite ont leur cœur en C++ ; Unity aussi,
avec le gameplay en C#. Bevy et Fyrox sont en Rust. Nous revenons donc dans le camp majoritaire, et le parcours
de lecture redevient direct.
