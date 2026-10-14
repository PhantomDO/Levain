# Le pilote de resize-sandbox.sh, à côté, qui le passe à powershell.exe par -EncodedCommand : redimensionne,
# réduit et restaure la fenêtre de l'exe lancé par le .sh par user32.dll, comme le ferait un utilisateur. C'est Windows
# qui agit, pas le programme : le moteur reçoit les mêmes événements que d'une souris.
#
# Pas « -Command - < resize-sandbox.ps1 » : lu sur l'entrée standard, un script qui contient un Add-Type @" … "@
# ne fait rien, sans un message (build/GOTCHA.md, « Redimensionner la fenêtre d'un exe lancé de la distro »).
# Sort en 1, en le disant, si la fenêtre n'apparaît pas ou qu'un redimensionnement est refusé (règle n°7).

# Sans cela, la distro lirait nos messages dans la page de code de la console, les accents perdus.
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
# La barre de progression du chargement des modules, sortie redirigée, arrive en XML sur la sortie d'erreur.
$ProgressPreference = "SilentlyContinue"

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class LevainWindow {
  [DllImport("user32.dll")]
  public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
}
"@

# La fenêtre n'a de poignée qu'une fois le device créé et les assets parcourus : jusqu'à 60 s en Debug.
# Le processus que le .sh vient de lancer, et lui seul (règle du portable : jamais un autre). Le .sh donne le nom
# de l'exe (LEVAIN_RESIZE_EXE, sans « .exe »), son chemin Windows (LEVAIN_RESIZE_PATH) et les PID de ce nom déjà en
# cours avant le lancement (LEVAIN_RESIZE_BEFORE, séparés par des virgules), que l'on écarte. Il faut un candidat,
# un seul : zéro ou plusieurs, on échoue sans toucher à rien.
$exeName = $env:LEVAIN_RESIZE_EXE
$exePath = $env:LEVAIN_RESIZE_PATH
if (-not $exeName -or -not $exePath) {
    "pilote : LEVAIN_RESIZE_EXE et LEVAIN_RESIZE_PATH manquent (lancer par resize-sandbox.sh)"
    exit 1
}
$before = @()
if ($env:LEVAIN_RESIZE_BEFORE) { $before = $env:LEVAIN_RESIZE_BEFORE -split "," | ForEach-Object { [int]$_ } }
$candidates = @()
for ($i = 0; $i -lt 120 -and $candidates.Count -eq 0; $i++) {
    Start-Sleep -Milliseconds 500
    # -ieq : Windows ne distingue pas la casse des chemins. Path peut manquer à un processus protégé : $null.
    $candidates = @(Get-Process $exeName -ErrorAction SilentlyContinue | Where-Object {
        $_.MainWindowHandle -ne 0 -and $before -notcontains $_.Id -and $_.Path -ieq $exePath })
}
if ($candidates.Count -ne 1) {
    "pilote : $($candidates.Count) fenêtres $exeName candidates en 60 s (une seule attendue), rien n'est touché"
    exit 1
}
$process = $candidates[0]
$window = $process.MainWindowHandle

# SWP_NOZORDER | SWP_NOACTIVATE : la place et la taille seulement, sans changer l'ordre des fenêtres ni prendre
# le focus à qui l'a.
$flags = 0x0014
function Resize-Window([int]$x, [int]$y, [int]$width, [int]$height) {
    if (-not [LevainWindow]::SetWindowPos($window, [IntPtr]::Zero, $x, $y, $width, $height, $flags)) {
        "pilote : SetWindowPos ${width} x ${height} refusé"
        exit 1
    }
    "pilote : fenêtre en ${width} x ${height}"
}

# Quelques images à la taille de départ, puis deux tailles, réduite, restaurée, une troisième taille. Les
# pauses laissent la boucle dessiner à chaque étape : une swapchain se reconstruit à l'image qui suit.
Start-Sleep -Seconds 4
Resize-Window 100 100 1280 720
Start-Sleep -Seconds 2
Resize-Window 50 50 1600 900
Start-Sleep -Seconds 2
# SW_MINIMIZE, puis SW_RESTORE. ShowWindow rend l'état d'avant l'appel, pas un succès : son retour ne dit rien.
[LevainWindow]::ShowWindow($window, 6) | Out-Null
"pilote : réduite"
Start-Sleep -Seconds 1
[LevainWindow]::ShowWindow($window, 9) | Out-Null
"pilote : restaurée"
Start-Sleep -Seconds 1
Resize-Window 0 0 1920 1080
exit 0
