@echo off
chcp 65001 >nul
cd /d "%~dp0"
title Compilacion Predictive Buffer V5 - CMD
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-windows.ps1"
set "resultado=%ERRORLEVEL%"
echo.
if "%resultado%"=="0" (
  echo Compilacion terminada. Mira build-windows para el archivo .geode.
) else (
  echo Compilacion FALLIDA. Revisa compilacion.log y compartelo para corregir el error.
)
echo.
pause
exit /b %resultado%
