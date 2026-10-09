# AGENTS.md — instructions pour les agents

Ce fichier est la **source unique** des instructions du projet, quel que soit l'outil (`CLAUDE.md` ne fait que
l'importer). Les procédures détaillées vivent dans des **skills**, un dossier chacun sous `.agents/skills/`.

## Le projet

**Levain** — moteur de jeu 3D en C++23 sur **NVRHI** (backend Vulkan) et **flecs** (ECS). Namespace racine
`levain`, cibles CMake préfixées `levain_`. **Linux d'abord** : Windows revient, compilé depuis Linux par
clang-cl, en Vulkan puis en Direct3D 12 (ADR-0035, M1.4).
Priorité de Donnovan : **faire un jeu avec un moteur construit ensemble**, et comprendre au passage comment
fonctionnent les moteurs du marché (Unreal, Unity, Godot…) grâce aux études et aux lectures.

À lire avant toute session : `docs/SPECS.md`, `docs/ROADMAP.md`, la dernière entrée de `docs/JOURNAL.md`.
Références pour NVRHI : Donut et Donut-Samples (NVIDIA, MIT), à lire et adapter, pas à ajouter en dépendance.

## Les rôles

- **L'agent** : il conçoit et écrit le code, les tests, la CI, la documentation, les études. Il tient le board
  et le journal à jour.
- **Donnovan** : il décide, relit et pose des questions. Il dispose de **1 à 2 h par semaine** : chaque minute de
  son attention compte. Il est développeur C++ expérimenté (Unreal, Unity) : pas besoin d'expliquer le C++, mais
  il faut expliquer les concepts moteur. Les détails de Vulkan ne l'intéressent pas : expliquer ce que NVRHI fait
  pour nous, pas l'API qu'il y a en dessous, sauf s'il le demande.
- **Le mode autonome** (décidé par Donnovan le 2026-10-04) : un subagent relit chaque PR à sa place ; l'agent fusionne
  après corrections et vérification (la CI verte, pour la PR vers `main`), ADR compris. Donnovan pose ses choix par
  sondage, et son choix vaut validation de l'ADR (règle n°3) ; il relit après coup.

## Règles non négociables

