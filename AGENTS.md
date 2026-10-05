# AGENTS.md — instructions pour les agents

Ce fichier est la **source unique** des instructions du projet, quel que soit l'outil (`CLAUDE.md` ne fait que
l'importer). Les procédures détaillées vivent dans des **skills**, un dossier chacun sous `.agents/skills/`.

## Le projet

**Levain** — moteur de jeu 3D en C++23 sur **NVRHI** (backend Vulkan) et **flecs** (ECS). Namespace racine
`levain`, cibles CMake préfixées `levain_`. **Linux d'abord** : Windows et Direct3D 12 sont différés jusqu'à ce
qu'une machine soit disponible (ADR-0011).
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
- **Le mode autonome** (décidé par Donnovan le 2026-10-04) : un subagent relit chaque PR à sa place ; l'agent
  fusionne après corrections et CI verte, ADR compris. Donnovan pose ses choix par sondage, et son choix vaut
  validation de l'ADR (règle n°3) ; il relit après coup.

## Règles non négociables

1. **Une seule PR ouverte à la fois.** Ne pas commencer une nouvelle tâche tant que la PR précédente n'est pas
   relue et fusionnée.
   Exception décidée par Donnovan (2026-10-05) : les PR d'un même milestone, déjà relues, s'ouvrent ensemble,
   **empilées** (chacune a pour base la branche de la précédente). Leurs CI tournent en parallèle ; elles
   fusionnent ensuite dans l'ordre, en **merge commit** : la PR suivante garde ainsi son SHA et sa CI, là où un
   squash l'obligerait à se rebaser et à repasser la CI. Chaque commit de `main` reste vérifié, sans attendre
   la CI d'une PR pour ouvrir la suivante. Trois pièges :
   - fusionner avec `tools/merge-stack.sh <N> <N+1> …` : chaque PR fusionne sans supprimer sa branche, la base
     de la suivante passe à `main`, puis la branche est supprimée. Supprimer la branche d'abord **ferme** la PR
     suivante au lieu de la rediriger (vu avec #271), et ne rien faire la laisserait fusionner dans la branche
     de la précédente, sans check requis ;
   - si `main` a bougé hors de la pile, intégrer `main` au bas de la pile et laisser la CI repasser ;
   - une correction au milieu de la pile se propage aux PR suivantes, dont la CI repasse avant la fusion.
   La protection de `main` doit garder `strict: false` (pas d'obligation d'être à jour), sans quoi chaque
   fusion invaliderait la CI de la suivante.
2. **Une PR se relit en 30 minutes au plus** (environ 400 lignes hors tiers et généré). Sinon, découper.
   Exception admise par Donnovan (2026-09-21) : du code Vulkan qui forme un bloc peut dépasser, **s'il reste
   lisible et que l'écart est signalé** dans la PR avec sa raison.
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
| [`cloture`](.agents/skills/cloture/SKILL.md) | Clôture d'un milestone ou d'une phase : tag, release, ratio, recalibrage |
| [`questions`](.agents/skills/questions/SKILL.md) | Répondre à une question de Donnovan, comparer avec les autres moteurs |

`.claude/skills` est un lien vers `.agents/skills`, pour que Claude Code découvre les skills tout seul. Un autre
outil les lit par le chemin ci-dessus.
