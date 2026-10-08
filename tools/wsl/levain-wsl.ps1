<#
.SYNOPSIS
    Une distro WSL dédiée à Levain : Ubuntu 26.04, comme la CI et la distrobox dev-ubuntu, rangée dans son
    propre dossier et supprimable d'une commande, sans rien laisser derrière elle.

.DESCRIPTION
    Crée la distro « levain-dev » (WSL 2.4.4 au moins, pour --name et --location), puis l'outille par
    provision-levain.sh, posé à côté de ce script : LLVM 23, vcpkg et emsdk aux versions de la CI, Claude Code,
    les dépôts Levain et Rando, et les assets de test. Rien n'est installé sous Windows : tout vit dans le disque
    virtuel de la distro, dans -Location. Mode d'emploi : README.md, dans ce dossier.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\levain-wsl.ps1
    powershell -ExecutionPolicy Bypass -File .\levain-wsl.ps1 -SkipFirstBuild
    powershell -ExecutionPolicy Bypass -File .\levain-wsl.ps1 -Remove
#>
param(
    [string]$Name = "levain-dev",
    [string]$Distro = "Ubuntu-26.04",
    # Un dossier par distro : un second -Name ne partage pas celui de la première.
    [string]$Location = (Join-Path $env:LOCALAPPDATA $Name),
    [string]$User = "",
    [switch]$SkipFirstBuild,
    [switch]$Remove
)

# Pas de $ErrorActionPreference = "Stop" : sous PowerShell 5.1, il ferait d'une ligne d'erreur d'un programme
# natif (wsl.exe) une exception. Chaque appel à wsl vérifie plutôt $LASTEXITCODE, et échoue bruyamment.
# wsl.exe écrit en UTF-16 par défaut : PowerShell 5.1 y lirait des caractères nuls entre les lettres.
$env:WSL_UTF8 = "1"

function Stop-WithError([string]$Message) {
    Write-Host "levain-wsl : $Message" -ForegroundColor Red
    exit 1
}

# La version de WSL, sur la première ligne de « wsl --version » (« WSL version: 2.4.13.0 », ou traduite).
$versionLine = (wsl --version | Select-Object -First 1)
if ($LASTEXITCODE -ne 0 -or -not ($versionLine -match '(\d+\.\d+\.\d+)')) {
    Stop-WithError ("WSL absent ou trop ancien. Dans un terminal administrateur : " +
        "wsl --install --no-distribution (puis redémarrer), ou wsl --update.")
}
if ([version]$Matches[1] -lt [version]'2.4.4') {
    Stop-WithError ("WSL $($Matches[1]) : --name et --location demandent 2.4.4 au moins. " +
        "Mettre à jour : wsl --update.")
}

$existing = @(wsl --list --quiet | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if ($LASTEXITCODE -ne 0) { Stop-WithError "wsl --list a échoué." }

if ($Remove) {
    if ($existing -notcontains $Name) {
        Stop-WithError "aucune distro « $Name ». Son dossier, s'il reste ($Location), est à effacer à la main."
    }
    wsl --terminate $Name | Out-Null
    wsl --unregister $Name
    if ($LASTEXITCODE -ne 0) { Stop-WithError "wsl --unregister $Name a échoué." }
    # --unregister a déjà effacé le disque virtuel : le dossier vide part, jamais son contenu. Un -Location
    # choisi à la main peut contenir autre chose, qu'un effacement récursif emporterait.
    if (Test-Path $Location) {
        try { [IO.Directory]::Delete($Location) }
        catch { Write-Host "Le dossier $Location n'est pas vide : laissé tel quel." -ForegroundColor Yellow }
    }
    Write-Host "« $Name » est supprimée, avec son disque virtuel." -ForegroundColor Green
    exit 0
}

if ($existing -contains $Name) {
    Stop-WithError "la distro « $Name » existe déjà : -Remove d'abord, ou un autre -Name."
}

# Un nom d'utilisateur Linux : minuscules, chiffres, - et _ seulement. Celui de Windows est adapté ; un -User
# donné à la main et invalide est refusé, plutôt que remplacé en silence.
if ($User) {
    if ($User -cnotmatch '^[a-z_][a-z0-9_-]*$') {
        Stop-WithError "-User « $User » : un nom Linux, en minuscules, chiffres, - et _."
    }
} else {
    $User = ($env:USERNAME.ToLower() -replace '[^a-z0-9_-]', '')
    if ($User -cnotmatch '^[a-z_][a-z0-9_-]*$') { $User = "levain" }
}

if (-not (Test-Path (Join-Path $PSScriptRoot "provision-levain.sh"))) {
    Stop-WithError "provision-levain.sh introuvable à côté de ce script ($PSScriptRoot)."
}

$online = (wsl --list --online | Out-String)
if ($LASTEXITCODE -ne 0) { Stop-WithError "wsl --list --online a échoué : pas de réseau ?" }
if ($online -notmatch [regex]::Escape($Distro)) {
    Stop-WithError "« $Distro » n'est pas proposée par WSL. Voir : wsl --list --online, puis -Distro <nom>."
}

New-Item -ItemType Directory -Force -Path $Location -ErrorAction Stop | Out-Null
Write-Host "Création de « $Name » ($Distro) dans $Location…"
wsl --install $Distro --name $Name --location $Location --no-launch
if ($LASTEXITCODE -ne 0) { Stop-WithError "wsl --install a échoué." }

# Le script est lu depuis le dossier de celui-ci (--cd accepte un chemin Windows) : un chemin Windows passé dans
# la commande perdrait ses barres obliques inverses dans bash -c. Recopié dans la distro sans ses \r (un clone
# sous Windows peut l'avoir réécrit en CRLF), puis lancé depuis ce fichier : un tube vers bash -s laisserait apt
# ou llvm.sh, qui lisent l'entrée standard, y avaler la suite.
$buildFlag = if ($SkipFirstBuild) { "--skip-first-build" } else { "" }
Write-Host "Outillage de la distro (apt, LLVM, vcpkg, emsdk, Claude Code, dépôts, assets)…"
wsl -d $Name -u root --cd $PSScriptRoot -- bash -c ("tr -d '\r' < provision-levain.sh > /tmp/provision-levain.sh" +
    " && bash /tmp/provision-levain.sh '$User' $buildFlag")
if ($LASTEXITCODE -ne 0) {
    Stop-WithError "l'outillage a échoué (voir ci-dessus). La distro reste là pour comprendre : -Remove l'efface."
}

# /etc/wsl.conf, complété par l'outillage, ne vaut qu'au prochain démarrage de la distro.
wsl --terminate $Name | Out-Null

Write-Host ""
Write-Host "« $Name » est prête." -ForegroundColor Green
Write-Host "  Entrer          : wsl -d $Name"
Write-Host "  Git             : git config --global user.name … ; user.email … ; gh auth login (pour pousser)"
Write-Host "  Claude Code     : claude, puis le compte voulu dans le navigateur. Si rien ne s'ouvre : touche c"
Write-Host "                    (l'URL copiée), l'ouvrir dans le navigateur de Windows, coller le code affiché"
Write-Host "                    à « Paste code here if prompted ». Vérifier : /status ; changer : /logout, /login."
Write-Host "  Vérifier        : cd ~/Projects/Levain && tools/verify.sh"
Write-Host "  Tout supprimer  : powershell -ExecutionPolicy Bypass -File $PSCommandPath -Remove"
Write-Host ""
Write-Host "La mémoire de la VM WSL (la moitié de la RAM par défaut) se règle dans %USERPROFILE%\.wslconfig :"
Write-Host "  [wsl2]"
Write-Host "  memory=48GB"
