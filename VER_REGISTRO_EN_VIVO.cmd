@echo off
chcp 65001 >nul
title Predictive Buffer V5 - visor CMD
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\watch-log.ps1"
if errorlevel 1 echo.
if errorlevel 1 echo Error abriendo el visor. Comprueba PowerShell y el archivo debug.log.
pause
