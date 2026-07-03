@echo off
setlocal enabledelayedexpansion

set SRC_DIR=src
set BUILD_DIR=build
set PKG_DIR=C:\dev\TinyRetroPad\packages\Microsoft.Web.WebView2

REM --- Toolchain paths ---
set VCTOOLS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207
set VCTOOLS_BIN=%VCTOOLS%\bin\Hostx86\x86
set WINKIT=C:\Program Files (x86)\Windows Kits\10
set WINKIT_VER=10.0.26100.0

set PATH=%VCTOOLS_BIN%;%PATH%

set INCLUDE=%VCTOOLS%\include;%WINKIT%\Include\%WINKIT_VER%\um;%WINKIT%\Include\%WINKIT_VER%\shared;%WINKIT%\Include\%WINKIT_VER%\ucrt;%WINKIT%\Include\%WINKIT_VER%\winrt;%PKG_DIR%\build\native\include
set LIB=%VCTOOLS%\lib\x86;%WINKIT%\Lib\%WINKIT_VER%\um\x86;%WINKIT%\Lib\%WINKIT_VER%\ucrt\x86
set WV2_LIB=%PKG_DIR%\build\native\x86

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo === Compiling %SRC_DIR%\tinymagicoditor_webview2.c ===
echo INCLUDE=%INCLUDE%
echo LIB=%LIB%

cl.exe /nologo /Zi /Fe"%BUILD_DIR%\TinyMagicoditorWeb.exe" /Fo"%BUILD_DIR%\tinymagicoditor_webview2.obj" ^
    "%SRC_DIR%\tinymagicoditor_webview2.c" ^
    /link /SUBSYSTEM:WINDOWS ^
    kernel32.lib user32.lib ole32.lib oleaut32.lib advapi32.lib shell32.lib comdlg32.lib ^
    "%WV2_LIB%\WebView2Loader.dll.lib"

if %ERRORLEVEL% neq 0 (
    echo === COMPILATION FAILED ===
    exit /b 1
)

echo === Copying WebView2Loader.dll ===
copy /Y "%WV2_LIB%\WebView2Loader.dll" "%BUILD_DIR%\"

echo === Build complete: %BUILD_DIR%\TinyMagicoditorWeb.exe ===
endlocal
