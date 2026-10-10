# Copies the built DLL into the game's MODS folder as the "Claude Player" mod.
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$dll = Join-Path $root 'CvGameCoreSource\BuildOutput\VS2008_ModWin32\CvGameCoreDLL_Expansion2Win32Mod.dll'
$modDir = Join-Path $env:USERPROFILE "Documents\My Games\Sid Meier's Civilization 5\MODS\Claude Player (v 1)"

if (-not (Test-Path $dll)) { throw "DLL not built: $dll" }
New-Item -ItemType Directory -Force $modDir | Out-Null

Copy-Item $dll (Join-Path $modDir 'CvGameCore_Expansion2.dll')
$md5 = (Get-FileHash -Algorithm MD5 (Join-Path $modDir 'CvGameCore_Expansion2.dll')).Hash
$modinfo = Get-Content (Join-Path $root 'mod\Claude Player.modinfo') -Raw
$modinfo = $modinfo.Replace('{{DLL_MD5}}', $md5)
[IO.File]::WriteAllText((Join-Path $modDir 'Claude Player.modinfo'), $modinfo, (New-Object Text.UTF8Encoding $false))

Write-Output "Deployed to $modDir (dll md5 $md5)"
