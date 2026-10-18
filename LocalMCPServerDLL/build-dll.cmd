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
endlocal & exit /b %RC%
