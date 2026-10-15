# Builds the mod folder with ModBuddy's own build targets (mod/LocalMCPServerDLL.civ5proj describes the mod)
# and updates the deployed copy in the game's MODS folder. The .modinfo is rewritten only when its text changes:
# the game re-registers a mod whose .modinfo changed and forgets that it was enabled, which also makes saves
# made with it look like they need a missing mod.
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$dll = Join-Path $root 'CvGameCoreSource\BuildOutput\VS2008_ModWin32\CvGameCoreDLL_Expansion2Win32Mod.dll'
$modSrc = Join-Path $root 'mod'
$buildDir = Join-Path $root 'Build\Local MCP Server DLL'
$userDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) "My Games\Sid Meier's Civilization 5"

if (-not (Test-Path $dll)) { throw "DLL not built: $dll" }
Copy-Item $dll (Join-Path $modSrc 'CvGameCore_Expansion2.dll') -Force
# Symbols for the deployed DLL, so a crash offset from the Windows event log can be resolved later.
$symDir = Join-Path $root 'deployed-symbols'
New-Item -ItemType Directory -Force $symDir | Out-Null
Get-ChildItem (Split-Path $dll) -Filter 'CvGameCoreDLL_Expansion2Win32Mod.*' | Copy-Item -Destination $symDir -Force

$gamePath = (Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 8930' -ErrorAction SilentlyContinue).InstallLocation
if (-not $gamePath) { throw "Civilization V install folder not found in the registry (Steam App 8930)" }
$msbuild = Join-Path $env:windir 'Microsoft.NET\Framework\v4.0.30319\MSBuild.exe'
& $msbuild (Join-Path $modSrc 'LocalMCPServerDLL.civ5proj') /t:Build "/p:Configuration=Package Only" "/p:SafeName=Local MCP Server DLL" "/p:Civ5Path=$gamePath" "/p:Civ5UserPath=$userDir" /nologo /v:q
if ($LASTEXITCODE -ne 0) { throw "ModBuddy build failed" }

# The build names the .modinfo after the mod and its version, and the game expects the mod folder to match.
$modinfoFile = @(Get-ChildItem $buildDir -Filter *.modinfo)
if ($modinfoFile.Count -ne 1) { throw "expected one .modinfo in $buildDir, found $($modinfoFile.Count)" }
$modinfoName = $modinfoFile[0].Name
$modsDir = Join-Path $userDir 'MODS'
$modDir = Join-Path $modsDir $modinfoFile[0].BaseName
# Only one version of the mod may be installed, or the game lists it twice.
$safeName = $modinfoFile[0].BaseName -replace ' \(v \d+\)$', ''
$installed = @(Get-ChildItem $modsDir -Directory -Filter "$safeName (v *)")
# The game locks the DLL while the mod is active; at the main menu it is free.
foreach ($dllFile in ($installed | Get-ChildItem -Recurse -Filter *.dll)) {
    try { [IO.File]::Open($dllFile.FullName, 'Open', 'ReadWrite', 'None').Close() }
    catch { throw "the game still has $($dllFile.Name) loaded; go to the main menu or close the game, then run again" }
}
$installed | Where-Object { $_.FullName -ne $modDir } | ForEach-Object {
    Remove-Item $_.FullName -Recurse -Force; Write-Output "Removed old version $($_.Name)"
}

New-Item -ItemType Directory -Force $modDir | Out-Null
$wanted = @()
Get-ChildItem $buildDir -Recurse -File | Where-Object { $_.Name -ne $modinfoName } | ForEach-Object {
    $rel = $_.FullName.Substring($buildDir.Length + 1)
    $wanted += $rel
    $dest = Join-Path $modDir $rel
    New-Item -ItemType Directory -Force (Split-Path $dest) | Out-Null
    Copy-Item $_.FullName $dest -Force
}
Get-ChildItem $modDir -Recurse -File | Where-Object { $_.Name -ne $modinfoName } | ForEach-Object {
    $rel = $_.FullName.Substring($modDir.Length + 1)
    if ($wanted -notcontains $rel) { Remove-Item $_.FullName -Force; Write-Output "Removed stale $rel" }
}

# The checksums change with every DLL build and the game does not need them; without them the .modinfo text
# stays the same from one build to the next.
$modinfo = [IO.File]::ReadAllText((Join-Path $buildDir $modinfoName)) -replace ' md5="[0-9A-Fa-f]+"', ''
$target = Join-Path $modDir $modinfoName
if ((Test-Path $target) -and ([IO.File]::ReadAllText($target) -eq $modinfo)) {
    Write-Output "Deployed to $modDir (modinfo unchanged, the mod stays enabled)"
} else {
    [IO.File]::WriteAllText($target, $modinfo, (New-Object Text.UTF8Encoding $false))
    Write-Output "Deployed to $modDir"
    Write-Output "modinfo changed: the game will re-register the mod once, so re-enable it in the Mods menu this time"
}
