#!/usr/bin/env bash
# Vérifie le hot-reload des shaders (M2.3, ADR-0014) sur le sandbox en marche :
#   1. une teinte rouge ajoutée à shaders/mesh.slang doit être recompilée et son pipeline recréé ;
#   2. une erreur de syntaxe doit aller dans le log sans arrêter le rendu ;
#   3. le shader d'origine, remis en place, doit être rechargé à son tour ;
#   4. un fichier inclus, shaders/lighting.slang, doit lui aussi recréer la passe (ADR-0025).
# Le délai de chaque rechargement est lu dans le log : âge du fichier à la détection, durée du build,
# durée de la recréation du pipeline.
#
# Usage, depuis la racine du dépôt, après un build linux-debug :
#   ./tools/shader-hot-reload.sh                            # fenêtre visible
#   SDL_VIDEO_DRIVER=offscreen ./tools/shader-hot-reload.sh # sans écran
#   SANDBOX=./build/linux-asan/sandbox/levain_sandbox ./tools/shader-hot-reload.sh
set -euo pipefail

shader=shaders/mesh.slang
included=shaders/lighting.slang
sandbox=${SANDBOX:-./build/linux-debug/sandbox/levain_sandbox}
log=$(mktemp)
backup=$(mktemp)

# Le shader d'origine revient quoi qu'il arrive, même si le script est interrompu.
includedBackup=$(mktemp)
cp "$shader" "$backup"
cp "$included" "$includedBackup"
trap 'cp "$backup" "$shader"; cp "$includedBackup" "$included"; rm -f "$backup" "$includedBackup"' EXIT

"$sandbox" --seconds 12 >"$log" 2>&1 &
pid=$!

sleep 3
echo "→ teinte rouge"
sed -i 's|return float4(shadeSurface(surface), 1.0);|return float4(float3(1.0, 0.3, 0.3) * shadeSurface(surface), 1.0);|' "$shader"
grep -q "float3(1.0, 0.3, 0.3)" "$shader" || { echo "ÉCHEC : la teinte n'a pas été appliquée"; exit 1; }

sleep 2
echo "→ erreur de syntaxe"
echo "ceci n'est pas du Slang" >>"$shader"

sleep 2
echo "→ shader d'origine"
cp "$backup" "$shader"

sleep 2
echo "→ fichier inclus"
echo "// touché par tools/shader-hot-reload.sh" >>"$included"

status=0
wait "$pid" || status=$?

echo "--- log du hot-reload"
# Les lignes du hot-reload, et le message de slangc qui suit « compilation échouée ».
sed -n '/\[shaders\]/p; /compilation échouée/,/\[shaders\] mesh.slang/{/^\[/!p}; /boucle arrêtée/p' "$log"
echo "---"

reloads=$(grep -c "pipeline des meshes recréé" "$log" || true)
failures=$(grep -c "compilation échouée" "$log" || true)
echo "sandbox : code $status ; pipelines recréés : $reloads (3 attendus) ; échecs signalés : $failures (1 attendu)"

# Contrôles bruyants (règle n°7) : chacun échoue avec son message.
[ "$status" -eq 0 ] || { echo "ÉCHEC : le sandbox s'est arrêté en erreur"; exit 1; }
[ "$reloads" -eq 3 ] || { echo "ÉCHEC : 3 rechargements attendus, dont celui du fichier inclus"; exit 1; }
[ "$failures" -eq 1 ] || { echo "ÉCHEC : l'erreur de compilation n'a pas été signalée"; exit 1; }
grep -Eq "boucle arrêtée après (11|12)\.[0-9] s" "$log" \
    || { echo "ÉCHEC : la boucle ne tourne pas jusqu'au bout"; exit 1; }
echo "OK"
rm -f "$log"
