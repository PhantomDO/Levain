#!/usr/bin/env bash
# Les programmes de la cible que lance la CI, et ce qu'elle en vérifie : le sandbox sur ses vues, son exécutable
# éditeur, Sponza cuite. Un seul script pour les jobs Linux et Windows (ADR-0035, décision 4) : les mêmes arguments
# et les mêmes contrôles des deux côtés, sans copie à tenir à jour. Sous Windows, il tourne dans le bash de Git, à la
# racine du miroir de l'arbre Linux (`C:\home\runner\work\…`), où se lisent les chemins compilés dans le sandbox.
#
#   tools/ci-programs.sh <lancement> <dossier de build>
#   SDL_VIDEO_DRIVER=offscreen tools/ci-programs.sh sandbox build/linux-debug
#
# Lancements : sandbox, fox, terrain, physics, sponza, character, hike, ui, editor, cooked. Depuis la racine du
# dépôt, après tools/fetch-assets.sh ; les journaux et les captures s'écrivent dans le dossier courant. Chaque
# contrôle échoue avec son message `::error::` (règle n°7), qu'Actions affiche en annotation.
#
# --seconds N : la boucle s'arrête d'elle-même N s après son premier tour, puis le programme sort normalement, et
# c'est à cette sortie que LeakSanitizer fait son compte. La durée est comptée depuis la boucle, pas depuis le
# lancement : le démarrage varie de 1 à plus de 10 s selon la charge du runner, et un délai extérieur de 8 s est
# déjà tombé avant la première frame (PR #59).
#
# timeout n'est plus qu'un filet contre un blocage :
#   --preserve-status : le code de sortie est celui du sandbox, pas celui de timeout.
#   -k 10 : SIGKILL 10 s plus tard si le programme ignore SIGTERM ; code 137, job rouge.
#   --foreground : un seul SIGTERM. Sans cette option, timeout en envoie un second au
#     groupe de processus, et ce second signal fait sauter une assertion du SDL de Debug
#     (SDL_quit.c:171), qui attend alors une réponse jusqu'au SIGKILL.
set -euo pipefail

