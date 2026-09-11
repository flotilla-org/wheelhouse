@echo off
setlocal enabledelayedexpansion
cd /D "%~dp0"
:restart

:: --- Usage Notes (2024/1/10) ------------------------------------------------
::
:: This is the Windows build script for the Wheelhouse project. It takes a list
:: of simple alphanumeric-only arguments which control (a) what is built,
:: (b) which compiler & linker are used, and (c) extra high-level build options.
:: By default, if no options are passed, then the "wheelhouse" app is built.
::
:: Below is a non-exhaustive list of possible ways to use the script:
:: `build wheelhouse`
:: `build wheelhouse clang`
:: `build wheelhouse release`
:: `build wheelhouse asan telemetry`
::
:: For a full list of possible build targets and their build command lines,
:: search for @build_targets in this file.
::
:: Below is a list of all possible non-target command line options:
::
:: - `asan`: enable address sanitizer
:: - `ubsan`: enable undefined-behavior sanitizer
:: - `telemetry`: enable RAD telemetry profiling support
:: - `spall`: enable spall profiling support

:: --- Unpack Arguments -------------------------------------------------------
for %%a in (%*) do set "%%~a=1"
if not "%msvc%"=="1" if not "%clang%"=="1" set msvc=1
if not "%release%"=="1" set debug=1
if "%debug%"=="1"   set release=0 && echo [debug mode]
if "%release%"=="1" set debug=0 && echo [release mode]
if "%msvc%"=="1"    set clang=0 && echo [msvc compile]
if "%clang%"=="1"   set msvc=0 && echo [clang compile]
if "%~1"==""                     echo [default mode, assuming `wheelhouse` build] && set wheelhouse=1
if "%~1"=="release" if "%~2"=="" echo [default mode, assuming `wheelhouse` build] && set wheelhouse=1

:: --- Unpack Command Line Build Arguments ------------------------------------
set auto_compile_flags=
set cleat_link=
if "%telemetry%"=="1"               set auto_compile_flags=%auto_compile_flags% -DPROFILE_TELEMETRY=1 && echo [telemetry profiling enabled]
if "%spall%"=="1"                   set auto_compile_flags=%auto_compile_flags% -DPROFILE_SPALL=1 && echo [spall profiling enabled]
if "%asan%"=="1"                    set auto_compile_flags=%auto_compile_flags% -fsanitize=address && echo [asan enabled]
if "%ubsan%"=="1"                   set auto_compile_flags=%auto_compile_flags% -fsanitize=undefined && echo [ubsan enabled]
if "%opengl%"=="1"                  set auto_compile_flags=%auto_compile_flags% -DR_BACKEND=R_BACKEND_OPENGL && echo [opengl render backend]
if "%dwarf%"=="1" if "%clang%"=="1" set auto_compile_flags=%auto_compile_flags% -gdwarf && echo [dwarf debug info]
if "%dwarf%"==""  if "%clang%"=="1" set auto_compile_flags=%auto_compile_flags% -gcodeview
if "%pgo%"=="1" (
  where llvm-profdata /q || echo llvm-profdata is not in the PATH || exit /b 1 
  if "%clang%"=="1" (
    if "%pgo_run%" == "1" (
      call llvm-profdata merge %LLVM_PROFILE_FILE% -output=%~dp0build\build.profdata || exit /b 1
      set auto_compile_flags=%auto_compile_flags% -fprofile-use=%~dp0build\build.profdata
      set pgo_run=0
    ) else (
      echo [pgo enabled]
      set auto_compile_flags=%auto_compile_flags% -fprofile-generate -mllvm -vp-counters-per-site=5
      set LLVM_PROFILE_FILE=%~dp0build\build.profraw
      set pgo_run=1
    )
  ) else (
    echo ERROR: PGO build is not supported with current compiler
    exit /b 1
  )
)
if "%wheelhouse%"=="1" set cleat=1
if "%cleat%"=="1" (
  if "%WHEELHOUSE_CLEAT_DIR%"=="" (set cleat_dir=%~dp0..\cleat) else (set cleat_dir=%WHEELHOUSE_CLEAT_DIR%)
  if "%WHEELHOUSE_CLEAT_FEATURES%"=="" (set cleat_features=ghostty-vt) else (set cleat_features=%WHEELHOUSE_CLEAT_FEATURES%)
  set cleat_profile=debug
  set cleat_profile_flags=
  if "%release%"=="1" set cleat_profile=release && set cleat_profile_flags=--release
  if "%WHEELHOUSE_CLEAT_TARGET_DIR%"=="" (set cleat_target_dir=!cleat_dir!\target) else (set cleat_target_dir=%WHEELHOUSE_CLEAT_TARGET_DIR%)
  set cleat_include_dir=!cleat_dir!\crates\cleat\include
  set cleat_lib_dir=!cleat_target_dir!\!cleat_profile!
  set cleat_feature_flags=
  if not "!cleat_features!"=="none" set cleat_feature_flags=--features "!cleat_features!"
  echo [cleat provider: !cleat_dir!]
  pushd "!cleat_dir!" || exit /b 1
  cargo build -p cleat --locked --no-default-features !cleat_profile_flags! !cleat_feature_flags! || exit /b 1
  popd
  rem cl accepts -I as well as /I, so one spelling serves both compilers; this must
  rem land in auto_compile_flags (not cl_common/clang_common) because the compile
  rem lines below snapshot those via immediate expansion
  set auto_compile_flags=!auto_compile_flags! -I"!cleat_include_dir!"
)

