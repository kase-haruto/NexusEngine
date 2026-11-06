@echo off
setlocal
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Create-LauncherShortcut.ps1" %*
set "shortcutExitCode=%errorlevel%"
if not "%shortcutExitCode%"=="0" (
    echo.
    echo Shortcut creation failed. See the error above.
    pause
)
exit /b %shortcutExitCode%
