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

#define IDM_NEW      1000
#define IDM_OPEN    1001
#define IDM_SAVE    1002
#define IDM_SAVEAS  1003
#define IDM_EXIT    1004
#define IDM_ABOUT   1005
#define IDM_TEXT_MODE  1006
#define IDM_QR_MODE    1007
#define IDM_VIS_MODE   1008
#define IDM_FONT_AUTO  1009
#define IDM_FONT_1L    1010
#define IDM_FONT_2L    1011
#define IDM_FONT_3L    1012

#define MODE_TEXT 0
#define MODE_QR   1
#define MODE_VIS  2

#define FONT_AUTO 0
#define FONT_1L   1
#define FONT_2L   2
#define FONT_3L   3

#define MAX_TEXT_SIZE (1024 * 1024)
#define MAX_HTML_SIZE (MAX_TEXT_SIZE * 8 + 8192)

static HWND g_hwnd = NULL;
static ICoreWebView2Controller *g_controller = NULL;
static ICoreWebView2 *g_webview = NULL;
static ICoreWebView2Environment *g_env = NULL;
static wchar_t *g_currentText = NULL;
static wchar_t *g_filePath = NULL;
static int g_textChanged = 0;
static int g_webviewReady = 0;
static int g_mode = MODE_TEXT;
static int g_qrFont = FONT_AUTO;

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

