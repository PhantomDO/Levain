#!/usr/bin/env bash
# Fusionne une pile de PR, dans l'ordre, en merge commit (AGENTS.md, règle n°1 et son exception).
#   tools/merge-stack.sh 271 272 273 …      (la première a main pour base, chacune la précédente)
#
# Avant toute fusion, la chaîne est vérifiée : la première a main pour base, chacune la branche de la
# précédente. Puis, pour chaque PR :
#   1. tous ses checks verts (on attend ceux en cours) ;
#   2. elle fusionne au SHA dont on a lu les checks, **sans** supprimer sa branche : GitHub fermerait
#      les PR qui l'ont pour base au lieu de les rediriger vers main (vu le 05/10/2026 avec #271) ;
#   3. toute PR ouverte qui avait sa branche pour base passe à main (`edited`, qui ne relance pas la
#      CI : son SHA et ses checks restent), qu'elle soit dans la liste ou non ;
#   4. la branche fusionnée est supprimée, puis on vérifie que ces PR sont toujours ouvertes.
# Le script s'arrête au premier écart, sans rien fusionner de plus (règle n°7).
set -uo pipefail

cd "$(dirname "$0")/.." || exit 1
prs=("$@")
[ "${#prs[@]}" -gt 0 ] || { echo "usage : tools/merge-stack.sh <PR> [<PR> …]" >&2; exit 2; }
fail() { echo "$*, arrêt" >&2; exit 1; }

# La chaîne entière, avant de rien fusionner : une PR oubliée ou un numéro mal tapé ferait entrer
# dans main les commits d'une base non fusionnée.
expected=main
for n in "${prs[@]}"; do
    base=$(gh pr view "$n" --json baseRefName -q .baseRefName) || fail "#$n introuvable"
    [ "$base" = "$expected" ] || fail "#$n : base $base, attendue $expected"
    expected=$(gh pr view "$n" --json headRefName -q .headRefName)
done

for n in "${prs[@]}"; do
    [ "$(gh pr view "$n" --json baseRefName -q .baseRefName)" = main ] || fail "#$n : base pas encore main"
    # Le SHA avant les checks : un push entre les deux fera refuser la fusion (--match-head-commit),
    # au lieu de fusionner un commit dont on n'a pas lu les checks.
    sha=$(gh pr view "$n" --json headRefOid -q .headRefOid) || fail "#$n : SHA illisible"
    # `gh pr checks` sort en erreur tant qu'un check est en cours ou rouge : on lit sa sortie, pas
    # son code.
    # Une coupure réseau se lit comme un check rouge : on l'attend 10 min au plus, au lieu de s'arrêter au
    # milieu d'une pile (vu le 08/10/2026 : « error connecting to api.github.com »).
    checks=$(gh pr checks "$n" 2>&1 || true)
    offline=0
    while grep -qE $'\t(pending|queued|in_progress)\t|error connecting|Could not resolve' <<< "$checks"; do
        if grep -qE 'error connecting|Could not resolve' <<< "$checks"; then
            offline=$((offline + 1))
            [ "$offline" -le 20 ] || fail "#$n : GitHub injoignable depuis 10 min"
            echo "#$n : GitHub injoignable, nouvel essai…"
        else
            offline=0
            echo "#$n : checks en cours…"
        fi
        sleep 30
        checks=$(gh pr checks "$n" 2>&1 || true)
    done
    bad=$(awk -F'\t' '{print $1"="$2}' <<< "$checks" | grep -vE "=pass|=skipping" || true)
    [ -z "$bad" ] || fail "#$n : checks pas verts (${bad//$'\n'/ })"
    head=$(gh pr view "$n" --json headRefName -q .headRefName)
    gh pr merge "$n" --merge --match-head-commit "$sha" > /dev/null || fail "#$n : fusion refusée"
    echo "#$n fusionnée ($head)"
    stacked=$(gh pr list --base "$head" --state open --json number -q '.[].number') \
        || fail "#$n : PR empilées illisibles ; $head gardée"
    # Par l'API REST : `gh pr edit --base` échoue depuis le 08/10/2026 sur une erreur GraphQL, la fin des
    # « Projects (classic) » qu'il interroge au passage.
    for m in $stacked; do
        gh api -X PATCH "repos/{owner}/{repo}/pulls/$m" -f base=main > /dev/null \
            || fail "#$m : base non changée ; $head gardée"
    done
    git push -q origin --delete "$head" || echo "#$n : la branche $head n'a pas pu être supprimée" >&2
    sleep 5 # GitHub a fermé #271 une seconde après la suppression de sa base
    for m in $stacked; do
        [ "$(gh pr view "$m" --json state -q .state)" = OPEN ] || fail "#$m n'est plus ouverte"
    done
done
