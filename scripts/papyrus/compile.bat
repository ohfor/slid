@echo off
setlocal

set SKYRIM=D:\SteamLibrary\steamapps\common\Skyrim Special Edition
set COMPILER=%SKYRIM%\Papyrus Compiler\PapyrusCompiler.exe
set FLAGS=%SKYRIM%\Data\source\Scripts\TESV_Papyrus_Flags.flg
set OUTPUT=%SKYRIM%\Data\Scripts
set IMPORTS=%SKYRIM%\Data\Scripts\Source;%SKYRIM%\Data\source\Scripts;%~dp0source

if not exist "%COMPILER%" (
    echo ERROR: PapyrusCompiler.exe not found at %COMPILER%
    exit /b 1
)

if "%~1"=="" (
    echo Compiling all scripts...
    for %%f in ("%~dp0source\SLID_*.psc") do (
        echo   %%~nxf
        "%COMPILER%" "%%f" -f="%FLAGS%" -i="%IMPORTS%" -o="%OUTPUT%"
    )
) else (
    echo Compiling %1.psc...
    "%COMPILER%" "%~dp0source\%1.psc" -f="%FLAGS%" -i="%IMPORTS%" -o="%OUTPUT%"
)

echo Done.
