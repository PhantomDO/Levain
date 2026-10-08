#!/usr/bin/env bash
# Outille une distro WSL Ubuntu 26.04 pour Levain, comme la distrobox dev-ubuntu et la CI
# (.github/workflows/ci.yml).
# Lancé par levain-wsl.ps1, en root, dans la distro neuve :
#
#   provision-levain.sh <utilisateur> [--skip-first-build]
#
# Les versions suivent la CI : les changer ici quand elles y changent. Chaque étape échoue bruyamment (règle n°7) :
# un téléchargement raté arrête le script, il ne laisse pas une distro à moitié outillée qui paraîtrait prête.
set -euo pipefail

user=${1:?usage : provision-levain.sh <utilisateur> [--skip-first-build]}
firstBuild=yes
[ "${2:-}" = "--skip-first-build" ] && firstBuild=no

VCPKG_TAG=2026.07.29 # = VCPKG_TAG de ci.yml, et le builtin-baseline de vcpkg.json
LLVM_VERSION=23      # = LLVM_VERSION de ci.yml : clang-format change de sortie d'une version majeure à l'autre
EMSDK_VERSION=6.0.10 # = EMSDK_VERSION de ci.yml

echo "== l'utilisateur $user"
if ! id "$user" > /dev/null 2>&1; then
    useradd --create-home --shell /bin/bash --groups sudo "$user"
fi
# Une distro jetable, à un seul utilisateur : sudo sans mot de passe, comme dans une distrobox, pour que l'agent
# installe un paquet sans rester bloqué sur une invite.
echo "$user ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/90-levain
chmod 0440 /etc/sudoers.d/90-levain
# Complété, jamais réécrit : l'image d'Ubuntu y pose déjà ses réglages (systemd), qu'un > effacerait.
if ! grep -q '^\[user\]' /etc/wsl.conf 2> /dev/null; then
    printf '\n[user]\ndefault=%s\n' "$user" >> /etc/wsl.conf
fi

echo "== les paquets (la liste de l'étape « Outils » de la CI, plus de quoi cloner et compiler)"
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y \
    build-essential git curl wget ca-certificates gnupg lsb-release software-properties-common \
    python3 cmake ninja-build zip unzip xz-utils pkg-config gh \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxtst-dev \
    libxkbcommon-dev libwayland-dev libdecor-0-dev libegl1-mesa-dev \
    libgl1-mesa-dev libgles2-mesa-dev libdrm-dev libgbm-dev libudev-dev \
    libvulkan1 mesa-vulkan-drivers vulkan-validationlayers

echo "== LLVM $LLVM_VERSION (apt.llvm.org, comme la CI)"
# Téléchargé puis exécuté, et non passé en pipe à bash : un téléchargement raté donnerait une entrée vide, que
# bash exécuterait sans erreur (la leçon de ci.yml).
wget -qO /tmp/llvm.sh https://apt.llvm.org/llvm.sh
bash /tmp/llvm.sh "$LLVM_VERSION"
apt-get install -y "clang-format-$LLVM_VERSION" "clang-tidy-$LLVM_VERSION"
# Des liens dans /usr/local/bin, qui précède /usr/bin : /usr/bin/clang++ est un vrai fichier du paquet clang,
# qu'update-alternatives ne remplacerait pas.
for tool in clang clang++ clang-format clang-tidy; do
    ln -sf "/usr/bin/$tool-$LLVM_VERSION" "/usr/local/bin/$tool"
done
for tool in clang++ clang-format clang-tidy; do
    "$tool" --version | grep -q "version $LLVM_VERSION\." \
        || { echo "erreur : $tool n'est pas en version $LLVM_VERSION" >&2; exit 1; }
done

echo "== vcpkg, emsdk, Claude Code, les dépôts et les assets, pour $user"
# Les étapes de l'utilisateur dans un fichier, lancé depuis ce fichier : passées à bash par l'entrée standard,
# un programme qui la lit (l'installeur de Claude Code, cmake, ctest) en avalerait la suite, et bash finirait
# en 0 sans les avoir faites.
cat > /tmp/levain-user-steps.sh << 'USER_STEPS'
cd ~

if [ ! -d vcpkg ]; then
    git clone --branch "$VCPKG_TAG" https://github.com/microsoft/vcpkg.git vcpkg
fi
./vcpkg/bootstrap-vcpkg.sh -disableMetrics

if [ ! -d emsdk ]; then
    git clone https://github.com/emscripten-core/emsdk.git emsdk
fi
./emsdk/emsdk install "$EMSDK_VERSION"
./emsdk/emsdk activate "$EMSDK_VERSION"

# L'installeur natif de Claude Code, téléchargé puis exécuté (même raison que llvm.sh).
curl -fsSL -o /tmp/claude-install.sh https://claude.ai/install.sh
bash /tmp/claude-install.sh

mkdir -p Projects
[ -d Projects/Levain ] || git clone https://github.com/PhantomDO/Levain.git Projects/Levain
[ -d Projects/Rando ] || git clone https://github.com/PhantomDO/Rando.git Projects/Rando
# Les assets de test tiers, jamais versionnés (ADR-0018) : sans eux, le build web refuse de se configurer.
(cd Projects/Levain && ./tools/fetch-assets.sh)

if ! grep -q "levain-wsl" ~/.bashrc; then
    cat >> ~/.bashrc << 'RC'

# levain-wsl : l'environnement de Levain (provision-levain.sh)
export VCPKG_ROOT="$HOME/vcpkg"
export PATH="$HOME/.local/bin:$PATH"
# Pas de GPU Linux sous WSL : les tests Vulkan tournent sur lavapipe, le Vulkan logiciel de Mesa, seul et
# préchargé comme dans la CI (build/GOTCHA.md, « LeakSanitizer et lavapipe »). tools/verify.sh lit ces deux
# variables à la place de RADV, le pilote de la machine de référence.
export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json
export LEVAIN_VK_PRELOAD=/usr/lib/x86_64-linux-gnu/libvulkan_lvp.so
RC
fi

if [ "$FIRST_BUILD" = yes ]; then
    echo "== le premier build (vcpkg compile les dépendances : long la première fois)"
    export VCPKG_ROOT="$HOME/vcpkg"
    export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json
    cd ~/Projects/Levain
    cmake --preset linux-debug
    cmake --build --preset linux-debug
    SDL_VIDEO_DRIVER=offscreen ctest --test-dir build/linux-debug --output-on-failure --timeout 120
fi
USER_STEPS
chmod 0644 /tmp/levain-user-steps.sh
sudo -u "$user" -H env VCPKG_TAG="$VCPKG_TAG" EMSDK_VERSION="$EMSDK_VERSION" FIRST_BUILD="$firstBuild" \
    bash -euo pipefail /tmp/levain-user-steps.sh

echo "== prête"
