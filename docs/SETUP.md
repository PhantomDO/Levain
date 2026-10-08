# Mise en route

Temps total estimé : environ 1 h, qui compte dans M0.1.

## 1. Valider les specs (≈ 45 min)

Lire et amender si besoin :

1. `docs/SPECS.md`
2. `docs/ROADMAP.md`
3. `docs/adr/0001` à `0007`
4. `docs/etudes/E1-rhi.md` (facultatif à ce stade, utile avant la phase 1)

Puis renseigner la **machine de référence** (SPECS §10), choisir le **nom** du moteur et une **licence**.

Commandes utiles sous Linux pour la machine de référence :

```bash
lscpu | grep "Model name"
vulkaninfo --summary          # GPU, pilote, version de Vulkan
free -h
```

## 2. Outils à installer

| Outil | Rôle |
|---|---|
| git, gh (GitHub CLI ; sous CachyOS : `sudo pacman -S github-cli`) | Dépôt et suivi |
| Steam avec Proton | Lancer les binaires Windows sur la machine de référence (dès M1.4) |
| CMake 3.28 ou plus, Ninja | Build |
| Clang et LLVM 23 (`clang-cl`, `lld-link`, `llvm-lib`, `llvm-rc`, `llvm-mt`) ; pour Windows, la STL et le SDK de Microsoft dans un winsysroot (ADR-0035) | Compilateurs ; Windows se compile depuis Linux |
| Vulkan SDK de LunarG | Validation layers, `vulkaninfo`, `slangc`, `dxc` |
| RenderDoc | Débogage GPU |
| Tracy (profiler) | Profiling |
| Claude Code | Développement |

vcpkg sera installé et configuré en M0.2 ; NVRHI et flecs arrivent par vcpkg.

**Compiler pour Windows** (ADR-0035) : clang-cl lit la STL et le SDK de Microsoft dans un *winsysroot*, la
disposition de Visual Studio (`VC/Tools/MSVC` et `Windows Kits/10`), que la variable `LEVAIN_WINSYSROOT` désigne.
Sans elle, la configuration refuse, et l'étape `windows-debug` de `tools/verify.sh` échoue en la nommant ;
`NO_WINDOWS=1` saute l'étape.

```bash
export LEVAIN_WINSYSROOT=~/winsysroot   # deux liens vers VC et « Windows Kits » d'un Visual Studio ou de ses Build Tools
cmake --preset windows-debug && cmake --build --preset windows-debug   # vcpkg compile les dépendances pour Windows
```

Sur un PC Windows, la distro WSL ci-dessous fait ces liens toute seule. Sur la machine de référence, le winsysroot
viendra de xwin, par le script de la CI (#346) : d'ici là, `NO_WINDOWS=1 tools/verify.sh`.

Depuis un PC Windows : une distro WSL dédiée, outillée comme la CI, avec Claude Code en ligne de commande, et
supprimable d'une commande ([`tools/wsl/README.md`](../tools/wsl/README.md)).

## 3. Créer le dépôt et le board (≈ 10 min)

Claude Code peut faire toute cette étape, sauf la connexion à GitHub (`gh auth login` et
`gh auth refresh -s project`), qui passe par le navigateur : à faire toi-même, une fois. Ensuite, dans Claude Code
à la racine du dossier :

> Lis CLAUDE.md et docs/SETUP.md, puis fais l'étape 3 : modèles GitHub, `git init`, premier commit. Lance
> `vulkaninfo --summary` pour compléter la machine de référence dans SPECS §10. Puis lance
> `DRY_RUN=1 ./tools/github-bootstrap.sh Levain` et montre-moi le résultat avant de lancer pour de vrai.

Les commandes, si tu préfères les lancer toi-même :

```bash
cd ~/Projects/Levain            # le dossier qui contient le kit

# Les outils distants de Claude ne peuvent pas écrire dans .github/ : les modèles d'issue et de PR
# sont livrés dans tools/github-templates/. À faire une seule fois, s'il existe :
mkdir -p .github && cp -r tools/github-templates/. .github/ && rm -r tools/github-templates

git init -b main
git add . && git commit -m "docs: initial specs, roadmap and ADRs"

gh auth login                  # si ce n'est pas déjà fait
gh auth refresh -s project     # droit de créer un GitHub Project

DRY_RUN=1 ./tools/github-bootstrap.sh <nom-du-moteur>   # affiche ce qui sera fait
./tools/github-bootstrap.sh <nom-du-moteur>             # crée tout
```

Le script crée le dépôt public, les labels, les 35 milestones avec leurs échéances, le board avec ses champs,
et les issues des phases 0 et 1 avec leurs estimations. On peut le relancer : il saute ce qui existe déjà.

## 4. Lancer un binaire Windows sous Proton (dès M1.4)

1. Récupérer l'artefact Windows de la dernière CI : `gh run download --name <artefact>` (ou l'onglet Actions).
2. Dans Steam : *Ajouter un jeu* → *Ajouter un jeu non-Steam*, choisir l'exécutable.
3. Propriétés du raccourci → *Compatibilité* → forcer une version de Proton ; options de lancement :
   `--api d3d12`.

## 5. Recevoir les notifications

- Sur la page du dépôt : **Watch → All Activity**.
- Application mobile GitHub : notifications de PR, de releases et d'issues.

## 6. Lancer la première session de code

À la racine du dépôt :

```bash
claude
```

puis : « Lis CLAUDE.md et démarre M0.2. »
