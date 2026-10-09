# Pièges — session, PR, journal, board

Un piège par entrée : symptôme, cause, parade. Le plus récent en haut.

## `gh project item-edit --number 1.1` est refusé : un décimal s'écrit en littéral dans la mutation (2026-10-09)

- **Symptôme** : `gh project item-edit --project-id … --id … --field-id … --number 1.1`, pour le « Passé (h) » de #18,
  répond « Column value must be a valid value for number column » (`gh` 2.46.0). La parade qui vient d'abord,
  `gh api graphql -F v=1.1` avec une variable, envoie le nombre comme une chaîne, et GitHub la refuse de même.
- **Cause** (**supposée**) : `--number` est un `float32` (`gh project item-edit --help`) et `gh api -F` ne convertit
  que `true`, `false`, `null` et les entiers (`gh api --help`) : le décimal arrive sous une forme que la colonne
  numérique ne prend pas. Non élucidé côté GitHub.
- **Parade** : écrire la valeur en littéral dans la mutation, sans variable, puis relire la colonne
  (`gh project item-list 1 --owner @me --format json --limit 200`, champ `passé (h)`) :

  ```bash
  gh api graphql -f query='
    mutation {
      updateProjectV2ItemFieldValue(input: {
        projectId: "<PROJECT_ID>", itemId: "<ITEM_ID>", fieldId: "<FIELD_ID>", value: {number: 1.1}
      }) { projectV2Item { id } }
    }'
  ```

## Une entrée écrite avant les réponses de Donnovan garde ses « à demander » (2026-10-09)

- **Symptôme** : la relecture a trouvé, dans l'entrée du jour, « Temps Donnovan : à demander », une règle « à voter »,
  une décision « à poser », des « reste à faire » faits, et des sommes qui ne sortaient plus de la commande citée
  (4,55 h au lieu de 4,70 h).
- **Cause** : l'entrée s'écrit avant les réponses, et chaque amendement change des faits sans relire le reste.
- **Parade** : à chaque amendement, chercher ces marques dans l'entrée du jour (vu rouge sur l'ancienne version de
  l'entrée, 5 lignes, vert sur la nouvelle) et relancer la commande de chaque somme citée :

  ```bash
  awk '/^## 2026-10-09/{f=1} /^## 2026-10-08/{f=0} f' docs/JOURNAL.md \
    | grep -nE "à demander|à voter|À poser|à poser|reste à faire|à remplir|à décider|à rédiger"
  ```

