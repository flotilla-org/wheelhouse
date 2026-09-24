@echo off
rem Runs the Wheelhouse UI diagnostics against build\wheelhouse.exe; build first with
rem `build wheelhouse`. Arguments pass through, e.g.
rem   run_tests -Diagnostics sidebar,panel
rem   run_tests -Diagnostics terminal_glyph   (fails until colour emoji lands, #59)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\run-windows-diagnostics.ps1" %*
exit /b %ERRORLEVEL%
