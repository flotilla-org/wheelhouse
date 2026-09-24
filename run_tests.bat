@echo off
rem Runs the Wheelhouse UI diagnostics against build\wheelhouse.exe; build first with
rem `build wheelhouse`. Arguments pass through, e.g.
rem   run_tests -Diagnostics sidebar,panel
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\run-windows-diagnostics.ps1" %*
exit /b %ERRORLEVEL%
