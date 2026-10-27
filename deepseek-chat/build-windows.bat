@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

rem O navegador integrado usa o Qt WebEngine, que no Windows so existe no kit
rem MSVC. Defina QT_DIR (ex.: C:\Qt\6.12.0\msvc2022_64) para escolher o kit.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if defined QT_DIR set "PATH=%QT_DIR%\bin;%PATH%"

where qmake >nul 2>nul
if errorlevel 1 (
    for /d %%d in (C:\Qt\6.*) do (
        if exist "%%d\msvc2022_64\bin\qmake.exe" set "QT_DIR=%%d\msvc2022_64"
    )
    if not defined QT_DIR (
        echo Erro: qmake nao encontrado. Instale o Qt 6 com o kit MSVC 2022 64-bit e o Qt WebEngine,
        echo ou defina QT_DIR com a pasta do kit.
        exit /b 1
    )
    set "PATH=!QT_DIR!\bin;!PATH!"
)

for /f "delims=" %%p in ('qmake -query QT_INSTALL_PREFIX') do set "QT_PREFIX=%%p"
if not exist "!QT_PREFIX!\bin\Qt6WebEngineQuick.dll" (
    echo Erro: o kit "!QT_PREFIX!" nao tem o Qt WebEngine.
    echo No Windows ele so existe no kit MSVC: instale "MSVC 2022 64-bit" e "Qt WebEngine"
    echo no Qt Maintenance Tool e defina QT_DIR com a pasta do kit MSVC.
    exit /b 1
)
echo ==^> Qt: !QT_PREFIX!

where cl >nul 2>nul
if errorlevel 1 (
    if not exist "!VSWHERE!" (
        echo Erro: compilador MSVC nao encontrado. Instale o Visual Studio Build Tools 2022
        echo com a carga "Desenvolvimento para desktop com C++".
        exit /b 1
    )
    set "VSDIR="
    for /f "usebackq delims=" %%v in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%v"
    if not defined VSDIR (
        echo Erro: nenhuma instalacao do Visual Studio com C++ x64 foi encontrada.
        exit /b 1
    )
    echo ==^> MSVC: !VSDIR!
    rem O vcvars64.bat tambem procura o vswhere.exe no PATH.
    for %%i in ("!VSWHERE!") do set "PATH=%%~dpi;!PATH!"
    call "!VSDIR!\VC\Auxiliary\Build\vcvars64.bat" >nul
    if errorlevel 1 exit /b 1
)

rem Pasta separada da build/ do Linux: o .qmake.stash do GCC quebra o qmake do MSVC.
if not exist build-windows mkdir build-windows
cd build-windows

echo ==^> qmake
qmake ..\deepseek-chat.pro -spec win32-msvc "CONFIG+=release" "CONFIG-=debug_and_release"
if errorlevel 1 exit /b 1

set "JOM="
where jom >nul 2>nul && set "JOM=jom"
if not defined JOM if exist "C:\Qt\Tools\QtCreator\bin\jom\jom.exe" set "JOM=C:\Qt\Tools\QtCreator\bin\jom\jom.exe"
if defined JOM (
    echo ==^> jom
    "!JOM!"
) else (
    echo ==^> nmake
    nmake
)
if errorlevel 1 exit /b 1

echo.
echo Binario gerado em: %~dp0bin\deepseek-chat.exe
echo Para distribuir, rode: windeployqt --qmldir %~dp0qml %~dp0bin\deepseek-chat.exe
