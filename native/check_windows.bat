@echo off
rem Runs the checks the Windows CI jobs run (ci\windows-checks.toml): builds with
rem `native\build wheelhouse meta`, then the tests, keeping going after a failure,
rem and prints a summary. Exits non-zero if any check failed. Arguments pass
rem through to ci\run-windows-checks.py, e.g.
rem   native\check_windows --group unit
rem   native\check_windows --skip build
rem   native\check_windows --list
rem Sets up the Visual Studio C++ environment first when cl.exe is not on PATH.
setlocal
where cl.exe >nul 2>nul && goto :have_msvc
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%vswhere%" goto :no_msvc
set "vcvars="
for /f "usebackq delims=" %%i in (`call "%vswhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find VC\Auxiliary\Build\vcvars64.bat`) do set "vcvars=%%i"
if not defined vcvars goto :no_msvc
call "%vcvars%" >nul 2>nul
where cl.exe >nul 2>nul || goto :no_msvc

:have_msvc
rem Cargo run from Git Bash can find its link.exe first; pin MSVC's, as CI does.
if not defined CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_LINKER for /f "delims=" %%i in ('where cl.exe') do if not defined CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_LINKER set "CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_LINKER=%%~dpilink.exe"
python "%~dp0..\ci\run-windows-checks.py" %*
exit /b %ERRORLEVEL%

:no_msvc
echo check_windows: cl.exe is not on PATH and no Visual Studio C++ build tools were found; run from a developer command prompt. 1>&2
exit /b 1