set andamento_link=
if "%wheelhouse%"=="1" (
  if "%WHEELHOUSE_ANDAMENTO_DIR%"=="" (set andamento_dir=%~dp0..\andamento) else (set andamento_dir=%WHEELHOUSE_ANDAMENTO_DIR%)
  for /f "tokens=2" %%t in ('rustc -vV ^| findstr /b "host:"') do set andamento_target=%%t
  if "%WHEELHOUSE_ANDAMENTO_TARGET_DIR%"=="" (set andamento_target_dir=!andamento_dir!\target) else (set andamento_target_dir=%WHEELHOUSE_ANDAMENTO_TARGET_DIR%)
  set andamento_lib_dir=!andamento_target_dir!\!andamento_target!\!cleat_profile!
  pushd "!andamento_dir!" || exit /b 1
  cargo build -p andamento-ffi --locked --target !andamento_target! --target-dir "!andamento_target_dir!" !cleat_profile_flags! || exit /b 1
  popd
  set auto_compile_flags=!auto_compile_flags! -I"!andamento_dir!\crates\andamento-ffi\include"
  set andamento_link="!andamento_lib_dir!\andamento_ffi.dll.lib"
  python tools\embed-sidebar-fixture.py || exit /b 1
)

:: --- Compile/Link Line Definitions ------------------------------------------
set cl_common=     /I..\src\ /I..\local\ /nologo /FC /Z7 /Zc:preprocessor
set cl_debug=      call cl /Od /Ob1 /DBUILD_DEBUG=1 %cl_common% %auto_compile_flags%
set cl_release=    call cl /O2 /DBUILD_DEBUG=0 %cl_common% %auto_compile_flags%
set cl_link=       /link /MANIFEST:EMBED /INCREMENTAL:NO /pdbaltpath:%%%%_PDB%%%% /NATVIS:"%~dp0\src\natvis\base.natvis" /noexp /nocoffgrpinfo /opt:ref /opt:icf
set cl_out=        /out:
set cl_obj_out=    /Fo:
set cl_linker=     
set clang_common=  -I..\src\ -I..\local\ -fdiagnostics-absolute-paths -Wall -Wno-unknown-warning-option -Wno-missing-braces -Wno-unused-function -Wno-unused-parameter -Wno-writable-strings -Wno-missing-field-initializers -Wno-unused-value -Wno-unused-variable -Wno-unused-local-typedef -Wno-deprecated-register -Wno-deprecated-declarations -Wno-unused-but-set-variable -Wno-single-bit-bitfield-constant-conversion -Wno-compare-distinct-pointer-types -Wno-initializer-overrides -Wno-incompatible-pointer-types-discards-qualifiers -Xclang -flto-visibility-public-std -D_USE_MATH_DEFINES -Dstrdup=_strdup -Dgnu_printf=printf -ferror-limit=10000 -mcx16 -msha
set clang_debug=   call clang -g -O0 -DBUILD_DEBUG=1 -D_DEBUG %clang_common% %auto_compile_flags%
set clang_release= call clang -g -O2 -DBUILD_DEBUG=0 -DNDEBUG %clang_common% %auto_compile_flags%
set clang_link=    -fuse-ld=lld -Xlinker /MANIFEST:EMBED -Xlinker /pdbaltpath:%%%%_PDB%%%% -Xlinker /NATVIS:"%~dp0\src\natvis\base.natvis" -Xlinker /opt:ref -Xlinker /opt:noicf
set clang_out=     -o
set clang_obj_out= -o
set clang_linker=  -Xlinker
rem rust cdylibs on windows produce cleat.dll + import lib cleat.dll.lib
if "%cleat%"=="1" if "%msvc%"=="1"  set cleat_link=/LIBPATH:"!cleat_lib_dir!" cleat.dll.lib
if "%cleat%"=="1" if "%clang%"=="1" set cleat_link=-L"!cleat_lib_dir!" -lcleat.dll

