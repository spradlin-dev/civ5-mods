@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio 9.0\Common7\Tools\vsvars32.bat" >nul
cd /d "%~dp0CvGameCoreSource"
set PDB=BuildOutput\VS2008_ModWin32\CvGameCoreDLL_Expansion2Win32Mod.pdb
rem A running game keeps the previous symbol file open; renaming it still works and
rem lets the linker write a fresh one.
if exist "%PDB%" move /y "%PDB%" "%PDB%.prev" >nul
vcbuild /nologo /useenv /M4 Expansion2.vs2008.sln "Mod|Win32"
set RC=%ERRORLEVEL%
if exist "%PDB%.prev" del "%PDB%.prev" 2>nul
if %RC% neq 0 (endlocal & exit /b %RC%)
rem ModBuddy packages the copy in mod\, so a build has to refresh it; the symbols are kept
rem in deployed-symbols\ for resolving crash offsets from installed builds.
copy /y "BuildOutput\VS2008_ModWin32\CvGameCoreDLL_Expansion2Win32Mod.dll" "..\mod\CvGameCore_Expansion2.dll" >nul
if errorlevel 1 set RC=1
if not exist "..\deployed-symbols" mkdir "..\deployed-symbols"
copy /y "BuildOutput\VS2008_ModWin32\CvGameCoreDLL_Expansion2Win32Mod.*" "..\deployed-symbols\" >nul
if errorlevel 1 set RC=1
endlocal & exit /b %RC%
