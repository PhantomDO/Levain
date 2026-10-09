#!/usr/bin/env bash
# Vérifie le hot-reload des textures (M4.4, ADR-0021) sur le sandbox en marche, avec le camion de
# tools/fetch-assets.sh :
#   1. une texture remplacée par des octets invalides doit aller dans le log, l'ancienne restant ;
#   2. le damier du sandbox, écrit à sa place, doit être rechargé en moins de 2 s après son
#      écriture (le critère de M4.4), et se voir sur la capture de fin.
# Le délai est lu dans le log : l'âge du fichier quand la nouvelle texture est prête.
#
# Usage, depuis la racine du dépôt, après un build linux-debug et ./tools/fetch-assets.sh :
#   ./tools/texture-hot-reload.sh                            # fenêtre visible
#   SDL_VIDEO_DRIVER=offscreen ./tools/texture-hot-reload.sh # sans écran
#   CAPTURE=capture.png SANDBOX=./build/linux-release/sandbox/levain_sandbox ./tools/texture-hot-reload.sh
set -euo pipefail

model=assets-cache/Models/CesiumMilkTruck/glTF/CesiumMilkTruck.gltf
texture=assets-cache/Models/CesiumMilkTruck/glTF/CesiumMilkTruck.jpg
sandbox=${SANDBOX:-./build/linux-debug/sandbox/levain_sandbox}
capture=${CAPTURE:-$(mktemp --suffix=.png)}
[ -f "$texture" ] || { echo "ÉCHEC : $texture absent, lancer ./tools/fetch-assets.sh"; exit 1; }
log=$(mktemp)
backup=$(mktemp -d)

# La texture et son .meta (dont le hash suit le contenu) reviennent quoi qu'il arrive : assets-cache
# est vérifié par SHA-256, et gardé en cache par la CI.
cp "$texture" "$texture.meta" "$backup/"
trap 'cp "$backup"/* "$(dirname "$texture")/"; rm -rf "$backup" "$texture.tmp"' EXIT

"$sandbox" --seconds 8 --model "$model" --capture "$capture" >"$log" 2>&1 &
pid=$!

# Le camion chargé et la boucle partie, et non un délai fixe depuis le lancement : 4 s ne suffisaient pas au
# sandbox Windows lancé de la distro WSL (le scan des assets y prend 3 s sur le partage), qui lisait alors les octets
# invalides à son premier chargement et s'arrêtait. Une minute au plus, puis un échec qui le dit (règle n°7).
for _ in $(seq 600); do
    grep -q "clic droit pour regarder" "$log" || ! kill -0 "$pid" 2>/dev/null || { sleep 0.1; continue; }
    break
done
grep -q "clic droit pour regarder" "$log" \
    || { cat "$log"; echo "ÉCHEC : la boucle du sandbox n'a pas démarré"; kill "$pid" 2>/dev/null || true; exit 1; }

sleep 1
echo "→ octets invalides"
# Écrits à côté puis renommés, comme le damier plus bas : `>"$texture"` tronque puis écrit, deux changements que le
# sandbox voit l'un après l'autre sur un runner chargé (un fichier vide, puis le contenu). Il signale alors deux
# échecs au lieu d'un (CI, #378).
printf 'ceci n est pas un JPEG' >"$texture.tmp"
mv "$texture.tmp" "$texture"

sleep 1.5
echo "→ damier"
# Un PNG sous le nom d'un .jpg : le décodeur reconnaît le format au contenu. Copié à côté puis
# renommé, pour que le sandbox ne voie jamais un fichier à moitié écrit.
cp data/textures/checker.png "$texture.tmp"
mv "$texture.tmp" "$texture"

status=0
wait "$pid" || status=$?

echo "--- log du hot-reload"
grep -E "\[assets\].*(CesiumMilkTruck|ancienne texture)|boucle arrêtée" "$log" || true
echo "---"

reloads=$(grep -c "CesiumMilkTruck.jpg rechargée" "$log" || true)
failures=$(grep -c "l'ancienne texture reste" "$log" || true)
delay=$(sed -nE 's/.*CesiumMilkTruck.jpg rechargée en [0-9]+ ms, ([0-9]+) ms après son écriture.*/\1/p' "$log" | head -1)
echo "sandbox : code $status ; rechargements : $reloads (1 attendu) ; échecs signalés : $failures (1 attendu) ; délai : ${delay:-?} ms (< 2000)"
echo "capture : $capture"

# Contrôles bruyants (règle n°7) : chacun échoue avec son message.
[ "$status" -eq 0 ] || { echo "ÉCHEC : le sandbox s'est arrêté en erreur"; exit 1; }
[ "$reloads" -eq 1 ] || { echo "ÉCHEC : $reloads rechargements, 1 attendu"; exit 1; }
[ "$failures" -eq 1 ] || { echo "ÉCHEC : $failures échecs signalés pour la texture invalide, 1 attendu (0 : non signalée ; 2 : écriture vue en deux temps)"; exit 1; }
[ -n "$delay" ] && [ "$delay" -lt 2000 ] || { echo "ÉCHEC : rechargée trop tard"; exit 1; }
grep -Eq "boucle arrêtée après [78]\.[0-9] s" "$log" \
    || { echo "ÉCHEC : la boucle ne tourne pas jusqu'au bout"; exit 1; }
echo "OK"
rm -f "$log"