1. **La CI ne tourne qu'une fois par fonctionnalité, avant la fusion dans `main`, sur la PR vers `main`.** Décidé par
   Donnovan le 2026-10-09, après trois jours à attendre une CI qui tournait pour chaque PR d'une pile, dit telle qu'il
   l'a écrit, fautes comprises : « On a perdu énormément de temps avec cette CI, je pense que le mieux c'est qu'on ne
   fasse les test CI seulement quand on merge dans master pas à chaque PR, et du coup tant qu'on à pas fini la feature
   on ne fait pas de merge dans master tu en pense quoi ? Parce que la ça fait 3 jours qu'on attend parfois 3h juste
   pour savoir si un commit passe. Je pers mon temps et toi aussi. » Au sondage « Quand est-ce que la CI tourne ? », sa
   réponse : « Avant la fusion (Recommandé) ».
   - **Une branche par fonctionnalité** (un milestone, ou un ensemble qui n'a de sens que fini), tirée de `main`, qui
     ne reçoit rien d'autre. Ses morceaux y entrent par des PR qui l'ont pour base, **une seule ouverte à la fois** :
     la suivante ne s'ouvre qu'une fois la précédente relue et fusionnée.
   - **Un morceau** est relu par un subagent (règle n°2), vérifié par `tools/verify.sh` (`BASE=<la branche de la
     fonctionnalité>`) et, s'il touche Windows, par les tests Windows du portable (build/SKILL.md), puis fusionné dans
     la branche de la fonctionnalité en merge commit. Le workflow ne se déclenche pas pour une PR dont la base n'est
     pas `main` : aucune CI GitHub à attendre.
   - **Une seule PR vers `main`, à la fin**, la fonctionnalité entière : sa CI est la porte, ses checks requis la
     condition de la fusion, qui se fait en **merge commit**. Tant que la fonctionnalité n'est pas finie, rien ne
     fusionne dans `main` ; si `main` a bougé entre-temps (un correctif urgent), l'intégrer à la branche avant cette PR.
   - **La CI à la main** avant une étape risquée (toolchain, dépendances, CI elle-même, nouveau backend), sans attendre
     la fin : `gh workflow run ci.yml --ref <branche>` (le déclencheur `workflow_dispatch` n'existe que si `ci.yml`
     est sur `main`). Les caches lourds ne sont enregistrés que par `main` (#382) : une fonctionnalité qui change les
     ports recompile vcpkg à chaque passage jusqu'à sa fusion.
   La protection de `main` garde ses checks requis et `strict: false` (pas d'obligation d'être à jour).
2. **Une PR se relit en 30 minutes au plus** (environ 400 lignes hors tiers et généré). Sinon, découper.
   Exception admise par Donnovan (2026-09-21) : du code Vulkan qui forme un bloc peut dépasser, **s'il reste
   lisible et que l'écart est signalé** dans la PR avec sa raison.
   Exception étendue par Donnovan (2026-10-09) au code du device **Direct3D 12** (« Si on veut que DX12 soit bien
   implémenter je suppose que c'est nécessaire. »), aux mêmes conditions : un bloc lisible, l'écart signalé dans la PR
   avec sa raison.
   Exception décidée par Donnovan (2026-10-06) : une PR qui **déplace du code sans le changer** peut dépasser,
   si l'écart est signalé et que le guide de lecture dit de la lire avec `git diff --color-moved` : seules les
   lignes changées en route ressortent (ADR-0029).
   La règle vaut pour **les morceaux**, les PR vers la branche d'une fonctionnalité, et non pour la PR finale vers
   `main`, qui ne contient que des morceaux déjà relus.
3. **Décision structurante = ADR d'abord** (`docs/adr/`, modèle `0000-modele.md`), validé par Donnovan avant
   l'implémentation.
4. **Zéro erreur de validation en Debug** (couche de validation NVRHI, validation layers Vulkan, couche de debug
   D3D12). Ne jamais désactiver une validation, un test, un warning ou un sanitizer pour faire passer quelque
   chose. Signaler le problème.
5. **Dépendances via vcpkg** (ou `FetchContent` avec commit figé s'il n'y a pas de port), licence permissive,
   visibilité limitée aux modules prévus (voir SPECS §7). Du code adapté de Donut garde son en-tête de licence MIT.
6. **Mesures reproductibles** : chaque chiffre du journal vient d'une commande ou d'un script versionné, sur la
   machine de référence (SPECS §10).
7. **Un contrôle échoue bruyamment** quand sa condition n'est pas réunie, il ne se contente jamais de ne pas
   s'exécuter (leçon de la phase 0 : trois garde-fous restés verts sans rien vérifier).

## Écrire le code

- **Forme du code (ADR-0011)**, la règle qui compte le plus pour Donnovan :
  - la logique s'écrit en **fonctions libres dont toutes les dépendances sont des paramètres** ;
  - la **glu ECS tient en une ligne**, jamais dans le fichier de logique ;
  - **chaque piège porte son nom** (`normalizeOrZero`, `clampPitch`) plutôt que d'être un calcul brut.
- Commentaires en français, qui expliquent le **pourquoi**, pas le quoi. Nommage et mise en forme : ADR-0009.
- Un `README.md` par module : rôle, invariants, points d'entrée, équivalents dans Unreal, Unity et Godot.
- Quand on utilise NVRHI ou flecs d'une manière non évidente, un commentaire renvoie à la section de leur
  documentation qui l'explique.

## Skills

Avant une tâche couverte par un skill, lire son `SKILL.md`, **puis son `GOTCHA.md`** : les pièges déjà
rencontrés, avec leur parade. Tout nouveau piège s'ajoute au `GOTCHA.md` du skill concerné, au moment où on le
rencontre (symptôme, cause, parade, date).

| Skill | Quand |
|---|---|
| [`session`](.agents/skills/session/SKILL.md) | Début et fin de session, branche, PR, journal, board, temps de Donnovan |
| [`build`](.agents/skills/build/SKILL.md) | Compiler, tester, sanitizers, profilage, CI, tests manuels de la fenêtre |
| [`pr-autonome`](.agents/skills/pr-autonome/SKILL.md) | Chaque PR en mode autonome : worktree, workflow, vérification, contre-tests, relecture, fusion dans la branche de la fonctionnalité, PR finale vers `main` |
| [`cloture`](.agents/skills/cloture/SKILL.md) | Clôture d'un milestone ou d'une phase : tag, release, ratio, recalibrage |
| [`questions`](.agents/skills/questions/SKILL.md) | Répondre à une question de Donnovan, comparer avec les autres moteurs |

`.claude/skills` est un lien vers `.agents/skills`, pour que Claude Code découvre les skills tout seul. Un autre
outil les lit par le chemin ci-dessus.
