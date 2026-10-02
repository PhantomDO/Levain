# Pièges — session, PR, journal, board

Un piège par entrée : symptôme, cause, parade. Le plus récent en haut.

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
- **Parade** : une PR par issue, en branches empilées (B part de A). La règle n°1 interdit d'ouvrir B avant la
  fusion de A : B reste poussée sans PR. Après la fusion de A **en squash**, les commits de A n'existent plus
  sur `main` : `git rebase --onto main <dernier commit de A>` sur B, sinon le rebase rejoue A.
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
