# TinyMagicoditorWeb v0.2 — WebView2 QRFont Prototype

## Verdict
**WebView2 is the functional GSUB rendering path for QR Font 1L.** Rich Edit is a confirmed dead end for QRFont GSUB; WebView2/Chromium correctly applies the font's GSUB substitutions and renders scannable QR codes.

## Architecture
```
TinyMagicoditorWeb.exe
  ├─ Win32 host window (C, MSVC, ~500 LOC)
  ├─ WebView2 child view (Edge Chromium 149)
  ├─ HTML generated in memory (no files written)
  ├─ CSS: font-family: "QR Font 1L" at 48px
  ├─ JS: postMessage sync on every input event
  └─ File: plain text only (.txt / .mgc)
```

## Key Results
| Test | Result |
|------|--------|
| `[hello]` | ✅ Scannable QR code |
| `[https://example.com]` | ⚠️ QR renders but readability limited by font capacity / font size |
| `[WIFI:T:WPA;S:MyNet;P:pass123;;]` | ❓ Not yet tested |
| Plain-text save | ✅ Saved file is raw text, not HTML/RTF |
| Multi-line save + reopen | ✅ JSON `\n` properly decoded → real line breaks |
| Notepad compatibility | ✅ CRLF line endings on save |

## Build Details
- **Toolchain**: MSVC 14.44.35207 (VS 2022 Build Tools), Windows SDK 10.0.26100.0
- **WebView2 SDK**: v1.0.3065.39 (NuGet), `WebView2Loader.dll` redistribué à côté de l'EXE
- **Binary**: `build\TinyMagicoditorWeb.exe` / `WebView2Loader.dll`
- **Source**: `src\tinymagicoditor_webview2.c`
- **Build**: `build-webview2.bat`

## Key Features
- File Open / Save / Save As via Win32 common dialogs
- Menu: File (Open, Save, Save As, Exit) + Help (About)
- WebView2 fills client area; resizes with window
- In-memory HTML generation with QR Font 1L
- Real-time text → QR preview via JavaScript `postMessage`
- JSON string decoding (`\n`, `\t`, `\"`, `\\`, `\uXXXX`) in C message handler
- CRLF conversion on save (Windows line endings)

## Limitations
- Requires WebView2 Runtime (Edge Chromium) installed; ships `WebView2Loader.dll` (177 KB) alongside EXE
- Binary larger than ASM version: ~33 KB EXE + 177 KB DLL = ~210 KB vs 12 KB for TinyMagicoditor
- No scrollbar/font-size controls yet
- URL-length magicode readability depends on QR Font 1L's module density at 48px

## Files
- `src/tinymagicoditor_webview2.c` — main source (Win32 + WebView2 COM)
- `build-webview2.bat` — build script
- `build/TinyMagicoditorWeb.exe` — output binary
- `build/WebView2Loader.dll` — WebView2 loader stub
- `packages/Microsoft.Web.WebView2/` — SDK (not tracked in git)

## Branch
`feature/tinymagicoditor-webview2-c`

## Date
2026-07-04
