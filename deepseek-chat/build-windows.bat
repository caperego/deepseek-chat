@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

where qmake >nul 2>nul
if errorlevel 1 (
    echo Erro: qmake nao encontrado no PATH.
    echo Abra o "Qt 6.x for Desktop" no menu Iniciar ou adicione o Qt ao PATH.
    exit /b 1
)

if not exist build mkdir build
cd build

echo ==^> qmake
qmake ..\deepseek-chat.pro
if errorlevel 1 exit /b 1

where mingw32-make >nul 2>nul
if not errorlevel 1 (
    echo ==^> mingw32-make
    mingw32-make -j%NUMBER_OF_PROCESSORS%
) else (
    echo ==^> nmake
    nmake
)
if errorlevel 1 exit /b 1

echo.
echo Binario gerado em: %~dp0bin\deepseek-chat.exe
echo Para distribuir, rode: windeployqt %~dp0bin\deepseek-chat.exe