static void UpdateViewMode(void) {
    if (!g_webview) return;
    wchar_t script[128];
    swprintf(script, 128, L"setMode(%d,%d);", g_mode, g_qrFont);
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

static void GenerateHTML(const wchar_t *text, wchar_t *html, size_t htmlSize, int mode) {
    wchar_t *escaped = malloc(sizeof(wchar_t) * (wcslen(text) * 6 + 1));
    if (!escaped) return;
    EscapeHTML(text, escaped, wcslen(text) * 6 + 1);

    swprintf(html, htmlSize,
        L"<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        L"<style>"
        L"*{margin:0;padding:0;box-sizing:border-box;}"
        L"body{background:#f5f0e8;display:flex;justify-content:center;padding:32px;font-family:sans-serif;}"
        L".container{max-width:900px;width:100%%;}"
        L".editor-wrap{background:#fff;border:2px solid #d4c9b8;border-radius:4px;padding:16px;box-shadow:0 2px 8px rgba(0,0,0,0.08);}"
        L"#editor,#formatted{width:100%%;border:none;outline:none;resize:none;font-family:Consolas,'Courier New',monospace;font-size:16px;line-height:1.6;background:#fafaf8;color:#222;white-space:pre-wrap;word-wrap:break-word;overflow:auto;padding:8px;min-height:400px;}"
        L"#editor:focus,#formatted:focus{outline:none;background:#fff;}"
        L".hidden{display:none!important;}"
        L".status{font-size:11px;color:#999;text-align:center;margin-top:12px;font-family:sans-serif;}"
        L".title{text-align:center;font-size:14px;color:#666;margin-bottom:8px;font-family:sans-serif;text-transform:uppercase;letter-spacing:2px;}"
        L"</style></head><body>"
        L"<div class='container'>"
        L"<div class='title'>MagicodesPad</div>"
        L"<div class='editor-wrap'>"
        L"<textarea id='editor' spellcheck='false'>%ls</textarea>"
        L"<div id='formatted' class='hidden' spellcheck='false'></div>"
        L"</div>"
        L"<div class='status'><span id='statusBar'>Loading...</span></div>"
        L"</div>"
        L"<script>"
        L"var ta=document.getElementById('editor');"
        L"var fv=document.getElementById('formatted');"
        L"var sb=document.getElementById('statusBar');"
        L"var isReRender=0, rawText='', currentFont=0;"
        L"var SHORT=17, MEDIUM=32, LIMITS=[53,17,32,53], FNAMES=[null,'QR Font 1L','QR Font 2L','QR Font 3L'];"
        L"var fonts=[null,\"'QR Font 1L',monospace\",\"'QR Font 2L',monospace\",\"'QR Font 3L',monospace\"];"
        L"function esc(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');}"
        L"function chooseFont(p){var b=new TextEncoder().encode(p).length;if(b>53)return 0;return b<=SHORT?1:(b<=MEDIUM?2:3);}"
        L"function getRaw(el){return el.textContent;}"
        L"function getOff(){var s=window.getSelection();if(!s.rangeCount)return 0;var r=s.getRangeAt(0),off=0,node=r.startContainer,nOff=r.startOffset;var w=document.createTreeWalker(fv,NodeFilter.SHOW_TEXT,null,false);while(w.nextNode()){if(w.currentNode===node){off+=nOff;break;}off+=w.currentNode.textContent.length;}return off;}"
        L"function setOff(off){var w=document.createTreeWalker(fv,NodeFilter.SHOW_TEXT,null,false);var pos=0,node=null,nOff=0;while(w.nextNode()){var len=w.currentNode.textContent.length;if(pos+len>=off){node=w.currentNode;nOff=off-pos;break;}pos+=len;}if(node){var s=window.getSelection(),r=document.createRange();r.setStart(node,Math.min(nOff,node.textContent.length));r.collapse(true);s.removeAllRanges();s.addRange(r);}}"
        L"function renderFormat(raw,fm){"
        L"var i=0,h='',bs,be,payload,fn,bytes;"
        L"while(i<raw.length){"
        L"bs=raw.indexOf('[',i);if(bs===-1){h+=esc(raw.substring(i));break;}"
        L"if(bs>i)h+=esc(raw.substring(i,bs));"
        L"be=raw.indexOf(']',bs);if(be===-1){h+=esc(raw.substring(bs));break;}"
        L"payload=raw.substring(bs+1,be);bytes=new TextEncoder().encode(payload).length;"
        L"fn=fm===0?chooseFont(payload):fm;"
        L"if(fn===0||bytes>LIMITS[fn]){"
        L"h+='<span style=\"background:#fff0f0;border:1px solid #f44;border-radius:3px;padding:1px 5px;font-family:Consolas,monospace;color:#c00;font-size:14px\" title=\"Needs QR Font 4L+: '+bytes+' bytes > '+LIMITS[fn]+' limit\">[TOO LONG: '+bytes+' bytes]</span>';"
        L"}else{"
        L"h+='<span style=\"font-family:'+fonts[fn]+';white-space:nowrap\" title=\"'+FNAMES[fn]+': '+bytes+' bytes\">['+esc(payload)+']</span>';"
        L"}"
        L"i=be+1;"
        L"}"
        L"return h;"
        L"}"
        L"function reRender(){if(!fv.classList.contains('hidden')){isReRender=1;var off=getOff();rawText=getRaw(fv);fv.innerHTML=renderFormat(rawText,currentFont);setOff(Math.min(off,rawText.length));isReRender=0;}}"
        L"fv.addEventListener('input',function(){if(isReRender)return;var v=getRaw(fv);window.chrome.webview.postMessage(v);reRender();});"
        L"function setMode(m,f){"
        L"currentFont=f;"
        L"if(getRaw(fv).length===0&&ta.value.length>0)fv.textContent=ta.value;"
        L"if(m===0){"
        L"ta.classList.remove('hidden');fv.classList.add('hidden');"
        L"if(getRaw(fv).length>0)ta.value=getRaw(fv);"
        L"sb.textContent='Mode: Text';"
        L"}else{"
        L"fv.classList.remove('hidden');ta.classList.add('hidden');"
        L"if(getRaw(fv).length===0)fv.textContent=ta.value;"
        L"fv.contentEditable=m===1?'true':'false';"
        L"if(m===1){fv.style.fontSize='48px';fv.style.lineHeight='2';fv.style.minHeight='300px';}"
        L"else{fv.style.fontSize='64px';fv.style.lineHeight='2';fv.style.minHeight='500px';}"
        L"rawText=getRaw(fv);fv.innerHTML=renderFormat(rawText,f);"
        L"var label=f===0?'Auto':'QR Font '+f+'L';"
        L"sb.textContent=(m===1?'Mode: QR Preview (':'Mode: Visualization (')+label+')';"
        L"}}"
        L"setMode(%d,%d);"
        L"window.chrome.webview.postMessage('ready');"
        L"</script></body></html>",
        escaped, mode, g_qrFont);

    free(escaped);
}

static void UpdateWebViewContent(void) {
    if (!g_webview || !g_currentText) return;
    wchar_t *html = malloc(sizeof(wchar_t) * MAX_HTML_SIZE);
    if (!html) return;
    GenerateHTML(g_currentText, html, MAX_HTML_SIZE, g_mode);
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
    g_mode = MODE_TEXT;
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
                case IDM_NEW:
                    if (g_textChanged) {
                        int r = MessageBoxW(hwnd, L"Save changes?", APP_TITLE,
                                            MB_YESNOCANCEL | MB_ICONQUESTION);
                        if (r == IDCANCEL) break;
                        if (r == IDYES) SaveCurrentFile();
                    }
                    if (g_currentText) free(g_currentText);
                    g_currentText = _wcsdup(L"");
                    if (g_filePath) { free(g_filePath); g_filePath = NULL; }
                    SetWindowTextW(hwnd, L"Untitled - MagicodesPad");
                    g_textChanged = 0;
                    g_mode = MODE_TEXT;
                    CheckMenuRadioItem(GetMenu(hwnd), IDM_TEXT_MODE, IDM_VIS_MODE,
                        IDM_TEXT_MODE, MF_BYCOMMAND);
                    if (g_webviewReady) UpdateWebViewContent();
                    break;
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
                case IDM_TEXT_MODE:
                    g_mode = MODE_TEXT;
                    CheckMenuRadioItem(GetMenu(hwnd), IDM_TEXT_MODE, IDM_VIS_MODE,
                        IDM_TEXT_MODE, MF_BYCOMMAND);
                    UpdateViewMode();
                    break;
                case IDM_QR_MODE:
                    g_mode = MODE_QR;
                    CheckMenuRadioItem(GetMenu(hwnd), IDM_TEXT_MODE, IDM_VIS_MODE,
                        IDM_QR_MODE, MF_BYCOMMAND);
                    UpdateViewMode();
                    break;
                case IDM_VIS_MODE:
                    g_mode = MODE_VIS;
                    CheckMenuRadioItem(GetMenu(hwnd), IDM_TEXT_MODE, IDM_VIS_MODE,
                        IDM_VIS_MODE, MF_BYCOMMAND);
                    UpdateViewMode();
                    break;
                case IDM_FONT_AUTO:
                    g_qrFont = FONT_AUTO;
                    CheckMenuRadioItem(GetMenu(hwnd), IDM_FONT_AUTO, IDM_FONT_3L,
                        IDM_FONT_AUTO, MF_BYCOMMAND);
                    if (g_mode != MODE_TEXT) UpdateViewMode();
                    break;
                case IDM_FONT_1L:
                    g_qrFont = FONT_1L;
                    CheckMenuRadioItem(GetMenu(hwnd), IDM_FONT_AUTO, IDM_FONT_3L,
                        IDM_FONT_1L, MF_BYCOMMAND);
                    if (g_mode != MODE_TEXT) UpdateViewMode();
                    break;
                case IDM_FONT_2L:
                    g_qrFont = FONT_2L;
                    CheckMenuRadioItem(GetMenu(hwnd), IDM_FONT_AUTO, IDM_FONT_3L,
                        IDM_FONT_2L, MF_BYCOMMAND);
                    if (g_mode != MODE_TEXT) UpdateViewMode();
                    break;
                case IDM_FONT_3L:
                    g_qrFont = FONT_3L;
                    CheckMenuRadioItem(GetMenu(hwnd), IDM_FONT_AUTO, IDM_FONT_3L,
                        IDM_FONT_3L, MF_BYCOMMAND);
                    if (g_mode != MODE_TEXT) UpdateViewMode();
                    break;
                case IDM_ABOUT:
                    MessageBoxW(hwnd,
                        L"TinyMagicoditorWeb v0.3\n\n"
                        L"QR Font 1L/2L/3L Magicode preview via WebView2.\n"
                        L"Plain text editing - no HTML/RTF/image in output.\n\n"
                        L"See: https://github.com/CozmoCyke/TinyRetroPad",
                        L"About TinyMagicoditorWeb", MB_OK);
                    break;
            }
            break;

        case WM_KEYDOWN:
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                switch (wParam) {
                    case 'N':
                        PostMessageW(hwnd, WM_COMMAND, IDM_NEW, 0);
                        break;
                    case 'O':
                        PostMessageW(hwnd, WM_COMMAND, IDM_OPEN, 0);
                        break;
                    case 'S':
                        PostMessageW(hwnd, WM_COMMAND, IDM_SAVE, 0);
                        break;
                }
            } else if (wParam == VK_F2) {
                g_mode = (g_mode + 1) % 3;
                CheckMenuRadioItem(GetMenu(hwnd), IDM_TEXT_MODE, IDM_VIS_MODE,
                    g_mode == MODE_TEXT ? IDM_TEXT_MODE :
                    g_mode == MODE_QR ? IDM_QR_MODE : IDM_VIS_MODE,
                    MF_BYCOMMAND);
                UpdateViewMode();
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
    AppendMenuW(hFile, MF_STRING, IDM_NEW, L"&New\tCtrl+N");
    AppendMenuW(hFile, MF_STRING, IDM_OPEN, L"&Open\tCtrl+O");
    AppendMenuW(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hFile, MF_STRING, IDM_SAVE, L"&Save\tCtrl+S");
    AppendMenuW(hFile, MF_STRING, IDM_SAVEAS, L"Save &As...");
    AppendMenuW(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hFile, MF_STRING, IDM_EXIT, L"E&xit");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFile, L"&File");

    HMENU hView = CreateMenu();
    AppendMenuW(hView, MF_STRING, IDM_TEXT_MODE, L"&Text Mode\tF2");
    AppendMenuW(hView, MF_STRING, IDM_QR_MODE, L"&QR Mode\tF2");
    AppendMenuW(hView, MF_STRING, IDM_VIS_MODE, L"&Visualization Mode\tF2");
    AppendMenuW(hView, MF_SEPARATOR, 0, NULL);
    HMENU hQRFont = CreateMenu();
    AppendMenuW(hQRFont, MF_STRING | MF_CHECKED, IDM_FONT_AUTO, L"&Auto");
    AppendMenuW(hQRFont, MF_STRING, IDM_FONT_1L, L"QR Font &1L");
    AppendMenuW(hQRFont, MF_STRING, IDM_FONT_2L, L"QR Font &2L");
    AppendMenuW(hQRFont, MF_STRING, IDM_FONT_3L, L"QR Font &3L");
    AppendMenuW(hView, MF_POPUP, (UINT_PTR)hQRFont, L"QR &Font");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hView, L"&View");

    HMENU hHelp = CreateMenu();
    AppendMenuW(hHelp, MF_STRING, IDM_ABOUT, L"&About");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hHelp, L"&Help");

    SetMenu(hwnd, hMenu);
    CheckMenuRadioItem(GetMenu(hwnd), IDM_TEXT_MODE, IDM_VIS_MODE,
        IDM_TEXT_MODE, MF_BYCOMMAND);
    CheckMenuRadioItem(GetMenu(hwnd), IDM_FONT_AUTO, IDM_FONT_3L,
        IDM_FONT_AUTO, MF_BYCOMMAND);

    if (lpCmdLine && lpCmdLine[0]) {
        OpenTextFile(lpCmdLine);
    } else {
        g_currentText = _wcsdup(L"");
        SetWindowTextW(hwnd, L"Untitled - MagicodesPad");
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
