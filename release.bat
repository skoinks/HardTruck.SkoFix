@echo off
rem Packs build\dinput.dll, the settings, the license and dgVoodoo2 into dist\HardTruck.SkoFix.zip
setlocal
cd /d "%~dp0"
set "TAR=%SystemRoot%\System32\tar.exe"
if not exist build\dinput.dll echo Run build.bat first.& exit /b 1
if not exist build\dgvoodoo\DDraw.dll echo Run tools\get-dgvoodoo.bat first.& exit /b 1
if not exist build\dgvoodoo\D3DImm.dll echo Run tools\get-dgvoodoo.bat first.& exit /b 1
if exist dist rmdir /s /q dist
if exist dist echo dist is in use.& exit /b 1
mkdir dist\pack || exit /b 1
copy /y build\dinput.dll dist\pack >nul || exit /b 1
copy /y HardTruck.SkoFix.ini dist\pack >nul || exit /b 1
copy /y LICENSE dist\pack\HardTruck.SkoFix.license.txt >nul || exit /b 1
copy /y build\dgvoodoo\DDraw.dll dist\pack >nul || exit /b 1
copy /y build\dgvoodoo\D3DImm.dll dist\pack >nul || exit /b 1
copy /y dgvoodoo\dgVoodoo.conf dist\pack >nul || exit /b 1
copy /y dgvoodoo\dgVoodoo.txt dist\pack >nul || exit /b 1
"%TAR%" -a -c -f dist\HardTruck.SkoFix.zip -C dist\pack D3DImm.dll DDraw.dll dgVoodoo.conf dgVoodoo.txt dinput.dll HardTruck.SkoFix.ini HardTruck.SkoFix.license.txt || exit /b 1
rmdir /s /q dist\pack
certutil -hashfile dist\HardTruck.SkoFix.zip SHA256
