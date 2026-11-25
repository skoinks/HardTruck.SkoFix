@echo off
rem Downloads dgVoodoo2 2.87.6, checks it and puts the two DLLs into build\dgvoodoo
setlocal
cd /d "%~dp0.."
set "URL=https://github.com/dege-diosg/dgVoodoo2/releases/download/2.87.6/dgVoodoo2_87_6.zip"
set "SHA=9afa493396feec7108f42a2d97661eef96bbaac5addf0dfb5cfbc3001cd76a0a"
if not exist build mkdir build
if exist build\dgvoodoo rmdir /s /q build\dgvoodoo
mkdir build\dgvoodoo || exit /b 1
"%SystemRoot%\System32\curl.exe" -sSfL -o build\dgvoodoo.zip "%URL%" || exit /b 1
certutil -hashfile build\dgvoodoo.zip SHA256 | findstr /i /x "%SHA%" >nul
if errorlevel 1 echo The download is not the expected file.& del build\dgvoodoo.zip & exit /b 1
"%SystemRoot%\System32\tar.exe" -x -f build\dgvoodoo.zip -C build\dgvoodoo --strip-components 2 MS/x86/DDraw.dll MS/x86/D3DImm.dll || exit /b 1
