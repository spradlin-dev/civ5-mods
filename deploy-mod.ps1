# Replaces the deployed "Claude Player" mod in the game's MODS folder with the built DLL and the files under mod/.
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$dll = Join-Path $root 'CvGameCoreSource\BuildOutput\VS2008_ModWin32\CvGameCoreDLL_Expansion2Win32Mod.dll'
$modSrc = Join-Path $root 'mod'
$modDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) "My Games\Sid Meier's Civilization 5\MODS\Claude Player (v 1)"

if (-not (Test-Path $dll)) { throw "DLL not built: $dll" }
if (Test-Path $modDir) { Remove-Item -Recurse -Force $modDir }
New-Item -ItemType Directory -Force $modDir | Out-Null

Copy-Item $dll (Join-Path $modDir 'CvGameCore_Expansion2.dll')
Get-ChildItem $modSrc -Recurse -File | Where-Object { $_.Extension -ne '.modinfo' } | ForEach-Object {
    $rel = $_.FullName.Substring($modSrc.Length + 1)
    $dest = Join-Path $modDir $rel
    New-Item -ItemType Directory -Force (Split-Path $dest) | Out-Null
    Copy-Item $_.FullName $dest
}

$entries = Get-ChildItem $modDir -Recurse -File | Where-Object { $_.Extension -ne '.modinfo' } | ForEach-Object {
    $rel = $_.FullName.Substring($modDir.Length + 1).Replace('\', '/')
    $md5 = (Get-FileHash -Algorithm MD5 $_.FullName).Hash
    $import = if ($_.Extension -eq '.dll') { '1' } else { '0' }
    "    <File md5=`"$md5`" import=`"$import`">$rel</File>"
}

$modinfo = Get-Content (Join-Path $modSrc 'Claude Player.modinfo') -Raw
$modinfo = $modinfo.Replace('{{FILES}}', ($entries -join "`r`n"))
[IO.File]::WriteAllText((Join-Path $modDir 'Claude Player.modinfo'), $modinfo, (New-Object Text.UTF8Encoding $false))

Write-Output "Deployed to $modDir"
$entries | ForEach-Object { Write-Output $_ }
