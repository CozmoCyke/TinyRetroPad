@echo off
setlocal enabledelayedexpansion

echo =============================================
echo  TinyRetroPad - Normal Link Build (no Crinkler)
echo =============================================
echo.

REM ---- Detect MASM32 ----
if exist "C:\masm32\bin\ml.exe" (
    set MASM_DIR=C:\masm32
    echo [OK] MASM32 found at !MASM_DIR!
) else (
    echo [ERR] MASM32 not found at C:\masm32\bin\ml.exe
    exit /b 1
)

REM ---- Detect MSVC Build Tools linker (Hostx86\x86) ----
REM Use pushd to avoid (x86) appearing inside for/if parenthesized blocks
pushd "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC" >nul 2>&1
if errorlevel 1 (
    echo [ERR] MSVC tools dir not found
    exit /b 1
)
set MSVC_VER=
for /f "delims=" %%v in ('dir /b /ad 2^>nul') do set MSVC_VER=%%v
popd
if not defined MSVC_VER (
    echo [ERR] No MSVC version found
    exit /b 1
)
set MSVC_LINK=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\!MSVC_VER!\bin\Hostx86\x86\link.exe
if not exist "!MSVC_LINK!" (
    echo [ERR] MSVC linker not found at !MSVC_LINK!
    exit /b 1
)
echo [OK] MSVC linker: !MSVC_LINK!

REM ---- Resolve MSVC dir from link.exe location ----
for %%l in ("!MSVC_LINK!") do set MSVC_DIR=%%~dpl..\..
set MSVC_LIB=!MSVC_DIR!\lib\x86

REM ---- Detect latest Windows SDK ----
REM We use a CALL trick to avoid (x86) in for/if context
set SDK_DIR=C:\Program Files (x86)\Windows Kits\10
pushd "!SDK_DIR!\Lib" >nul 2>&1
if errorlevel 1 (
    echo [ERR] Windows SDK lib dir not found
    exit /b 1
)
set SDK_LIB_VER=
for /f "delims=" %%v in ('dir /b /ad 2^>nul') do set SDK_LIB_VER=%%v
popd
if not defined SDK_LIB_VER (
    echo [ERR] Windows SDK not found
    exit /b 1
)
echo [OK] Windows SDK version: !SDK_LIB_VER!

set SDK_LIB=!SDK_DIR!\Lib\!SDK_LIB_VER!\um\x86
if not exist "!SDK_LIB!\kernel32.lib" (
    echo [ERR] x86 import libs not found at !SDK_LIB!
    exit /b 1
)

echo.
echo ---- Step 1: Assemble with MASM32 ----
echo      "!MASM_DIR!\bin\ml" /nologo /c /coff /Cp /I"!MASM_DIR!\include" trpad.asm
echo.

"!MASM_DIR!\bin\ml" /nologo /c /coff /Cp /I"!MASM_DIR!\include" trpad.asm
if errorlevel 1 (
    echo [ERR] Assembly failed
    exit /b 1
)
echo [OK] trpad.obj created

echo.
echo ---- Step 2: Link with MSVC linker ----
echo      "!MSVC_LINK!" /nologo trpad.obj /OUT:trpad_link.exe /SUBSYSTEM:WINDOWS /ENTRY:MainEntry /NODEFAULTLIB /LIBPATH:"!SDK_LIB!" /LIBPATH:"!MSVC_LIB!" kernel32.lib user32.lib shell32.lib comdlg32.lib gdi32.lib
echo.

"!MSVC_LINK!" /nologo ^
    trpad.obj ^
    /OUT:trpad_link.exe ^
    /SUBSYSTEM:WINDOWS ^
    /ENTRY:MainEntry ^
    /NODEFAULTLIB ^
    /LIBPATH:"!SDK_LIB!" ^
    /LIBPATH:"!MSVC_LIB!" ^
    kernel32.lib user32.lib shell32.lib comdlg32.lib gdi32.lib
if errorlevel 1 (
    echo [ERR] Linking failed
    del trpad.obj >nul 2>&1
    exit /b 1
)
echo [OK] trpad_link.exe created

echo ---- Cleanup ----
del trpad.obj >nul 2>&1
echo [OK] trpad.obj deleted

echo.
echo ---- Done! trpad_link.exe ----
for %%e in (trpad_link.exe) do echo   Size: %%~ze bytes

echo.
echo ---- Size comparison ----
set SZ_CRINK=0
if exist trpad.exe for %%e in (trpad.exe) do set SZ_CRINK=%%~ze
for %%e in (trpad_link.exe) do set SZ_LINK=%%~ze
echo   trpad.exe      (Crinkler) : !SZ_CRINK! bytes
echo   trpad_link.exe (normal)   : !SZ_LINK! bytes
if !SZ_CRINK! gtr 0 set /a RATIO=!SZ_LINK!/!SZ_CRINK!&echo   Ratio: ~!RATIO!x larger