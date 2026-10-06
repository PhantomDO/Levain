#!/usr/bin/env bash
# Télécharge les assets de test tiers listés dans tools/assets.lock vers assets-cache/, ignoré par
# git (ADR-0018 : les assets tiers ne sont jamais versionnés). Chaque fichier est vérifié par son
# SHA-256 ; un fichier déjà présent et intact n'est pas retéléchargé. Une ligne est « hash chemin »
# pour un fichier de glTF-Sample-Assets, au commit du lock, ou « hash chemin adresse » pour un
# fichier pris ailleurs (une HDRI de Poly Haven).
#
#   ./tools/fetch-assets.sh
#   ./tools/fetch-assets.sh <dossier> [préfixe…]
#
# Le second usage est celui d'un jeu (ADR-0029) : *Rando* télécharge par ce script, dans son propre
# dossier, les seuls assets dont il a besoin (`Models/Fox`, `Textures/`) ; jamais Sponza, dont la
# licence interdit la redistribution. Un préfixe qui ne désigne aucun fichier est une erreur.
#
# Un hash qui ne correspond pas arrête tout (règle n°7) : le fichier a changé à la source, ou le
# téléchargement est corrompu.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
lock="$root/tools/assets.lock"
dest="${1:-$root/assets-cache}"
prefixes=("${@:2}")
commit=$(awk '$1 == "commit" { print $2 }' "$lock")
[[ -n "$commit" ]] || { echo "aucun commit dans $lock" >&2; exit 1; }

count=0
while read -r hash path url; do
    [[ -z "$hash" || "$hash" == \#* || "$hash" == commit ]] && continue
    if [[ ${#prefixes[@]} -gt 0 ]]; then
        wanted=false
        for prefix in "${prefixes[@]}"; do
            [[ "$path" == "$prefix"* ]] && wanted=true
        done
        $wanted || continue
    fi
    file="$dest/$path"
    count=$((count + 1))
    if [[ -f "$file" ]] && echo "$hash  $file" | sha256sum --check --status; then
        continue
    fi
    mkdir -p "$(dirname "$file")"
    curl --fail --silent --show-error --location \
        "${url:-https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/$commit/$path}" \
        --output "$file.part"
    if ! echo "$hash  $file.part" | sha256sum --check --status; then
        rm -f "$file.part"
        echo "SHA-256 inattendu pour $path" >&2
        exit 1
    fi
    mv "$file.part" "$file"
    echo "téléchargé : $path"
done < "$lock"

[[ $count -gt 0 ]] || { echo "aucun fichier dans $lock pour : ${prefixes[*]:-tout}" >&2; exit 1; }
for prefix in "${prefixes[@]}"; do
    grep -qE "^[0-9a-f]{64} $prefix" "$lock" || { echo "aucun fichier pour « $prefix »" >&2; exit 1; }
done
echo "$count fichiers à jour dans $dest"