- **La recherche ne voit pas un fait qui a bougé** (2026-10-09, l'après-midi) : #366 à #370, dites ouvertes, étaient
  fusionnées, et « Prochaine étape » les fusionnait encore. Parade : relire l'état des PR juste avant de pousser
  (`gh pr list --state all --json number,state,mergedAt`) et chaque nombre de PR de l'entrée.

## Le board demande le droit `project`, et `gh auth refresh` un terminal (2026-10-08)

- **Symptôme** : `gh project item-list` répond « missing required scopes [read:project] » dans la distro WSL ;
  `gh auth refresh -s project`, lancé par `!`, répond « --hostname required when not running interactively ».
- **Cause** : `gh auth login` ne demande que les droits par défaut (`repo`, `read:org`, `gist`) ; `gh auth refresh`
  sans terminal exige `--hostname`.
- **Parade** : lancer `gh auth refresh --hostname github.com --scopes project` en tâche de fond, lire dans sa
  sortie le code à usage unique, et le donner à Donnovan, qui le saisit sur <https://github.com/login/device>
  depuis n'importe quel appareil ; la commande finit seule.

## La session distante montre le dossier où Claude Code a été lancé (2026-10-08)

- **Symptôme** : Donnovan ne voit pas les changements d'un worktree dans la session Remote Control ; elle montrait
  435 fichiers « modifiés ».
- **Cause** : la session avait été lancée depuis `/mnt/c/…/Projects/Levain`, une ancienne copie Windows aux fins
  de ligne CRLF ; le travail était dans `~/Projects/Levain` et ses worktrees, un autre dépôt.
- **Parade** : lancer `claude --remote-control` depuis `~/Projects/Levain` (`tools/wsl/README.md`) ; un worktree
  de ce dépôt s'ouvre alors par `EnterWorktree`.

## Supprimer la base d'une PR empilée la ferme (2026-10-05)

- **Symptôme** : `gh pr merge 269 --merge --delete-branch`, puis #271, empilée sur sa branche, ne passe pas à
  `main` : elle est **fermée**, et `gh pr edit --base main` la refuse (« closed pull request »).
- **Cause** (**supposée**) : la doc de GitHub annonce la redirection quand la branche de tête est supprimée après
  la fusion ; supprimée par l'API, comme le fait `gh`, elle a fermé la PR. À essayer une fois avec le réglage
  `delete_branch_on_merge`. Attendre ne sert à rien.
- **Parade** : `tools/merge-stack.sh`, qui passe la base de la suivante à `main` **avant** de supprimer la
  branche. Pour rattraper : repousser la branche supprimée à son SHA (`git push origin <sha>:refs/heads/<branche>`),
  `gh pr reopen`, `gh api -X PATCH repos/{owner}/{repo}/pulls/N -f base=main` (`gh pr edit --base` échoue avec
  le `gh` de la distro WSL, pr-autonome/GOTCHA.md), puis supprimer de nouveau la branche restaurée. La réouverture
  relance la CI (`reopened`), pas le changement de base.

## Modifier une branche au milieu d'une pile : la propager tout de suite (2026-10-05)

- **Symptôme** : après plusieurs `commit --amend` sur la branche C d'une pile B → C → D → E, un rebase de D et
  E « sur C » rejoue le commit de B et s'arrête sur des conflits ; D et E reposaient en fait sur une version de
  C antérieure aux corrections de relecture, qui manquaient donc au sommet de la pile.
- **Cause** : `git rebase --update-refs` ne déplace que les branches qui sont des ancêtres de celle qu'on
  rebase. Une C modifiée à part n'est plus l'ancêtre de E : la référence suivante l'a ignorée.
- **Parade** : après chaque `--amend` d'une branche du milieu, noter son SHA d'avant
  (`OLD=$(git rev-parse HEAD)` avant l'amend), puis tout de suite
  `git rebase --update-refs --onto <C> $OLD <sommet>`. Vérifier la pile :
  `for b in …; do echo "$b parent=$(git rev-parse --short $b~1)"; done`. Pour réparer : `git rebase --abort`,
  reconstruire C par `cherry-pick` sur le bon B, puis rebaser le reste depuis l'ancienne C de la chaîne.

## Rebaser une pile après la fusion de sa base : la branche n'existe plus (2026-10-03)

- **Symptôme** : `git rebase --onto origin/main $(git log -1 --format=%H <branche fusionnée>) <suivante>` échoue
  sur « argument ambigu », et la PR suivante s'ouvre avec le commit déjà fusionné en double.
- **Cause** : `gh pr merge --delete-branch` supprime aussi la branche locale de la base ; la variable est vide, et
  la commande composée continue.
- **Parade** : noter le SHA de la base **avant** la fusion, ou le reprendre dans `git log` de la suivante (le
  commit juste sous le sien), puis `git rebase --onto origin/main <sha> <suivante>` et
  `git push --force-with-lease`. Vérifier les fichiers de la PR (`gh pr view --json files`) avant d'attendre la CI.

## `gh pr merge --delete-branch` supprime le worktree de la branche (2026-10-02)

- **Symptôme** : après la fusion de la dernière PR d'une pile, le worktree `../Levain-pbr` où elle était sortie
  a disparu, son dossier de build avec.
- **Cause** : `gh pr merge --delete-branch` supprime la branche locale, et le worktree qui la porte avec elle.
- **Parade** : rien de perdu si tout est poussé. Pour garder un worktree, y sortir une autre branche avant de
  fusionner, ou fusionner sans `--delete-branch`. Une pile empilée se rebase depuis le worktree où sont ses
  branches (git refuse de sortir une branche déjà sortie ailleurs).

## `git checkout <fichier>` efface le travail non indexé (2026-09-22)

- **Symptôme** : après avoir retiré une ligne pour vérifier qu'un test échoue sans elle, `git checkout
  engine/scene/src/scene.cpp` a rendu le fichier de `main` : une heure de travail non commité perdue (réécrite
  de mémoire).
- **Cause** : `git checkout <fichier>` restaure depuis l'index, qui ne contenait rien pour ce fichier.
- **Parade** : pour un essai destructif, copier le fichier d'abord (`cp fichier /tmp/…`), ou commiter avant.
  `git restore` a le même effet : c'est l'index qui compte, pas la commande.

## `git stash` et `git add -N` (2026-09-21)

- **Symptôme** : « Entry … not uptodate. Cannot merge » ; rien n'est remisé.
- **Cause** : des fichiers marqués « à ajouter » par `git add -N` (utilisé pour mesurer une PR avec
  `git diff --numstat`) bloquent le remisage.
- **Parade** : sauvegarder l'arbre, puis `git reset` pour vider l'index avant `git stash -u`. Après une mesure,
  toujours finir par `git reset`.

## Heredoc sans guillemets dans une description de PR (2026-09-21)

- **Symptôme** : « permission non accordée : docs/ROADMAP.md » à la création de #71, et les noms de fichiers entre
  accents graves disparus de la description.
- **Cause** : `<<EOF` sans guillemets, pour y glisser une variable : le shell a exécuté chaque passage entre accents
  graves comme une commande.
- **Parade** : toujours `<<'EOF'`. Pour un texte calculé, l'écrire dans un fichier avec Python, puis
  `gh pr create --body-file`.

## Une PR trop grosse se découpe en branches empilées (2026-09-21)

- **Exception** : le code Vulkan de base (#55, 549 lignes ; #56, 545) ne se découpe pas sans étapes qui compilent
  mais n'affichent rien. Donnovan l'accepte tant que le code reste lisible et que l'écart est signalé (règle
  n°2 d'`AGENTS.md`). Ce qui se découpe proprement se découpe toujours : l'ADR-0012 est parti seul (#54).

- **Symptôme** : M1.1 complet faisait ~710 lignes, près du double de la règle n°2.
- **Parade** : une PR par issue, en branches empilées (B part de A). Depuis le 2026-10-05 (exception à la
  règle n°1 d'`AGENTS.md`), B s'ouvre en même temps que A, avec A pour base, et elles fusionnent en **merge
  commit** : B n'a pas à être rebasée. Avant, A fusionnait **en squash** : ses commits n'existaient plus sur
  `main`, et il fallait `git rebase --onto main <dernier commit de A>` sur B, sinon le rebase rejouait A. C'est
  toujours le cas pour une PR fusionnée seule en squash.
- **`gh pr merge --delete-branch` supprime aussi la branche locale** (2026-09-22) : le rebase de B qui la
  nommait échoue (« amont invalide »). Noter le hash du dernier commit de A avant la fusion, et rebaser sur lui.

## Mesurer avant d'annoncer un chiffre (2026-09-20, 2026-09-21)

- **Symptôme** : « environ 300 lignes » annoncées pour l'infrastructure de l'ADR-0011, 347 mesurées ;
  « 4 avertissements » annoncés pour `-Wall -Wextra`, 5 en réalité, parce que seul le Debug avait été compilé ;
  « 83 lignes » et « 25 pièges » écrits dans la PR de ce fichier même, 68 et 22 mesurés.
- **Parade** : mesurer d'abord, sur **tous** les presets, puis annoncer. Un chiffre non mesuré se dit comme tel.

## Le temps de Donnovan est son temps total (2026-09-20, 2026-09-21)

- **Symptôme** : la phase 0 semblait coûter 3,0 h ; elle en avait coûté 4,9. Le ratio de 0,50 aurait amputé la
  roadmap d'environ 19 h. Le 21/09, M1.1 semblait coûter 40 min (ratio 0,44) ; la journée en avait pris 75.
- **Cause** : d'abord la question portait sur la relecture seule. Puis, posée après chaque PR (« depuis ta
  dernière réponse »), elle oubliait le temps passé entre deux, en allers-retours.
- **Parade** : demander le temps **total** sur le projet, et en fin de journée **le total depuis le matin**.
  Réconcilier le board sur ce total (écart réparti au prorata des issues).
- **Le 22/09, M3.3 déclarait 0,33 h et en avait coûté 1,33** (×4) : entre les deux PR, Donnovan avait posé
  deux questions de conception et ouvert le sujet du jeu visé. Une question, un sondage, une discussion de
  cadrage sont du temps Donnovan : ne jamais présenter le déclaré par PR comme autre chose qu'un provisoire.
- **Le même jour, M3.4 : 0,42 h déclarées, 1,00 h réelles** (×2,4). Le provisoire donnait un ratio de phase de
  0,71, sous la fourchette, et j'ai annoncé à Donnovan un recalibrage possible de toute la roadmap — pour
  rien : réconcilié, le ratio remonte à 0,87. **Ne pas tirer de conclusion de phase sur des chiffres
  provisoires**, et le dire quand on en parle quand même.

## Numéros d'ADR (2026-09-20)

- **Symptôme** : deux collisions (0009 et 0012 déjà pris par des ADR « prévus » dans la ROADMAP).
- **Parade** : ne jamais réserver de numéro à l'avance. Le numéro se prend à l'écriture : `ls docs/adr/`.

## Labels et milestones GitHub (2026-09-20)

- **Labels** : n'utiliser que des labels existants (`gh label list`). `type:docs` n'existe pas : c'est
  `type:infra` + `area:docs`.
- **Milestone fermé** : `gh issue edit --milestone "<titre>"` ne trouve pas un milestone fermé. Passer par l'API :
  `gh api -X PATCH repos/PhantomDO/Levain/issues/<n> -F milestone=<numéro>`.

## Un revert emporte aussi la documentation (2026-09-20)

- **Symptôme** : le revert du passage à Rust a aussi retiré l'entrée du journal et l'ADR-0010.
- **Parade** : après un `git revert`, restaurer depuis `main` ce qui doit rester (`git checkout main -- docs/…`).

## Réglages du dépôt (2026-09-21)

- La protection de `main` exige les checks `linux-debug`, `linux-release`, `linux-asan` : ce sont les noms des
  entrées de la matrice CI. Renommer un preset impose de mettre la protection à jour d'abord.
- Modifier la protection est un réglage du dépôt : **seulement avec l'accord de Donnovan**. Ajouter un check
  après qu'il a tourné au moins une fois, pour être sûr de son nom :
  `gh api -X PATCH repos/PhantomDO/Levain/branches/main/protection/required_status_checks --input <json>`
  (`app_id` 15368 = GitHub Actions).

## Bugs en amont (2026-09-21)

- **Ne rien signaler à SDL** : ses mainteneurs n'acceptent pas les contributions d'IA (décision de Donnovan).
  Documenter le contournement dans le README du module et le journal. Pour un autre projet, demander d'abord.
