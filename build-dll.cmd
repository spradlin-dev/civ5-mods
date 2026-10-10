@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio 9.0\Common7\Tools\vsvars32.bat" >nul
cd /d "%~dp0CvGameCoreSource"
vcbuild /nologo /useenv /M4 Expansion2.vs2008.sln "Mod|Win32"
endlocal
