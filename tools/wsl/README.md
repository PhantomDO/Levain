# Levain sous Windows, dans une distro WSL

Une distro WSL dédiée, `levain-dev` : Ubuntu 26.04, la même chaîne que la CI et que la distrobox `dev-ubuntu` de la
machine de référence, avec Claude Code en ligne de commande. Elle vit dans son propre dossier et se supprime d'une
commande, sans rien laisser sous Windows.

Le moteur ne se construit pas sous Windows : il se compile sous Linux, y compris son binaire Windows, par clang-cl
(ADR-0035). WSL est le chemin le plus court pour travailler depuis un PC Windows, et l'exe compilé dans la distro
se lance sous Windows depuis son terminal.

## Installer

Prérequis : Windows 10 ou 11, WSL 2 en version 2.4.4 au moins (`wsl --update`). Sans WSL du tout, dans un terminal
administrateur : `wsl --install --no-distribution`, puis redémarrer.

Les deux fichiers de ce dossier côte à côte, puis dans PowerShell :

```powershell
powershell -ExecutionPolicy Bypass -File .\levain-wsl.ps1
```

Le script :

1. crée la distro `levain-dev` (`Ubuntu-26.04`) dans `%LOCALAPPDATA%\levain-dev` (un dossier par `-Name`) ;
2. y lance `provision-levain.sh`, en root : les paquets de l'étape « Outils » de la CI, LLVM 23 (apt.llvm.org),
   vcpkg et emsdk aux versions de `ci.yml`, Claude Code (son installeur natif), les dépôts Levain et Rando dans
   `~/Projects`, et les assets de test (`tools/fetch-assets.sh`, sans lesquels le build web ne se configure pas) ;
3. fait un premier build Debug et ses tests. Long la première fois : vcpkg compile toutes les dépendances.
   `-SkipFirstBuild` le saute.

Options : `-Name` (autre nom de distro), `-Distro` (si `Ubuntu-26.04` n'est pas proposée : `wsl --list --online`),
`-Location` (autre dossier), `-User` (nom Linux, sinon celui de Windows).

Les versions de `provision-levain.sh` suivent `.github/workflows/ci.yml` (`VCPKG_TAG`, `LLVM_VERSION`,
`EMSDK_VERSION`) : les changer ensemble.

## Se connecter

```bash
wsl -d levain-dev
git config --global user.name "…" && git config --global user.email "…"
gh auth login    # pour pousser
claude           # Claude Code
```

Au premier `wsl -d levain-dev`, Ubuntu peut encore proposer sa configuration de premier lancement, que
`--no-launch` a sautée (pas vérifié) : l'utilisateur existe déjà, le script l'a créé.

Claude Code ouvre le navigateur pour la connexion : s'y connecter avec le compte voulu. Sous WSL, si rien ne
s'ouvre, la touche `c` copie l'URL, à ouvrir dans le navigateur de Windows ; le navigateur affiche alors un code, à
coller dans le terminal à « Paste code here if prompted ». `/status` dit quel compte est connecté ; `/logout` puis
`/login` en change. Les identifiants restent dans la distro (`~/.claude/.credentials.json`), à part de ceux de
Windows.

Piège : `ANTHROPIC_API_KEY` ou `CLAUDE_CODE_OAUTH_TOKEN`, s'ils sont définis dans la distro, passent avant le
compte du `/login` (documentation de Claude Code, « Authentication »). Le script n'en définit aucun.

## Piloter la session à distance

Depuis le web (claude.ai/code) ou l'application Claude sur le téléphone, par Remote Control (documentation de
Claude Code, « Remote Control ») :

```bash
cd ~/Projects/Levain && claude --remote-control "Levain"
```

Le pied de l'invite affiche `/rc active` ; `/remote-control` ouvre le panneau, avec l'URL et le QR code. La
session apparaît sous son nom. Le terminal reste ouvert : le processus
`claude` tourne dans la distro. Les demandes de permission attendent qu'on y réponde, à distance aussi.
Prérequis : un abonnement Pro ou Max, et la connexion par `/login` ; une clé d'API, `ANTHROPIC_BASE_URL` vers un
proxy ou `CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC` la bloquent.

Après un `wsl --shutdown`, la conversation n'est pas perdue : son historique est sur le disque de la distro.
`claude --continue` reprend la dernière (du même dossier), `claude --resume` en propose une liste, puis
`/remote-control` (ou `/rc`) la reconnecte avec son historique. `/config` propose de le faire au démarrage
(`remoteControlAtStartup`).

## Voir les résultats depuis Windows

- **Les fichiers** : `\\wsl.localhost\levain-dev\home\<utilisateur>\Projects\Levain` dans l'Explorateur, ou
  `explorer.exe .` depuis la distro. Les captures du moteur (`--capture`) sont des PNG à ouvrir là.
- **La page web, sur la vraie carte graphique** : le build `web` servi depuis la distro, ouvert dans Edge ou Chrome
  sous Windows, où WebGPU tourne sur le GPU. WSL relaie `localhost` :

  ```bash
  source ~/emsdk/emsdk_env.sh && cmake --preset web && cmake --build --preset web
  python3 -m http.server -d build/web/sandbox 8000
  cmd.exe /c start http://localhost:8000/levain_sandbox.html
  ```

- **Le sandbox natif en fenêtre** : WSLg affiche les fenêtres Linux sur le bureau Windows, mais le rendu passe par
  Vulkan logiciel : lent, et sans valeur de mesure.

Garder les dépôts dans la distro, pas sous `/mnt/c` : les fichiers de Windows vus de WSL sont lents, et la
surveillance des fichiers y marche mal.

## Les limites

- **Pas de GPU Linux sous WSL pour ce moteur** : les tests Vulkan tournent sur lavapipe, le Vulkan logiciel de
  Mesa, comme dans la CI. Le script le règle dans `~/.bashrc` (`VK_DRIVER_FILES`, et `LEVAIN_VK_PRELOAD`, que lit
  `tools/verify.sh`). Les mesures de performance restent celles de la machine de référence (SPECS §10).
- **La mémoire** : WSL prend la moitié de la RAM par défaut, pour toutes les distros à la fois. Le premier build,
  où vcpkg compile Dawn, en demande beaucoup. Pour en donner plus, dans `%USERPROFILE%\.wslconfig` :

  ```ini
  [wsl2]
  memory=48GB
  ```

- **Deux machines sur un même dépôt** : un agent à la fois ouvre des PR (AGENTS.md, règle n°1).
- **Pas essayé sous Windows** au moment de l'écrire (2026-10-08) : la première installation dira ce qui manque, et
  `-Remove` rend un nouvel essai gratuit.

## Supprimer

```powershell
powershell -ExecutionPolicy Bypass -File .\levain-wsl.ps1 -Remove
```

`wsl --unregister levain-dev` efface la distro et son disque virtuel, puis le dossier, s'il est vide : un dossier
`-Location` choisi à la main et qui contient autre chose reste tel quel, rien n'y est effacé.
