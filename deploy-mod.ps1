# Updates the deployed "Claude Player" mod in the game's MODS folder with the built DLL and the files under mod/.
# The .modinfo is rewritten only when its text changes: the game re-registers a mod whose .modinfo changed and
# forgets that it was enabled, which also makes saves made with it look like they need a missing mod.
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$dll = Join-Path $root 'CvGameCoreSource\BuildOutput\VS2008_ModWin32\CvGameCoreDLL_Expansion2Win32Mod.dll'
$modSrc = Join-Path $root 'mod'
$modDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) "My Games\Sid Meier's Civilization 5\MODS\Claude Player (v 1)"
$modinfoName = 'Claude Player.modinfo'

if (-not (Test-Path $dll)) { throw "DLL not built: $dll" }
New-Item -ItemType Directory -Force $modDir | Out-Null

Copy-Item $dll (Join-Path $modDir 'CvGameCore_Expansion2.dll') -Force
# Symbols for the deployed DLL, so a crash offset from the Windows event log can be resolved later.
$symDir = Join-Path $root 'deployed-symbols'
New-Item -ItemType Directory -Force $symDir | Out-Null
Get-ChildItem (Split-Path $dll) -Filter 'CvGameCoreDLL_Expansion2Win32Mod.*' | Copy-Item -Destination $symDir -Force

$wanted = @('CvGameCore_Expansion2.dll')
Get-ChildItem $modSrc -Recurse -File | Where-Object { $_.Name -ne $modinfoName } | ForEach-Object {
    $rel = $_.FullName.Substring($modSrc.Length + 1)
    $wanted += $rel
    $dest = Join-Path $modDir $rel
    New-Item -ItemType Directory -Force (Split-Path $dest) | Out-Null
    Copy-Item $_.FullName $dest -Force
}
Get-ChildItem $modDir -Recurse -File | Where-Object { $_.Name -ne $modinfoName } | ForEach-Object {
    $rel = $_.FullName.Substring($modDir.Length + 1)
    if ($wanted -notcontains $rel) { Remove-Item $_.FullName -Force; Write-Output "Removed stale $rel" }
}

$entries = $wanted | Sort-Object | ForEach-Object {
    $import = if ($_.EndsWith('.dll')) { '1' } else { '0' }
    "    <File import=`"$import`">$($_.Replace('\', '/'))</File>"
}
$modinfo = [IO.File]::ReadAllText((Join-Path $modSrc $modinfoName)).Replace('{{FILES}}', ($entries -join "`r`n"))
$target = Join-Path $modDir $modinfoName
if ((Test-Path $target) -and ([IO.File]::ReadAllText($target) -eq $modinfo)) {
    Write-Output "Deployed to $modDir (modinfo unchanged, the mod stays enabled)"
} else {
    [IO.File]::WriteAllText($target, $modinfo, (New-Object Text.UTF8Encoding $false))
    Write-Output "Deployed to $modDir"
    Write-Output "modinfo changed: the game will re-register the mod once, so re-enable it in the Mods menu this time"
}
