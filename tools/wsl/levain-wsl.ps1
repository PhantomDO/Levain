<#
.SYNOPSIS
    Une distro WSL dédiée à Levain : Ubuntu 26.04, comme la CI et la distrobox dev-ubuntu, rangée dans son
    propre dossier et supprimable d'une commande, sans rien laisser derrière elle.

.DESCRIPTION
    Crée la distro « levain-dev » (WSL 2.4.4 au moins, pour --name et --location), puis l'outille par
    provision-levain.sh, posé à côté de ce script : LLVM 23, vcpkg et emsdk aux versions de la CI, Claude Code,
    et les dépôts Levain et Rando. Rien n'est installé sous Windows ; tout vit dans le disque virtuel de la
    distro, dans -Location.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\levain-wsl.ps1
    powershell -ExecutionPolicy Bypass -File .\levain-wsl.ps1 -SkipFirstBuild
    powershell -ExecutionPolicy Bypass -File .\levain-wsl.ps1 -Remove
#>
param(
    [string]$Name = "levain-dev",
    [string]$Distro = "Ubuntu-26.04",
    [string]$Location = (Join-Path $env:LOCALAPPDATA "levain-wsl"),
    [string]$User = "",
    [switch]$SkipFirstBuild,
    [switch]$Remove
)

$ErrorActionPreference = "Stop"
# wsl.exe écrit en UTF-16 par défaut : PowerShell 5.1 y lirait des caractères nuls entre les lettres.
$env:WSL_UTF8 = "1"

function Stop-WithError([string]$Message) {
    Write-Host "levain-wsl : $Message" -ForegroundColor Red
    exit 1
}

wsl --status > $null 2>&1
if ($LASTEXITCODE -ne 0) {
    Stop-WithError "WSL n'est pas installé. Dans un terminal administrateur : wsl --install --no-distribution, puis redémarrer."
}

$existing = @(wsl --list --quiet | ForEach-Object { $_.Trim() } | Where-Object { $_ })

if ($Remove) {
    if ($existing -notcontains $Name) { Stop-WithError "aucune distro « $Name » à supprimer." }
    wsl --terminate $Name > $null 2>&1
    wsl --unregister $Name
    if ($LASTEXITCODE -ne 0) { Stop-WithError "wsl --unregister $Name a échoué." }
    if (Test-Path $Location) { Remove-Item -Recurse -Force $Location }
    Write-Host "« $Name » est supprimée, avec son disque virtuel ($Location)." -ForegroundColor Green
    exit 0
}

if ($existing -contains $Name) {
    Stop-WithError "la distro « $Name » existe déjà : -Remove d'abord, ou un autre -Name."
}

# Un nom d'utilisateur Linux : minuscules, chiffres, - et _ seulement.
if (-not $User) { $User = ($env:USERNAME.ToLower() -replace '[^a-z0-9_-]', '') }
if (-not $User -or $User -notmatch '^[a-z_][a-z0-9_-]*$') { $User = "levain" }

$provision = Join-Path $PSScriptRoot "provision-levain.sh"
if (-not (Test-Path $provision)) { Stop-WithError "provision-levain.sh introuvable à côté de ce script ($provision)." }

$online = (wsl --list --online | Out-String)
if ($online -notmatch [regex]::Escape($Distro)) {
    Stop-WithError "« $Distro » n'est pas proposée par WSL. Voir : wsl --list --online, puis -Distro <nom>."
}

New-Item -ItemType Directory -Force -Path $Location | Out-Null
Write-Host "Création de « $Name » ($Distro) dans $Location…"
wsl --install $Distro --name $Name --location $Location --no-launch
if ($LASTEXITCODE -ne 0) {
    Stop-WithError "wsl --install a échoué. --name et --location demandent WSL 2.4.4 au moins : wsl --update."
}

# Le script est recopié dans la distro sans ses \r : un clone sous Windows peut l'avoir réécrit en CRLF, que bash
# refuserait ligne par ligne. Une copie dans un fichier, et non un tube vers `bash -s` : apt ou llvm.sh, qui lisent
# l'entrée standard, y avaleraient la suite du script, et une lecture ratée donnerait à bash une entrée vide, qu'il
# exécuterait sans erreur.
$linuxPath = (wsl -d $Name -u root -- wslpath -a "$provision").Trim()
$buildFlag = if ($SkipFirstBuild) { "--skip-first-build" } else { "" }
Write-Host "Outillage de la distro (apt, LLVM, vcpkg, emsdk, Claude Code, dépôts)…"
wsl -d $Name -u root -- bash -c "tr -d '\r' < '$linuxPath' > /tmp/provision-levain.sh && bash /tmp/provision-levain.sh '$User' $buildFlag"
if ($LASTEXITCODE -ne 0) {
    Stop-WithError "l'outillage a échoué (voir ci-dessus). La distro reste là pour comprendre : -Remove pour l'effacer."
}

# /etc/wsl.conf, écrit par l'outillage, ne vaut qu'au prochain démarrage de la distro.
wsl --terminate $Name > $null 2>&1

Write-Host ""
Write-Host "« $Name » est prête." -ForegroundColor Green
Write-Host "  Entrer          : wsl -d $Name"
Write-Host "  GitHub          : gh auth login   (pour pousser)"
Write-Host "  Claude Code     : claude   puis le compte voulu dans le navigateur. Si rien ne s'ouvre : touche c"
Write-Host "                    (l'URL copiée), ouvrir dans le navigateur de Windows, puis coller le code affiché"
Write-Host "                    à « Paste code here if prompted ». Vérifier avec /status ; changer : /logout, /login."
Write-Host "                    Ni ANTHROPIC_API_KEY ni CLAUDE_CODE_OAUTH_TOKEN dans la distro : ils passeraient"
Write-Host "                    avant le compte du /login."
Write-Host "  Vérifier        : cd ~/Projects/Levain && tools/verify.sh"
Write-Host "  Tout supprimer  : powershell -ExecutionPolicy Bypass -File $PSCommandPath -Remove"
Write-Host ""
Write-Host "La mémoire de la VM WSL se règle pour toutes les distros dans %USERPROFILE%\.wslconfig :"
Write-Host "  [wsl2]"
Write-Host "  memory=48GB"
