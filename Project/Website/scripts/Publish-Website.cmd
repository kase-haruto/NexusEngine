@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Publish-Website.ps1" %*
exit /b %ERRORLEVEL%
