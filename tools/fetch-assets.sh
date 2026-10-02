#!/usr/bin/env bash
# Télécharge les assets de test tiers listés dans tools/assets.lock vers assets-cache/, ignoré par
# git (ADR-0018 : les assets tiers ne sont jamais versionnés). Chaque fichier est vérifié par son
# SHA-256 ; un fichier déjà présent et intact n'est pas retéléchargé. Une ligne est « hash chemin »
# pour un fichier de glTF-Sample-Assets, au commit du lock, ou « hash chemin adresse » pour un
# fichier pris ailleurs (une HDRI de Poly Haven).
#
#   ./tools/fetch-assets.sh
#
# Un hash qui ne correspond pas arrête tout (règle n°7) : le fichier a changé à la source, ou le
# téléchargement est corrompu.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
lock="$root/tools/assets.lock"
dest="$root/assets-cache"
commit=$(awk '$1 == "commit" { print $2 }' "$lock")
[[ -n "$commit" ]] || { echo "aucun commit dans $lock" >&2; exit 1; }

count=0
while read -r hash path url; do
    [[ -z "$hash" || "$hash" == \#* || "$hash" == commit ]] && continue
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

[[ $count -gt 0 ]] || { echo "aucun fichier dans $lock" >&2; exit 1; }
echo "$count fichiers à jour dans $dest"