:: --- Per-Build Settings -----------------------------------------------------
set link_icon=logo.res
if "%msvc%"=="1"    set linker=%cl_linker%
if "%clang%"=="1"   set linker=%clang_linker%
if "%msvc%"=="1"    set only_compile=/c
if "%clang%"=="1"   set only_compile=-c
if "%msvc%"=="1"    set EHsc=/EHsc
if "%clang%"=="1"   set EHsc=
if "%msvc%"=="1"    set no_aslr=/DYNAMICBASE:NO
if "%clang%"=="1"   set no_aslr=-Wl,/DYNAMICBASE:NO
if "%msvc%"=="1"    set rc=call rc
if "%clang%"=="1"   set rc=call llvm-rc
if "%msvc%"=="1"    set link_dll=/link /DLL
if "%clang%"=="1"   set link_dll=-Xlinker -DLL

:: --- Choose Compile/Link Lines ----------------------------------------------
if "%msvc%"=="1"      set compile_debug=%cl_debug%
if "%msvc%"=="1"      set compile_release=%cl_release%
if "%msvc%"=="1"      set compile_link=%cl_link%
if "%msvc%"=="1"      set out=%cl_out%
if "%msvc%"=="1"      set obj_out=%cl_obj_out%
if "%clang%"=="1"     set compile_debug=%clang_debug%
if "%clang%"=="1"     set compile_release=%clang_release%
if "%clang%"=="1"     set compile_link=%clang_link%
if "%clang%"=="1"     set out=%clang_out%
if "%clang%"=="1"     set obj_out=%clang_obj_out%
if "%debug%"=="1"     set compile=%compile_debug%
if "%release%"=="1"   set compile=%compile_release%

:: --- Prep Directories -------------------------------------------------------
if not exist build mkdir build
if not exist local mkdir local

:: --- Produce Logo Icon File -------------------------------------------------
pushd build
%rc% /nologo /fo logo.res ..\data\logo.rc || exit /b 1
popd

:: --- Get Current Git Commit Id ----------------------------------------------
for /f %%i in ('call git describe --always --dirty')   do set compile=%compile% -DBUILD_GIT_HASH=\"%%i\"
for /f %%i in ('call git rev-parse HEAD')              do set compile=%compile% -DBUILD_GIT_HASH_FULL=\"%%i\"

:: --- Build & Run Metaprogram ------------------------------------------------
pushd build
if "%meta%"=="1" (
  echo [building metagen]
  %compile_debug% ..\src\metagen\metagen_main.c %compile_link% %out%metagen.exe || exit /b 1
)
if "%no_meta%"=="" if exist metagen.exe (
  echo [running metagen]
  metagen.exe || exit /b 1
)
popd

:: --- Build Everything (@build_targets) --------------------------------------
pushd build
if "%wheelhouse%"=="1"                    set didbuild=1 && %compile% ..\src\uishell\uishell_main.c                            %compile_link% %link_icon% %cleat_link% %andamento_link% %out%wheelhouse.exe || exit /b 1
if "%wheelhouse%"=="1" if "%cleat%"=="1"  copy /y "!cleat_lib_dir!\cleat.dll" . >nul
if "%wheelhouse%"=="1" copy /y "!andamento_lib_dir!\andamento_ffi.dll" . >nul
popd

:: --- Warn On No Builds ------------------------------------------------------
if "%didbuild%"=="" (
  echo [WARNING] no valid build target specified; must use build target names as arguments to this script, like `build wheelhouse`.
  exit /b 1
)