[ $# -eq 2 ] || { echo "usage : $0 <lancement> <dossier de build>" >&2; exit 2; }
run=$1
build=$2
[ -d "$build" ] || { echo "::error::$build n'existe pas"; exit 1; }

# Le chemin d'un exécutable du build, avec son `.exe` s'il en a un : le bash de Git l'ajoute seul, celui d'une distro
# WSL qui lance l'exe Windows (une répétition locale), non.
executableIn() { # $1 = dossier de build, $2 = chemin dans le build, sans extension
    if [ -f "$1/$2.exe" ]; then echo "$1/$2.exe"; else echo "$1/$2"; fi
}

# La sortie d'un programme, affichée et gardée dans un fichier. Sous Windows, spdlog finit ses lignes par « \r\n » :
# le « \r » retiré, un motif ancré en fin de ligne (`$`) se lit comme sous Linux.
logTo() { tr -d '\r' | tee "$1"; }

sandbox=$(executableIn "$build" sandbox/levain_sandbox)
editor=$(executableIn "$build" sandbox/levain_sandbox_editor)

# Le camion. Le contrôle final exige une boucle d'au moins une seconde : une boucle qui s'arrête tôt sans
# erreur est une panne silencieuse (règle n°7).
runSandbox() {
    timeout --foreground --preserve-status -k 10 60 \
        "$sandbox" --seconds 3 --capture capture.png \
        --model assets-cache/Models/CesiumMilkTruck/glTF/CesiumMilkTruck.gltf \
        | logTo sandbox.log
    grep -Eq "boucle arrêtée après ([1-9]|[0-9]{2,})\.[0-9] s" sandbox.log \
        || { echo "::error::la boucle du sandbox a tourné moins d'une seconde"; exit 1; }
    # La capture relit l'image de la swapchain : elle doit exister et ne pas être vide.
    test -s capture.png || { echo "::error::capture.png absente ou vide"; exit 1; }
}

# Fox : un modèle skinné qui passe du repos à la course et revient (M4.5, ADR-0022). En Debug,
# validation active : c'est la première passe compute du moteur. La ligne « skinning : »
# prouve que le compute a tourné ; « os le plus rapide » mesure le critère de M4.5, sans saut
# visible. En local (Release), la course seule atteint 504 unités/s, la locomotion aussi, et
# une bascule sans fondu 178 110 : au-delà de 1 000, une transition a sauté.
runFox() {
    timeout --foreground --preserve-status -k 10 60 \
        "$sandbox" --seconds 8 \
        --model assets-cache/Models/Fox/glTF/Fox.gltf --model-scale 0.05 \
        --locomotion Survey,Walk,Run | logTo fox.log
    fastest=$(sed -nE 's/.*os le plus rapide : ([0-9]+) unités.*/\1/p' fox.log)
    [ -n "$fastest" ] || { echo "::error::le skinning du renard n'a pas tourné"; exit 1; }
    [ "$fastest" -le 1000 ] \
        || { echo "::error::un os a bougé à $fastest unités/s : saut de pose"; exit 1; }
    # Le temps GPU de la passe d'ombres se mesure (M5.3). Ici sur le renard : sous lavapipe, en
    # Debug, Sponza ne fait que quelques images en 2 s, moins que les trois qu'un timer GPU met à
    # rendre sa mesure. Le chiffre de Sponza vient de la machine de référence (journal).
    grep -Eq "ombres : [0-9.]+ ms GPU en moyenne sur [1-9][0-9]* mesures" fox.log \
        || { echo "::error::le temps GPU de la passe d'ombres n'a pas été mesuré"; exit 1; }
    # Chaque passe chronométrée, et des dessins comptés (#133) : une passe à 0,000 ms n'a pas été
    # mesurée, et une passe absente n'a pas tourné (le renderer ne cite que les étapes mesurées).
    passes=$(grep -E "passes, GPU en moyenne : clusters [0-9.]+ ms, ombres [0-9.]+ ms, opaques [0-9.]+ ms, ciel [0-9.]+ ms, tonemapping" fox.log) \
        || { echo "::error::le temps GPU des passes n'a pas été donné en entier"; exit 1; }
    if grep -q " 0\.000 ms" <<<"$passes"; then
        echo "::error::une passe n'a pas été mesurée : $passes"; exit 1
    fi
    grep -Eq "dessins, par image : caméra [1-9][0-9.]* appels et [1-9][0-9]* triangles" fox.log \
        || { echo "::error::les appels de dessin n'ont pas été comptés"; exit 1; }
    # Le même renard sur le backend WebGPU (Dawn, hors écran, ADR-0023), validation active :
    # le skinning compute passe par le backend, et doit tourner.
    timeout --foreground --preserve-status -k 10 60 \
        "$sandbox" --gpu webgpu --seconds 8 \
        --model assets-cache/Models/Fox/glTF/Fox.gltf --model-scale 0.05 \
        --locomotion Survey,Walk,Run | logTo fox-webgpu.log
    grep -q "WebGPU en natif" fox-webgpu.log \
        || { echo "::error::le renard n'a pas tourné sur WebGPU"; exit 1; }
    grep -Eq "os le plus rapide : [1-9]" fox-webgpu.log \
        || { echo "::error::le skinning du renard n'a pas tourné sur WebGPU"; exit 1; }
}

# Le terrain (M5.6), premier plugin moteur, le lac et l'herbe (M5.7) : leurs passes s'inscrivent dans
# le renderer (ADR-0025) et doivent dessiner, sur Vulkan comme sur WebGPU, validation active.
#
# `--steps 8`, pas `--seconds 3` : exactement 8 images, quelle que soit la vitesse du runner, comme le personnage,
# la vallée et l'interface. En 3 s, lavapipe pour Windows en faisait 3 à 6 (7 sur le runner Linux), et le minuteur
# GPU, qui rend sa première mesure à la quatrième image, n'avait parfois rien mesuré : « l'étape transparente n'a
# rien dessiné » alors qu'elle dessinait. Huit images donnent cinq mesures (mesuré sous lavapipe), de quoi tenir
# avec des images lentes ; trois images ne donnent aucune mesure, et le contrôle rougit (contre-test de la PR).
runTerrain() {
    for gpu in vulkan webgpu; do
        timeout --foreground --preserve-status -k 10 120 \
            "$sandbox" --gpu $gpu --steps 8 \
            --view terrain | logTo terrain-$gpu.log
        # La sélection (M6.2) s'inscrit avec la physique, avant le terrain.
        grep -q "étapes du rendu : ombres : modèles, démo, terrain ; opaques : modèles, démo, sélection, terrain" terrain-$gpu.log \
            || { echo "::error::le terrain ne s'est pas inscrit dans le renderer ($gpu)"; exit 1; }
        grep -Eq "terrain, par image : [1-9][0-9.]* parcelles dessinées" terrain-$gpu.log \
            || { echo "::error::aucune parcelle du terrain dessinée ($gpu)"; exit 1; }
        # Le lac (M5.7), le premier dessin de l'étape Transparent : il doit y être inscrit, et
        # chronométré, donc dessiné. Le chronomètre n'existe que sous Vulkan : le backend WebGPU
        # ne mesure pas les temps GPU, et sa ligne de passes reste vide.
        grep -q "transparents : eau" terrain-$gpu.log \
            || { echo "::error::l'eau ne s'est pas inscrite dans le renderer ($gpu)"; exit 1; }
        if [ "$gpu" = vulkan ]; then
            grep -Eq "transparents [0-9.]+ ms" terrain-$gpu.log \
                || { echo "::error::l'étape transparente n'a rien dessiné ($gpu)"; exit 1; }
        fi
        # L'herbe (M5.7), dans l'étape Opaque après le terrain, et des brins demandés.
        grep -q "opaques : modèles, démo, sélection, terrain, herbe" terrain-$gpu.log \
            || { echo "::error::l'herbe ne s'est pas inscrite dans le renderer ($gpu)"; exit 1; }
        grep -Eq "herbe, par image : [0-9.]+ parcelles et [1-9][0-9]* brins" terrain-$gpu.log \
            || { echo "::error::aucun brin d'herbe dessiné ($gpu)"; exit 1; }
        # La physique de la vallée (M6.2) : le terrain, le lac et ses 64 caisses. Qu'elles
        # atteignent l'eau, 8 pas ne le permettent pas (ni 3 s sous lavapipe, ni 3 s sur un GPU :
        # « 0 caisses dans l'eau » à chaque lancement) : terrain_test.cpp le vérifie sans GPU, en
        # 600 pas. Ici, le lac doit exister comme volume de la physique, et la ligne le dit.
        grep -q "physique : 66 corps" terrain-$gpu.log \
            || { echo "::error::la vallée n'a pas ses 66 corps ($gpu)"; exit 1; }
        grep -Eq "lac : [0-9]+ caisses dans l'eau" terrain-$gpu.log \
            || { echo "::error::le lac n'est pas un volume de la physique ($gpu)"; exit 1; }
    done
}

# Le pixel de `--pick 960,540` est le centre d'une fenêtre de 1920 × 1080, celle que le sandbox demande. Windows
# réduit une fenêtre redimensionnable à la taille du bureau (celui d'un runner n'est pas documenté) : le pixel
# visé cesserait d'être le centre, et la sélection échouerait sous un message trompeur (« aucune caisse »), ou
# passerait sans plus vérifier ce que le commentaire de `runPhysics` dit (règle n°7). La taille se lit sur la
# capture, relue de la swapchain : le contrôle échoue en nommant la cause, avant ceux qui en dépendent.
requireFullHdCapture() { # $1 = journal, $2 = capture, $3 = backend
    grep -q "capture : $2 (1920 × 1080)" "$1" || {
        echo "::error::la capture $2 ne fait pas 1920 × 1080 (bureau plus petit que la fenêtre ?) : --pick 960,540" \
            "ne vise plus le centre ($3) ; journal : $(grep -o "capture : .*" "$1" || echo "aucune ligne de capture")"
        exit 1
    }
}

# Les 1 000 caisses de M6.1 (ADR-0026), et leur sélection par raycast (M6.2, ADR-0027) : Jolt
# dans l'application, sous la validation, les sanitizers et les deux backends. La plus haute
# part de 18,4 m (`HighestCrateStart`) : après 3 s, elle doit être descendue, ce que 8 pas
# suffisent à montrer. Pas « retombée » : sur lavapipe sous ASan, une image lente plafonne la
# simulation à 4 pas (ADR-0016), et les caisses tombent alors au ralenti. Le pixel visé, le
# centre de l'image, touche une caisse dans la grille de départ, pendant la chute et dans le
# tas (essayé de 0,1 à 8 s) : quelle que soit la vitesse du runner. La capture rend une image
# après la sélection : le contour passe sous la validation, les sanitizers et WebGPU.
runPhysics() {
    for gpu in vulkan webgpu; do
        timeout --foreground --preserve-status -k 10 120 \
            "$sandbox" --gpu $gpu --seconds 3 \
            --view physics --pick 960,540 --capture physics-$gpu.png | logTo physics-$gpu.log
        requireFullHdCapture physics-$gpu.log physics-$gpu.png $gpu
        line=$(grep -o "physique : [0-9]* corps ; la caisse la plus haute à y = [-0-9.]* m" physics-$gpu.log) \
            || { echo "::error::le sandbox n'a pas rendu compte de la physique ($gpu)"; exit 1; }
        echo "$line" | grep -q "physique : 1001 corps" \
            || { echo "::error::il manque des corps : $line ($gpu)"; exit 1; }
        highest=$(echo "$line" | grep -o "y = [-0-9.]*" | cut -d' ' -f3)
        awk -v y="$highest" 'BEGIN { exit !(y < 18.3) }' \
            || { echo "::error::les caisses n'ont pas bougé : $line ($gpu)"; exit 1; }
        # Le critère de M6.2 : un clic (--pick) sélectionne par raycast une caisse du tas.
        grep -Eq "sélection : ::crate_[0-9]+_[0-9]+_[0-9]+ à [0-9.]+ m" physics-$gpu.log \
            || { echo "::error::la sélection par raycast n'a touché aucune caisse ($gpu)"; exit 1; }
    done
}

# Sponza : 105 dessins par image. En Debug seulement, où la validation de NVRHI est active :
# c'est elle qui a montré que le buffer des constantes manquait de versions (M4.1).
runSponza() {
    timeout --foreground --preserve-status -k 10 120 \
        "$sandbox" --seconds 2 \
        --model assets-cache/Models/Sponza/glTF/Sponza.gltf | logTo sponza.log
    grep -Eq "boucle arrêtée après ([1-9]|[0-9]{2,})\.[0-9] s" sponza.log \
        || { echo "::error::la boucle avec Sponza a tourné moins d'une seconde"; exit 1; }
    # Sponza sur le backend WebGPU, validation active (#185).
    timeout --foreground --preserve-status -k 10 120 \
        "$sandbox" --gpu webgpu --seconds 2 \
        --model assets-cache/Models/Sponza/glTF/Sponza.gltf | logTo sponza-webgpu.log
    grep -q "WebGPU en natif" sponza-webgpu.log \
        || { echo "::error::Sponza n'a pas tourné sur WebGPU"; exit 1; }
    grep -Eq "boucle arrêtée après ([1-9]|[0-9]{2,})\.[0-9] s" sponza-webgpu.log \
        || { echo "::error::la boucle de Sponza sur WebGPU a tourné moins d'une seconde"; exit 1; }
}

# Le critère de M6.3 (ADR-0028) : le renard monte, dans Sponza, l'escalier posé contre le rebord
# de 0,9 m de la tranchée. `--steps 200` : exactement 200 pas de simulation, un par image, quelle
# que soit la vitesse du runner ; le résultat ne dépend donc pas de la machine (mesuré : le même
# au centimètre sur RADV et sous lavapipe). Parti du fond de la tranchée (y = −0,92), il doit
# finir au niveau des galeries (−0,02), au sol, sur le palier, toujours dans la tranchée sud
# (z entre −6,26 et −5,2).
runCharacter() {
    timeout --foreground --preserve-status -k 10 300 \
        "$sandbox" --view character --walk 1,0 \
        --steps 200 --capture character.png | logTo character.log
    line=$(grep -o "personnage : pieds à ([-0-9., ]*), [^,]*" character.log) \
        || { echo "::error::le sandbox n'a pas rendu compte du personnage"; exit 1; }
    echo "$line" | grep -q ", au sol" \
        || { echo "::error::le personnage n'est pas au sol : $line"; exit 1; }
    feet=$(echo "$line" | grep -o "([-0-9., ]*)" | tr -d '()' | tr ',' ' ')
    read -r x y z <<<"$feet"
    awk -v x="$x" -v y="$y" -v z="$z" \
        'BEGIN { exit !(y > -0.1 && y < 0.05 && x > 0.0 && z > -6.26 && z < -5.2) }' \
        || { echo "::error::le renard n'a pas monté l'escalier : $line"; exit 1; }
}

# Le renard dans la vallée (`--view hike`), la vue que montre la page web : Sponza ne peut pas y
# être publiée. Sur Vulkan et sur WebGPU, le backend du navigateur, avec le terrain, l'herbe et
# l'eau dessinés. Parti de (204, 280) sur le fond de la vallée, il doit avoir marché vers le lac,
# au sol, sans dévier : mesuré (206,91 ; −0,05 ; 280,02) sur les deux backends. 120 pas
# seulement : sous lavapipe, 200 pas prenaient 88 s par backend sur un CPU rapide. Le `timeout` de 450 s est un filet
# contre un blocage, pas un contrôle (voir l'en-tête) : lavapipe pour Windows sur les 4 processeurs du runner
# mettait environ 249 s pour WebGPU (le démarrage compris), 300 s ne laissaient que 51 s de marge, et un
# dépassement tue le processus sans autre message que le code 143.
runHike() {
    for gpu in vulkan webgpu; do
        timeout --foreground --preserve-status -k 10 450 \
            "$sandbox" --gpu $gpu --view hike --walk 1,0 \
            --steps 120 | logTo hike-$gpu.log
        grep -q "opaques : modèles, démo, sélection, terrain, herbe ; transparents : eau" hike-$gpu.log \
            || { echo "::error::la vallée ne s'est pas inscrite dans le renderer ($gpu)"; exit 1; }
        line=$(grep -o "personnage : pieds à ([-0-9., ]*), [^,]*" hike-$gpu.log) \
            || { echo "::error::le sandbox n'a pas rendu compte du renard ($gpu)"; exit 1; }
        echo "$line" | grep -q ", au sol" \
            || { echo "::error::le renard n'est pas au sol : $line ($gpu)"; exit 1; }
        feet=$(echo "$line" | grep -o "([-0-9., ]*)" | tr -d '()' | tr ',' ' ')
        read -r x y z <<<"$feet"
        awk -v x="$x" -v z="$z" 'BEGIN { exit !(x > 206.0 && x < 208.0 && z > 279.0 && z < 281.0) }' \
            || { echo "::error::le renard n'a pas marché vers le lac : $line ($gpu)"; exit 1; }
    done
}

# L'interface (M7.1, ADR-0032) : les panneaux de debug ouverts dès le départ (`--ui on`), sur
# les deux backends, validation active (Debug). L'UI doit avoir dessiné quelque chose ; sous
# Vulkan, son minuteur GPU doit avoir mesuré ; sous WebGPU, notre backend ne relit pas les
# minuteurs, et le bilan doit le dire (« non mesuré ») au lieu d'un 0 qui passerait pour une
# mesure. Le temps lui-même se juge sur la machine de référence, pas sous lavapipe. La capture
# rejoue l'image d'UI que `captureFrame` construit, sous validation elle aussi.
runUi() {
    for gpu in vulkan webgpu; do
        timeout --foreground --preserve-status -k 10 300 \
            "$sandbox" --gpu $gpu --view hike --ui on \
            --steps 60 --capture ui-$gpu.png | logTo ui-$gpu.log
        test -s ui-$gpu.png || { echo "::error::pas de capture avec l'UI ($gpu)"; exit 1; }
        line=$(grep -o "ui : CPU .* commandes dessinées" ui-$gpu.log) \
            || { echo "::error::le sandbox n'a pas rendu compte de l'UI ($gpu)"; exit 1; }
        draws=$(echo "$line" | grep -o "[0-9]* commandes dessinées" | grep -o "^[0-9]*")
        [ "$draws" -gt 0 ] || { echo "::error::l'UI n'a rien dessiné : $line ($gpu)"; exit 1; }
        if [ "$gpu" = vulkan ]; then
            { echo "$line" | grep -q "GPU [0-9.]* ms en moyenne" \
                && ! echo "$line" | grep -q "GPU 0\.000 ms"; } \
                || { echo "::error::le minuteur GPU de l'UI n'a rien mesuré : $line"; exit 1; }
        else
            echo "$line" | grep -q "GPU non mesuré" \
                || { echo "::error::sous WebGPU, le temps GPU de l'UI devrait être non mesuré : $line"; exit 1; }
        fi
    done
}

# L'éditeur (M7.2, ADR-0034) : l'exécutable éditeur du sandbox, panneaux ouverts, une entité
# choisie par son chemin, sous validation et sous les sanitizers. Son bilan compte les lignes
# de la hiérarchie et les champs que l'inspecteur a dessinés pour la sélection, à la dernière
# image (zéro pour un panneau jamais dessiné), et redit la sélection : une ligne absente, zéro
# entité, zéro champ (le panneau n'a rien dessiné pour la sélection) ou une autre sélection que
# celle de --select font échouer l'étape, qui sinon resterait verte sans rien vérifier (règle
# n°7). Le compte dit que le panneau a dessiné ; quels champs, c'est le test de l'éditeur.
runEditor() {
    timeout --foreground --preserve-status -k 10 120 \
        "$editor" --select grid::cube_1_2 \
        --seconds 3 | logTo editor.log
    line=$(grep -o "éditeur : [0-9]* entités, [0-9]* champs dessinés ; sélection : .*" editor.log) \
        || { echo "::error::l'éditeur n'a pas rendu compte de ses entités et de ses champs"; exit 1; }
    entities=$(echo "$line" | grep -o "[0-9]* entités" | grep -o "^[0-9]*")
    [ "$entities" -gt 0 ] || { echo "::error::l'éditeur ne compte aucune entité : $line"; exit 1; }
    fields=$(echo "$line" | grep -o "[0-9]* champs" | grep -o "^[0-9]*")
    [ "$fields" -gt 0 ] || { echo "::error::l'inspecteur n'a dessiné aucun champ : $line"; exit 1; }
    echo "$line" | grep -q "sélection : grid::cube_1_2$" \
        || { echo "::error::la sélection n'est pas celle de --select : $line"; exit 1; }
}

# Sponza cuite, après levain_cook : aucun asset chargé depuis sa source (ADR-0020).
runCooked() {
    timeout --foreground --preserve-status -k 10 120 \
        "$sandbox" --seconds 2 \
        --model assets-cache/Models/Sponza/glTF/Sponza.gltf | logTo sponza-cooked.log
    if grep -q "depuis la source" sponza-cooked.log; then
        echo "::error::un asset de Sponza chargé depuis sa source : la cuisson n'a pas servi"
        exit 1
    fi
    # La collision du décor (ADR-0028), relue de son .lvcol : la vue du personnage, deux pas.
    timeout --foreground --preserve-status -k 10 120 \
        "$sandbox" --view character --steps 2 \
        | logTo character-cooked.log
    grep -q "collision de sponza : [0-9]* triangles, chargés" character-cooked.log \
        || { echo "::error::la vue du personnage n'a pas chargé la collision de Sponza"; exit 1; }
    if grep -Eq "simplifiée au chargement|depuis la source" character-cooked.log; then
        echo "::error::la collision de Sponza simplifiée au chargement : le .lvcol n'a pas servi"
        exit 1
    fi
}

case "$run" in
    sandbox) runSandbox ;;
    fox) runFox ;;
    terrain) runTerrain ;;
    physics) runPhysics ;;
    sponza) runSponza ;;
    character) runCharacter ;;
    hike) runHike ;;
    ui) runUi ;;
    editor) runEditor ;;
    cooked) runCooked ;;
    *) echo "::error::lancement inconnu : $run" >&2; exit 2 ;;
esac
