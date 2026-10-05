@echo off
rem Runs gusek.ps1 without depending on the PowerShell execution policy.
rem   gusek dev     gusek test     gusek full     gusek doctor
setlocal
rem Clear PSModulePath so Windows PowerShell 5.1 does not inherit pwsh modules.
set "PSModulePath="
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0gusek.ps1" %*
exit /b %ERRORLEVEL%
