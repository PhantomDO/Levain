#!/usr/bin/env bash
# Télécharge les assets de test tiers listés dans tools/assets.lock vers assets-cache/, ignoré par
# git (ADR-0018 : les assets tiers ne sont jamais versionnés). Chaque fichier est vérifié par son
# SHA-256 ; un fichier déjà présent et intact n'est pas retéléchargé. Une ligne est « hash chemin »
# pour un fichier de glTF-Sample-Assets, au commit du lock, ou « hash chemin adresse » pour un
# fichier pris ailleurs (une HDRI de Poly Haven). « hash chemin adresse membre » : l'adresse est une
# archive zip, dont on extrait le membre (Inter ne publie ses TTF que dans le zip de sa version) ; le
# hash est celui du fichier extrait, pas de l'archive.
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
# Chaque préfixe doit désigner au moins un fichier du lock, vérifié avant tout téléchargement : un
# préfixe vide prendrait tout, un préfixe mal écrit ne prendrait rien, sans rien dire.
for prefix in ${prefixes[@]+"${prefixes[@]}"}; do
    [[ -n "$prefix" ]] || { echo "préfixe vide" >&2; exit 1; }
    awk -v p="$prefix" '$1 ~ /^[0-9a-f]{64}$/ && index($2, p) == 1 { found = 1 } END { exit !found }' \
        "$lock" || { echo "aucun fichier pour « $prefix » dans $lock" >&2; exit 1; }
done
commit=$(awk '$1 == "commit" { print $2 }' "$lock")
[[ -n "$commit" ]] || { echo "aucun commit dans $lock" >&2; exit 1; }

# Écrit sur la sortie standard le membre d'un zip. `unzip` d'abord (Linux, WSL, et Git pour Windows
# qui livre /usr/bin/unzip.exe) ; `7z` et Python ne sont que des replis. Aucun des trois : code 2 et
# une erreur qui le dit, pas un « membre introuvable ». Un membre absent est le code 1.
extract_member() {
    if command -v unzip > /dev/null; then
        unzip -p "$1" "$2"
    elif command -v 7z > /dev/null; then
        # 7z rend 0 et une sortie vide pour un membre absent : on le transforme en échec. Un
        # fichier, pas une substitution de commande : bash y perdrait les octets nuls de la police.
        local out
        out=$(mktemp) || return 1
        if 7z e -so "$1" "$2" > "$out" 2> /dev/null && [[ -s "$out" ]]; then
            cat "$out"
            rm -f "$out"
        else
            rm -f "$out"
            return 1
        fi
    elif command -v python3 > /dev/null; then
        python3 -c 'import sys, zipfile; sys.stdout.buffer.write(zipfile.ZipFile(sys.argv[1]).read(sys.argv[2]))' "$1" "$2"
    else
        echo "ni unzip, ni 7z, ni python3 : impossible d'ouvrir le zip $1" >&2
        return 2
    fi
}

count=0
while read -r hash path url member; do
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
    if [[ -n "${member:-}" ]]; then
        extract_member "$file.part" "$member" > "$file.member" \
            || { rc=$?; rm -f "$file.part" "$file.member"
                 [[ $rc -eq 2 ]] || echo "membre $member introuvable dans l'archive de $path" >&2
                 exit 1; }
        mv "$file.member" "$file.part"
    fi
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
