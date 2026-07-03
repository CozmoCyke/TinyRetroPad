#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <objbase.h>
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <WebView2.h>

#pragma comment(lib, "shlwapi.lib")

#define APP_NAME L"TinyMagicoditorWeb"
#define APP_TITLE L"TinyMagicoditorWeb - MagicodesPad"
#define CLASS_NAME L"TinyMagicoditorWeb_Window"

#define IDM_OPEN  1001
#define IDM_SAVE  1002
#define IDM_SAVEAS 1003
#define IDM_EXIT  1004
#define IDM_ABOUT 1005
#define IDM_QRPREVIEW 1006

#define MAX_TEXT_SIZE (1024 * 1024)
#define MAX_HTML_SIZE (MAX_TEXT_SIZE * 2 + 4096)

static HWND g_hwnd = NULL;
static ICoreWebView2Controller *g_controller = NULL;
static ICoreWebView2 *g_webview = NULL;
static ICoreWebView2Environment *g_env = NULL;
static wchar_t *g_currentText = NULL;
static wchar_t *g_filePath = NULL;
static int g_textChanged = 0;
static int g_webviewReady = 0;
static int g_qrPreview = 1;

/* ---- COM VTables ---- */

/* EnvCompletedHandler */
static HRESULT STDMETHODCALLTYPE Env_QueryInterface(
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *This,
    REFIID riid, void **ppvObject) {
    if (IsEqualIID(riid, &IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler) ||
        IsEqualIID(riid, &IID_IUnknown)) {
        *ppvObject = This;
        return S_OK;
    }
    *ppvObject = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE Env_AddRef(
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *This) {
    return 1;
}
static ULONG STDMETHODCALLTYPE Env_Release(
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *This) {
    return 1;
}
static HRESULT STDMETHODCALLTYPE Env_Invoke(
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *This,
    HRESULT result, ICoreWebView2Environment *env) {
    if (SUCCEEDED(result)) {
        g_env = env;
        g_env->lpVtbl->AddRef(g_env);
    }
    return S_OK;
}
static ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandlerVtbl envVtbl = {
    Env_QueryInterface, Env_AddRef, Env_Release, Env_Invoke
};
static ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler envHandler = { &envVtbl };

/* ControllerCompletedHandler */
static HRESULT STDMETHODCALLTYPE Ctlr_QueryInterface(
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *This,
    REFIID riid, void **ppvObject) {
    if (IsEqualIID(riid, &IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler) ||
        IsEqualIID(riid, &IID_IUnknown)) {
        *ppvObject = This;
        return S_OK;
    }
    *ppvObject = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE Ctlr_AddRef(
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *This) {
    return 1;
}
static ULONG STDMETHODCALLTYPE Ctlr_Release(
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *This) {
    return 1;
}

static ICoreWebView2Controller *g_newController = NULL;

static HRESULT STDMETHODCALLTYPE Ctlr_Invoke(
    ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *This,
    HRESULT result, ICoreWebView2Controller *controller) {
    if (SUCCEEDED(result)) {
        g_newController = controller;
        g_newController->lpVtbl->AddRef(g_newController);
    }
    return S_OK;
}
static ICoreWebView2CreateCoreWebView2ControllerCompletedHandlerVtbl ctlrVtbl = {
    Ctlr_QueryInterface, Ctlr_AddRef, Ctlr_Release, Ctlr_Invoke
};
static ICoreWebView2CreateCoreWebView2ControllerCompletedHandler ctlrHandler = { &ctlrVtbl };

/* ---- JSON string decoder ---- */

static wchar_t *UnescapeJSON(const wchar_t *json, size_t *outLen) {
    if (!json || json[0] != L'"') return NULL;
    size_t len = wcslen(json);
    if (len < 2 || json[len - 1] != L'"') return NULL;

    const wchar_t *in = json + 1;
    size_t inLen = len - 2;
    wchar_t *out = malloc(sizeof(wchar_t) * (inLen + 1));
    if (!out) return NULL;
    size_t j = 0;

    for (size_t i = 0; i < inLen; i++) {
        if (in[i] == L'\\' && i + 1 < inLen) {
            switch (in[++i]) {
                case L'"':  out[j++] = L'"'; break;
                case L'\\': out[j++] = L'\\'; break;
                case L'/':  out[j++] = L'/'; break;
                case L'b':  out[j++] = L'\b'; break;
                case L'f':  out[j++] = L'\f'; break;
                case L'n':  out[j++] = L'\n'; break;
                case L'r':  out[j++] = L'\r'; break;
                case L't':  out[j++] = L'\t'; break;
                case L'u': {
                    if (i + 4 < inLen) {
                        wchar_t hex[5] = {in[i+1], in[i+2], in[i+3], in[i+4], 0};
                        wchar_t code = (wchar_t)wcstol(hex, NULL, 16);
                        out[j++] = code;
                        i += 4;
                    }
                    break;
                }
                default: out[j++] = in[i]; break;
            }
        } else {
            out[j++] = in[i];
        }
    }
    out[j] = L'\0';
    if (outLen) *outLen = j;
    return out;
}

/* WebMessageReceivedEventHandler */
static HRESULT STDMETHODCALLTYPE Msg_QueryInterface(
    ICoreWebView2WebMessageReceivedEventHandler *This,
    REFIID riid, void **ppvObject) {
    if (IsEqualIID(riid, &IID_ICoreWebView2WebMessageReceivedEventHandler) ||
        IsEqualIID(riid, &IID_IUnknown)) {
        *ppvObject = This;
        return S_OK;
    }
    *ppvObject = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE Msg_AddRef(
    ICoreWebView2WebMessageReceivedEventHandler *This) {
    return 1;
}
static ULONG STDMETHODCALLTYPE Msg_Release(
    ICoreWebView2WebMessageReceivedEventHandler *This) {
    return 1;
}
static HRESULT STDMETHODCALLTYPE Msg_Invoke(
    ICoreWebView2WebMessageReceivedEventHandler *This,
    ICoreWebView2 *sender, ICoreWebView2WebMessageReceivedEventArgs *args) {
    LPWSTR message = NULL;
    if (SUCCEEDED(args->lpVtbl->get_WebMessageAsJson(args, &message))) {
        wchar_t *decoded = UnescapeJSON(message, NULL);
        if (decoded) {
            if (g_currentText) free(g_currentText);
            g_currentText = decoded;
            g_textChanged = 1;
        }
        CoTaskMemFree(message);
    }
    return S_OK;
}
static ICoreWebView2WebMessageReceivedEventHandlerVtbl msgVtbl = {
    Msg_QueryInterface, Msg_AddRef, Msg_Release, Msg_Invoke
};
static ICoreWebView2WebMessageReceivedEventHandler msgHandler = { &msgVtbl };

/* ExecuteScript completed handler (stub) */
static HRESULT STDMETHODCALLTYPE Exec_QueryInterface(
    ICoreWebView2ExecuteScriptCompletedHandler *This,
    REFIID riid, void **ppvObject) {
    if (IsEqualIID(riid, &IID_ICoreWebView2ExecuteScriptCompletedHandler) ||
        IsEqualIID(riid, &IID_IUnknown)) { *ppvObject = This; return S_OK; }
    *ppvObject = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE Exec_AddRef(
    ICoreWebView2ExecuteScriptCompletedHandler *This) { return 1; }
static ULONG STDMETHODCALLTYPE Exec_Release(
    ICoreWebView2ExecuteScriptCompletedHandler *This) { return 1; }
static HRESULT STDMETHODCALLTYPE Exec_Invoke(
    ICoreWebView2ExecuteScriptCompletedHandler *This,
    HRESULT errorCode, LPCWSTR resultObjectAsJson) { return S_OK; }
static ICoreWebView2ExecuteScriptCompletedHandlerVtbl execVtbl = {
    Exec_QueryInterface, Exec_AddRef, Exec_Release, Exec_Invoke
};
static ICoreWebView2ExecuteScriptCompletedHandler execHandler = { &execVtbl };

static void UpdateQRPreview(void) {
    if (!g_webview) return;
    wchar_t script[128];
    swprintf(script, 128,
        L"setQRMode(%ls);",
        g_qrPreview ? L"true" : L"false");
    g_webview->lpVtbl->ExecuteScript(g_webview, script, &execHandler);
}

/* ---- Helpers ---- */

static void EscapeHTML(const wchar_t *in, wchar_t *out, size_t outSize) {
    size_t j = 0;
    for (const wchar_t *p = in; *p && j < outSize - 8; p++) {
        switch (*p) {
            case L'&':  wcscpy(out + j, L"&amp;"); j += 5; break;
            case L'<':  wcscpy(out + j, L"&lt;"); j += 4; break;
            case L'>':  wcscpy(out + j, L"&gt;"); j += 4; break;
            case L'"':  wcscpy(out + j, L"&quot;"); j += 6; break;
            case L'\'': wcscpy(out + j, L"&#39;"); j += 5; break;
            default:    out[j++] = *p; break;
        }
    }
    out[j] = L'\0';
}

static void GenerateHTML(const wchar_t *text, wchar_t *html, size_t htmlSize, int qrPreview) {
    wchar_t *escaped = malloc(sizeof(wchar_t) * (wcslen(text) * 6 + 1));
    if (!escaped) return;
    EscapeHTML(text, escaped, wcslen(text) * 6 + 1);

    swprintf(html, htmlSize,
        L"<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        L"<style>"
        L"*{margin:0;padding:0;box-sizing:border-box;}"
        L"body{background:#f5f0e8;display:flex;justify-content:center;padding:32px;font-family:sans-serif;}"
        L".container{max-width:800px;width:100%%;}"
        L".editor-wrap{background:#fff;border:2px solid #d4c9b8;border-radius:4px;padding:16px;box-shadow:0 2px 8px rgba(0,0,0,0.08);}"
        L"#editor{width:100%%;min-height:300px;border:none;outline:none;resize:none;font-family:'QR Font 1L',monospace;font-size:48px;line-height:2;background:transparent;color:#000;letter-spacing:0;white-space:pre;overflow:auto;}"
        L"#editor:focus{outline:none;}"
        L".status{font-size:11px;color:#999;text-align:center;margin-top:12px;font-family:sans-serif;}"
        L".title{text-align:center;font-size:14px;color:#666;margin-bottom:8px;font-family:sans-serif;text-transform:uppercase;letter-spacing:2px;}"
        L"</style></head><body>"
        L"<div class='container'>"
        L"<div class='title'>MagicodesPad</div>"
        L"<div class='editor-wrap'>"
        L"<textarea id='editor' spellcheck='false'>%ls</textarea>"
        L"</div>"
        L"<div class='status'><span id='statusBar'>QR Preview: %ls</span></div>"
        L"</div>"
        L"<script>"
        L"var ed=document.getElementById('editor');"
        L"var sb=document.getElementById('statusBar');"
        L"ed.addEventListener('input',function(){"
        L"  window.chrome.webview.postMessage(ed.value);"
        L"});"
        L"function setQRMode(on){"
        L"  if(on){ed.style.fontFamily=\"'QR Font 1L',monospace\";sb.textContent='QR Preview: ON';}"
        L"  else{ed.style.fontFamily=\"Consolas,'Courier New',monospace\";sb.textContent='QR Preview: OFF';}"
        L"}"
        L"window.chrome.webview.postMessage('ready');"
        L"</script></body></html>",
        escaped, qrPreview ? L"ON" : L"OFF");

    free(escaped);
}

static void UpdateWebViewContent(void) {
    if (!g_webview || !g_currentText) return;
    wchar_t *html = malloc(sizeof(wchar_t) * MAX_HTML_SIZE);
    if (!html) return;
    GenerateHTML(g_currentText, html, MAX_HTML_SIZE, g_qrPreview);
    g_webview->lpVtbl->NavigateToString(g_webview, html);
    free(html);
}

/* ---- File I/O ---- */

static BOOL ReadFileToText(const wchar_t *path) {
    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;
    DWORD size = GetFileSize(hFile, NULL);
    if (size == INVALID_FILE_SIZE || size == 0) { CloseHandle(hFile); return FALSE; }
    char *buf = malloc(size + 1);
    if (!buf) { CloseHandle(hFile); return FALSE; }
    DWORD read;
    if (!ReadFile(hFile, buf, size, &read, NULL)) {
        free(buf); CloseHandle(hFile); return FALSE;
    }
    CloseHandle(hFile);
    buf[size] = 0;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, buf, size, NULL, 0);
    if (wlen <= 0) { wlen = MultiByteToWideChar(CP_ACP, 0, buf, size, NULL, 0); }
    if (wlen <= 0) { free(buf); return FALSE; }
    if (g_currentText) free(g_currentText);
    g_currentText = malloc(sizeof(wchar_t) * (wlen + 1));
    if (!g_currentText) { free(buf); return FALSE; }
    MultiByteToWideChar(CP_UTF8, 0, buf, size, g_currentText, wlen);
    g_currentText[wlen] = L'\0';
    free(buf);
    g_textChanged = 0;
    return TRUE;
}

static BOOL WriteTextToFile(const wchar_t *path, const wchar_t *text) {
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (utf8Len <= 0) return FALSE;
    char *utf8 = malloc(utf8Len);
    if (!utf8) return FALSE;
    WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, utf8Len, NULL, NULL);
    int textLen = utf8Len - 1;

    int crlfCount = 0;
    for (int i = 0; i < textLen; i++) {
        if (utf8[i] == '\n' && (i == 0 || utf8[i - 1] != '\r')) crlfCount++;
    }

    int outLen = textLen + crlfCount;
    char *out = malloc(outLen);
    if (!out) { free(utf8); return FALSE; }
    int j = 0;
    for (int i = 0; i < textLen; i++) {
        if (utf8[i] == '\n' && (i == 0 || utf8[i - 1] != '\r')) {
            out[j++] = '\r';
        }
        out[j++] = utf8[i];
    }

    HANDLE hFile = CreateFileW(path, GENERIC_WRITE, 0, NULL,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) { free(utf8); free(out); return FALSE; }
    DWORD written;
    BOOL ok = WriteFile(hFile, out, outLen, &written, NULL);
    CloseHandle(hFile);
    free(utf8);
    free(out);
    if (ok) { g_textChanged = 0; }
    return ok;
}

static void OpenTextFile(const wchar_t *path) {
    if (!ReadFileToText(path)) return;
    if (g_filePath) free(g_filePath);
    g_filePath = _wcsdup(path);
    wchar_t title[512];
    const wchar_t *name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    swprintf(title, 512, L"%ls - TinyMagicoditorWeb", name);
    SetWindowTextW(g_hwnd, title);
    if (g_webviewReady) {
        UpdateWebViewContent();
    }
}

static BOOL SaveCurrentFile(void) {
    if (!g_currentText) return FALSE;
    if (!g_filePath) {
        wchar_t path[MAX_PATH] = {0};
        OPENFILENAMEW ofn = {sizeof(ofn)};
        ofn.hwndOwner = g_hwnd;
        ofn.lpstrFilter = L"Magicodes Text (*.mgc;*.txt)\0*.mgc;*.txt\0All Files (*.*)\0*.*\0";
        ofn.lpstrDefExt = L"mgc";
        ofn.lpstrFile = path;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_OVERWRITEPROMPT;
        if (!GetSaveFileNameW(&ofn)) return FALSE;
        g_filePath = _wcsdup(path);
    }
    return WriteTextToFile(g_filePath, g_currentText);
}

/* ---- Window ---- */

static void ResizeWebView(void) {
    if (g_controller) {
        RECT r;
        GetClientRect(g_hwnd, &r);
        g_controller->lpVtbl->put_Bounds(g_controller, r);
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hwnd = hwnd;

            HRESULT hr = CreateCoreWebView2Environment(&envHandler);
            if (FAILED(hr)) {
                MessageBoxW(hwnd, L"WebView2 runtime not available.\nPlease install Microsoft Edge WebView2 Runtime.",
                           APP_TITLE, MB_ICONERROR);
                return -1;
            }

            MSG msgLoop;
            while (!g_env) {
                while (PeekMessageW(&msgLoop, NULL, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&msgLoop);
                    DispatchMessageW(&msgLoop);
                }
                Sleep(10);
            }

            hr = g_env->lpVtbl->CreateCoreWebView2Controller(g_env, hwnd, &ctlrHandler);
            if (FAILED(hr)) return -1;

            while (!g_newController) {
                while (PeekMessageW(&msgLoop, NULL, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&msgLoop);
                    DispatchMessageW(&msgLoop);
                }
                Sleep(10);
            }

            g_controller = g_newController;

            g_controller->lpVtbl->get_CoreWebView2(g_controller, &g_webview);
            g_webview->lpVtbl->AddRef(g_webview);

            g_controller->lpVtbl->put_IsVisible(g_controller, TRUE);

            ICoreWebView2Settings *settings = NULL;
            g_webview->lpVtbl->get_Settings(g_webview, &settings);
            if (settings) {
                settings->lpVtbl->put_AreDevToolsEnabled(settings, FALSE);
                settings->lpVtbl->put_AreDefaultContextMenusEnabled(settings, FALSE);
                settings->lpVtbl->put_IsStatusBarEnabled(settings, FALSE);
                settings->lpVtbl->Release(settings);
            }

            g_webview->lpVtbl->add_WebMessageReceived(g_webview, &msgHandler, NULL);

            ResizeWebView();

            g_webviewReady = 1;

            if (g_currentText) {
                UpdateWebViewContent();
            }

            break;
        }

        case WM_SIZE:
            ResizeWebView();
            break;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDM_OPEN: {
                    wchar_t path[MAX_PATH] = {0};
                    OPENFILENAMEW ofn = {sizeof(ofn)};
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = L"Magicodes Text (*.mgc;*.txt)\0*.mgc;*.txt\0All Files (*.*)\0*.*\0";
                    ofn.lpstrFile = path;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_FILEMUSTEXIST;
                    if (GetOpenFileNameW(&ofn)) {
                        OpenTextFile(path);
                    }
                    break;
                }
                case IDM_SAVE:
                    SaveCurrentFile();
                    break;
                case IDM_SAVEAS: {
                    wchar_t path[MAX_PATH] = {0};
                    OPENFILENAMEW ofn = {sizeof(ofn)};
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = L"Magicodes Text (*.mgc;*.txt)\0*.mgc;*.txt\0All Files (*.*)\0*.*\0";
                    ofn.lpstrDefExt = L"mgc";
                    ofn.lpstrFile = path;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_OVERWRITEPROMPT;
                    if (GetSaveFileNameW(&ofn)) {
                        if (g_filePath) free(g_filePath);
                        g_filePath = _wcsdup(path);
                        SaveCurrentFile();
                    }
                    break;
                }
                case IDM_EXIT:
                    if (g_textChanged) {
                        int r = MessageBoxW(hwnd, L"Save changes?", APP_TITLE,
                                            MB_YESNOCANCEL | MB_ICONQUESTION);
                        if (r == IDCANCEL) break;
                        if (r == IDYES) SaveCurrentFile();
                    }
                    PostQuitMessage(0);
                    break;
                case IDM_QRPREVIEW:
                    g_qrPreview = !g_qrPreview;
                    CheckMenuItem(GetMenu(hwnd), IDM_QRPREVIEW,
                        MF_BYCOMMAND | (g_qrPreview ? MF_CHECKED : MF_UNCHECKED));
                    UpdateQRPreview();
                    break;
                case IDM_ABOUT:
                    MessageBoxW(hwnd,
                        L"TinyMagicoditorWeb v0.2\n\n"
                        L"QR Font 1L Magicode preview via WebView2.\n"
                        L"Plain text editing - no HTML/RTF/image in output.\n\n"
                        L"See: https://github.com/CozmoCyke/TinyRetroPad",
                        L"About TinyMagicoditorWeb", MB_OK);
                    break;
            }
            break;

        case WM_KEYDOWN:
            if (wParam == VK_F2) {
                g_qrPreview = !g_qrPreview;
                CheckMenuItem(GetMenu(hwnd), IDM_QRPREVIEW,
                    MF_BYCOMMAND | (g_qrPreview ? MF_CHECKED : MF_UNCHECKED));
                UpdateQRPreview();
            }
            break;

        case WM_CLOSE:
            if (g_textChanged) {
                int r = MessageBoxW(hwnd, L"Save changes?", APP_TITLE,
                                    MB_YESNOCANCEL | MB_ICONQUESTION);
                if (r == IDCANCEL) return 0;
                if (r == IDYES) SaveCurrentFile();
            }
            DestroyWindow(hwnd);
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

/* ---- Entry ---- */

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        MessageBoxW(NULL, L"COM initialization failed.", APP_TITLE, MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, APP_TITLE,
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        900, 700, NULL, NULL, hInstance, NULL);
    if (!hwnd) {
        CoUninitialize();
        return 1;
    }

    HMENU hMenu = CreateMenu();
    HMENU hFile = CreateMenu();
    AppendMenuW(hFile, MF_STRING, IDM_OPEN, L"&Open\tCtrl+O");
    AppendMenuW(hFile, MF_STRING, IDM_SAVE, L"&Save\tCtrl+S");
    AppendMenuW(hFile, MF_STRING, IDM_SAVEAS, L"Save &As...");
    AppendMenuW(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hFile, MF_STRING, IDM_EXIT, L"E&xit");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFile, L"&File");

    HMENU hView = CreateMenu();
    AppendMenuW(hView, MF_STRING | MF_CHECKED, IDM_QRPREVIEW, L"&QR Preview\tF2");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hView, L"&View");

    HMENU hHelp = CreateMenu();
    AppendMenuW(hHelp, MF_STRING, IDM_ABOUT, L"&About");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hHelp, L"&Help");

    SetMenu(hwnd, hMenu);

    if (lpCmdLine && lpCmdLine[0]) {
        OpenTextFile(lpCmdLine);
    } else {
        g_currentText = _wcsdup(L"Type your Magicode here...");
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_webview) g_webview->lpVtbl->Release(g_webview);
    if (g_controller) g_controller->lpVtbl->Release(g_controller);
    if (g_env) g_env->lpVtbl->Release(g_env);
    if (g_currentText) free(g_currentText);
    if (g_filePath) free(g_filePath);

    CoUninitialize();
    return (int)msg.wParam;
}
