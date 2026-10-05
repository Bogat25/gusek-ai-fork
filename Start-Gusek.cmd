@echo off
rem Portable launcher for GUSEK with local AI assistant
setlocal
set "APP_DIR=%~dp0"
if "%APP_DIR:~-1%"=="\" set "APP_DIR=%APP_DIR:~0,-1%"
set "GUSEK_AI_DATA=%APP_DIR%\work\assistant"
set "SciTE_USERHOME=%APP_DIR%\work\config"
if not exist "%GUSEK_AI_DATA%" mkdir "%GUSEK_AI_DATA%"
if not exist "%SciTE_USERHOME%" mkdir "%SciTE_USERHOME%"
start "" "%APP_DIR%\gusek.exe" %*
