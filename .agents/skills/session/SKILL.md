---
name: session
description: Rituel d'une session de travail sur Levain — état de départ, branche, commits, ouverture de la PR, journal, board GitHub et temps de Donnovan. À utiliser au début et à la fin de chaque session, et à chaque PR.
---

# Session de travail

Lire d'abord [`GOTCHA.md`](GOTCHA.md).

## Au début

1. Lire la dernière entrée de `docs/JOURNAL.md` et le milestone en cours dans `docs/ROADMAP.md`.
2. Vérifier l'état : `gh pr list`, `gh issue list --milestone "<milestone en cours>"`.
3. Annoncer en 3 lignes : l'issue visée, ce qui va être fait, l'estimation de temps de relecture pour Donnovan.

## Pendant

- Branche `m<phase>.<n>/<sujet>`, commits en Conventional Commits (en anglais).
- Le code suit la section « Écrire le code » d'`AGENTS.md`.
- Mesurer la taille de la PR **avant** de l'annoncer : `git add -A -N && git diff --numstat main`.

## À la fin

1. Mesurer les critères du milestone et noter les commandes utilisées.
2. Ouvrir la PR avec le modèle `.github/pull_request_template.md`. Le **guide de lecture** est la partie la plus
   importante : fichiers dans l'ordre, et pour chacun ce qu'il faut y comprendre. En mode autonome, la boucle
   entière (worktree, vérification, contre-tests, relecture, pile, fusion) est dans le skill
   [`pr-autonome`](../pr-autonome/SKILL.md).
3. Ajouter une entrée à `docs/JOURNAL.md` (format dans le fichier), la plus récente en haut.
4. Mettre à jour le board : Status, et « Passé (h) » dès que Donnovan donne son temps.
5. Toujours demander à Donnovan, à la fin de la session : « **Combien de temps as-tu passé sur le projet en
   tout ?** » — et non « combien sur la relecture ». Les « Heures Donnovan » de la ROADMAP comptent *tout* son
   engagement : pilotage, questions, décisions, relecture. En fin de journée, demander aussi **le total depuis
   le début** et réconcilier le board : les réponses « depuis la dernière fois » oublient le temps entre deux.

## Board GitHub

Le board est le GitHub Project n°1, « Levain — Roadmap », avec les champs **Status** (Todo, In Progress, Done),
**Estimé (h)**, **Passé (h)**, **Phase**. Il a été créé par `tools/github-bootstrap.sh`.

```bash
gh project field-list 1 --owner @me --format json             # IDs des champs et des options
gh project item-list 1 --owner @me --format json --limit 200  # IDs des items
gh project item-edit --project-id <PROJECT_ID> --id <ITEM_ID> --field-id <FIELD_ID> --number 0.5
gh project item-edit --project-id <PROJECT_ID> --id <ITEM_ID> --field-id <FIELD_ID> --single-select-option-id <OPTION_ID>
```

Une issue fermée par une PR (`Closes #N`) passe seule en Done.
