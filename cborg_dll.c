#include "editor_frame_api.h"
#include "Scintilla.h"
#include "SciLexer.h"
#include "Lexilla.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IDC_FRAME_SCI_CHILD 5001

typedef struct {
    HWND              hFrame;
    HWND              hSci;
    HMENU             hAttachedMenu;
    HMODULE           hSciDll;
    HMODULE           hLexillaDll;
    UINT              frameId;
    EDITOR_THEME_MODE theme;
    EDITOR_LANG_MODE  lang;
    BOOL              wordWrap;
    BOOL              showWhitespace;
    BOOL              showEol;
    BOOL              showIndentGuides;
    BOOL              modified;
    int               framePad;
} EDITOR_FRAME_CTX;

static HINSTANCE g_hDllInst = NULL;

static EDITOR_FRAME_CTX *GetFrameCtx(HWND hFrame) {
    if (!hFrame || !IsWindow(hFrame)) return NULL;
    return (EDITOR_FRAME_CTX *)(LONG_PTR)GetWindowLongPtrA(hFrame, GWLP_USERDATA);
}

static void ResolveModuleSibling(HMODULE hMod, const char *fileName, char *outPath, DWORD cchOut) {
    char modPath[MAX_PATH];
    DWORD len = GetModuleFileNameA(hMod, modPath, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        char *slash = modPath + len;
        while (slash > modPath && *slash != '\\' && *slash != '/') {
            --slash;
        }
        if (slash > modPath) {
            *slash = '\0';
            if ((size_t)((slash - modPath) + 1 + lstrlenA(fileName) + 1) <= (size_t)cchOut) {
                lstrcpyA(outPath, modPath);
                lstrcatA(outPath, "\\");
                lstrcatA(outPath, fileName);
                return;
            }
        }
    }
    lstrcpynA(outPath, fileName, (int)cchOut);
}

static void SyncMenuChecks(EDITOR_FRAME_CTX *ctx) {
    HMENU m = ctx ? ctx->hAttachedMenu : NULL;
    if (!m || !IsMenu(m)) return;

    CheckMenuItem(m, CMD_EDITOR_THEME_DARK,  MF_BYCOMMAND | (ctx->theme == EDITOR_THEME_OBSIDIAN ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, CMD_EDITOR_THEME_LIGHT, MF_BYCOMMAND | (ctx->theme == EDITOR_THEME_LIGHT    ? MF_CHECKED : MF_UNCHECKED));

    CheckMenuItem(m, CMD_EDITOR_WRAP_TOGGLE,   MF_BYCOMMAND | (ctx->wordWrap         ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, CMD_EDITOR_WS_TOGGLE,     MF_BYCOMMAND | (ctx->showWhitespace   ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, CMD_EDITOR_EOL_TOGGLE,    MF_BYCOMMAND | (ctx->showEol          ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, CMD_EDITOR_GUIDES_TOGGLE, MF_BYCOMMAND | (ctx->showIndentGuides ? MF_CHECKED : MF_UNCHECKED));

    CheckMenuItem(m, CMD_EDITOR_LANG_C,      MF_BYCOMMAND | (ctx->lang == EDITOR_LANG_C      ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, CMD_EDITOR_LANG_PYTHON, MF_BYCOMMAND | (ctx->lang == EDITOR_LANG_PYTHON ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, CMD_EDITOR_LANG_JSON,   MF_BYCOMMAND | (ctx->lang == EDITOR_LANG_JSON   ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, CMD_EDITOR_LANG_BATCH,  MF_BYCOMMAND | (ctx->lang == EDITOR_LANG_BATCH  ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, CMD_EDITOR_LANG_PLAIN,  MF_BYCOMMAND | (ctx->lang == EDITOR_LANG_PLAIN  ? MF_CHECKED : MF_UNCHECKED));
}

static void NotifyFormState(EDITOR_FRAME_CTX *ctx) {
    HWND hParent;
    NM_EDITOR_FRAME_STATE nm;

    if (!ctx || !ctx->hFrame || !IsWindow(ctx->hFrame)) return;
    hParent = GetParent(ctx->hFrame);
    if (!hParent || !IsWindow(hParent)) return;

    ZeroMemory(&nm, sizeof(nm));
    nm.hdr.hwndFrom = ctx->hFrame;
    nm.hdr.idFrom   = ctx->frameId;
    nm.hdr.code     = NM_EDITOR_STATE_CHANGED;
    nm.modified     = ctx->modified;
    nm.themeMode    = (int)ctx->theme;
    nm.langMode     = (int)ctx->lang;
    EditorFrame_GetCaretStats(ctx->hFrame, &nm.line, &nm.col, &nm.lineCount, &nm.charCount);

    SendMessageA(hParent, WM_NOTIFY, (WPARAM)ctx->frameId, (LPARAM)&nm);
}

static void ApplyThemeInternal(EDITOR_FRAME_CTX *ctx) {
    COLORREF bg, fg, marginBg, marginFg, caretCol, selBg;
    if (!ctx || !ctx->hSci) return;

    if (ctx->theme == EDITOR_THEME_LIGHT) {
        bg       = RGB(250, 251, 253);
        fg       = RGB(24, 28, 36);
        marginBg = RGB(236, 240, 246);
        marginFg = RGB(110, 120, 136);
        caretCol = RGB(0, 102, 204);
        selBg    = RGB(198, 224, 252);
    } else {
        bg       = RGB(10, 14, 22);
        fg       = RGB(224, 234, 246);
        marginBg = RGB(16, 22, 34);
        marginFg = RGB(106, 130, 160);
        caretCol = RGB(0, 240, 255);
        selBg    = RGB(32, 64, 96);
    }

    SendMessageA(ctx->hSci, SCI_STYLESETFONT, STYLE_DEFAULT, (LPARAM)"JetBrains Mono");
    SendMessageA(ctx->hSci, SCI_STYLESETSIZE, STYLE_DEFAULT, 11);
    SendMessageA(ctx->hSci, SCI_STYLESETBACK, STYLE_DEFAULT, (LPARAM)bg);
    SendMessageA(ctx->hSci, SCI_STYLESETFORE, STYLE_DEFAULT, (LPARAM)fg);
    SendMessageA(ctx->hSci, SCI_STYLECLEARALL, 0, 0);

    SendMessageA(ctx->hSci, SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
    SendMessageA(ctx->hSci, SCI_SETMARGINWIDTHN, 0, 52);
    SendMessageA(ctx->hSci, SCI_STYLESETBACK, STYLE_LINENUMBER, (LPARAM)marginBg);
    SendMessageA(ctx->hSci, SCI_STYLESETFORE, STYLE_LINENUMBER, (LPARAM)marginFg);

    SendMessageA(ctx->hSci, SCI_SETCARETFORE, (WPARAM)caretCol, 0);
    SendMessageA(ctx->hSci, SCI_SETSELBACK, 1, (LPARAM)selBg);
    SendMessageA(ctx->hSci, SCI_SETINDENTATIONGUIDES, ctx->showIndentGuides ? SC_IV_REAL : SC_IV_NONE, 0);

    if (ctx->hFrame && IsWindow(ctx->hFrame)) {
        InvalidateRect(ctx->hFrame, NULL, TRUE);
    }
}

static void ApplyLexerInternal(EDITOR_FRAME_CTX *ctx) {
    CreateLexerFn pCreateLexer;
    const char *lexerName = NULL;
    const char *kw0 = "";

    if (!ctx || !ctx->hSci) return;

    ApplyThemeInternal(ctx);

    switch (ctx->lang) {
        case EDITOR_LANG_PYTHON:
            lexerName = "python";
            kw0 = "and as assert async await break class continue def del elif else except finally for from global if import in is lambda nonlocal not or pass raise return try while with yield";
            break;
        case EDITOR_LANG_JSON:
            lexerName = "json";
            kw0 = "true false null";
            break;
        case EDITOR_LANG_BATCH:
            lexerName = "batch";
            kw0 = "echo set if else for in do goto call exit rem pause setlocal endlocal";
            break;
        case EDITOR_LANG_PLAIN:
            lexerName = NULL;
            break;
        case EDITOR_LANG_C:
        default:
            lexerName = "cpp";
            kw0 = "auto break case char const continue default do double else enum extern float for goto if inline int long register restrict return short signed sizeof static struct switch typedef union unsigned void volatile while BOOL HWND HMODULE LRESULT WPARAM LPARAM";
            break;
    }

    if (lexerName && ctx->hLexillaDll) {
        pCreateLexer = (CreateLexerFn)(void *)GetProcAddress(ctx->hLexillaDll, "CreateLexer");
        if (pCreateLexer) {
            void *lexer = pCreateLexer(lexerName);
            if (lexer) {
                SendMessageA(ctx->hSci, SCI_SETILEXER, 0, (LPARAM)lexer);
                SendMessageA(ctx->hSci, SCI_SETKEYWORDS, 0, (LPARAM)kw0);
                if (ctx->theme == EDITOR_THEME_OBSIDIAN) {
                    SendMessageA(ctx->hSci, SCI_STYLESETFORE, SCE_C_WORD, RGB(0, 240, 255));
                    SendMessageA(ctx->hSci, SCI_STYLESETFORE, SCE_C_STRING, RGB(255, 196, 86));
                    SendMessageA(ctx->hSci, SCI_STYLESETFORE, SCE_C_COMMENT, RGB(92, 128, 150));
                    SendMessageA(ctx->hSci, SCI_STYLESETFORE, SCE_C_COMMENTLINE, RGB(92, 128, 150));
                    SendMessageA(ctx->hSci, SCI_STYLESETFORE, SCE_C_NUMBER, RGB(184, 128, 255));
                } else {
                    SendMessageA(ctx->hSci, SCI_STYLESETFORE, SCE_C_WORD, RGB(0, 64, 180));
                    SendMessageA(ctx->hSci, SCI_STYLESETFORE, SCE_C_STRING, RGB(163, 21, 21));
                    SendMessageA(ctx->hSci, SCI_STYLESETFORE, SCE_C_COMMENT, RGB(0, 128, 0));
                    SendMessageA(ctx->hSci, SCI_STYLESETFORE, SCE_C_COMMENTLINE, RGB(0, 128, 0));
                    SendMessageA(ctx->hSci, SCI_STYLESETFORE, SCE_C_NUMBER, RGB(128, 0, 128));
                }
            }
        }
    } else {
        SendMessageA(ctx->hSci, SCI_SETILEXER, 0, 0);
    }
}

static void LayoutFrameInner(EDITOR_FRAME_CTX *ctx, int w, int h) {
    int pad;
    int innerW;
    int innerH;

    if (!ctx || !ctx->hSci) return;
    pad = ctx->framePad;
    innerW = w - (pad * 2);
    innerH = h - (pad * 2);
    if (innerW < 10) innerW = 10;
    if (innerH < 10) innerH = 10;

    MoveWindow(ctx->hSci, pad, pad, innerW, innerH, TRUE);
}

static void PaintInsertableFrame(EDITOR_FRAME_CTX *ctx, HDC hdc) {
    RECT rc;
    COLORREF frameBg;
    COLORREF borderCol;
    HBRUSH hBrush;
    HPEN hPen;
    HGDIOBJ oldBrush, oldPen;

    if (!ctx || !ctx->hFrame) return;
    GetClientRect(ctx->hFrame, &rc);

    if (ctx->theme == EDITOR_THEME_LIGHT) {
        frameBg   = RGB(224, 230, 238);
        borderCol = RGB(120, 140, 168);
    } else {
        frameBg   = RGB(14, 20, 32);
        borderCol = RGB(0, 240, 255);
    }

    hBrush = CreateSolidBrush(frameBg);
    hPen   = CreatePen(PS_SOLID, 1, borderCol);
    oldBrush = SelectObject(hdc, hBrush);
    oldPen   = SelectObject(hdc, hPen);

    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);

    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(hPen);
    DeleteObject(hBrush);
}

static void CleanupFrameCtx(EDITOR_FRAME_CTX *ctx) {
    if (!ctx) return;
    if (ctx->hSci && IsWindow(ctx->hSci)) {
        DestroyWindow(ctx->hSci);
        ctx->hSci = NULL;
    }
    if (ctx->hLexillaDll) {
        FreeLibrary(ctx->hLexillaDll);
        ctx->hLexillaDll = NULL;
    }
    if (ctx->hSciDll) {
        FreeLibrary(ctx->hSciDll);
        ctx->hSciDll = NULL;
    }
    free(ctx);
}

static LRESULT CALLBACK EditorFrame_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    EDITOR_FRAME_CTX *ctx = GetFrameCtx(hwnd);

    switch (msg) {
        case WM_NCCREATE: {
            CREATESTRUCTA *cs = (CREATESTRUCTA *)lParam;
            ctx = (EDITOR_FRAME_CTX *)calloc(1, sizeof(EDITOR_FRAME_CTX));
            if (!ctx) return FALSE;
            ctx->hFrame           = hwnd;
            ctx->frameId          = (UINT)(UINT_PTR)cs->hMenu;
            ctx->theme            = EDITOR_THEME_OBSIDIAN;
            ctx->lang             = EDITOR_LANG_C;
            ctx->showIndentGuides = TRUE;
            ctx->framePad         = 2;
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)ctx);
            return TRUE;
        }

        case WM_CREATE: {
            char sciPath[MAX_PATH];
            char lexPath[MAX_PATH];
            RECT rc;
            CREATESTRUCTA *cs = (CREATESTRUCTA *)lParam;

            ResolveModuleSibling(g_hDllInst, "Scintilla.dll", sciPath, MAX_PATH);
            ctx->hSciDll = LoadLibraryA(sciPath);
            if (!ctx->hSciDll) ctx->hSciDll = LoadLibraryA("Scintilla.dll");
            if (!ctx->hSciDll) {
                CleanupFrameCtx(ctx);
                SetWindowLongPtrA(hwnd, GWLP_USERDATA, 0);
                return -1;
            }

            ResolveModuleSibling(g_hDllInst, "Lexilla.dll", lexPath, MAX_PATH);
            ctx->hLexillaDll = LoadLibraryA(lexPath);
            if (!ctx->hLexillaDll) ctx->hLexillaDll = LoadLibraryA("Lexilla.dll");

            GetClientRect(hwnd, &rc);
            ctx->hSci = CreateWindowExA(
                0,
                "Scintilla",
                "",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN,
                ctx->framePad,
                ctx->framePad,
                (rc.right - ctx->framePad * 2) > 10 ? (rc.right - ctx->framePad * 2) : 100,
                (rc.bottom - ctx->framePad * 2) > 10 ? (rc.bottom - ctx->framePad * 2) : 100,
                hwnd,
                (HMENU)(UINT_PTR)IDC_FRAME_SCI_CHILD,
                ctx->hSciDll,
                NULL
            );
            if (!ctx->hSci) {
                CleanupFrameCtx(ctx);
                SetWindowLongPtrA(hwnd, GWLP_USERDATA, 0);
                return -1;
            }

            ApplyLexerInternal(ctx);
            if (cs && cs->lpszName && cs->lpszName[0] != '\0') {
                SendMessageA(ctx->hSci, SCI_SETTEXT, 0, (LPARAM)cs->lpszName);
                SendMessageA(ctx->hSci, SCI_SETSAVEPOINT, 0, 0);
                ctx->modified = FALSE;
            } else {
                EditorFrame_InsertTemplate(hwnd, ctx->lang, ctx->theme);
            }
            SyncMenuChecks(ctx);
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            PaintInsertableFrame(ctx, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_SIZE:
            if (ctx) {
                LayoutFrameInner(ctx, LOWORD(lParam), HIWORD(lParam));
            }
            return 0;

        case WM_SETFOCUS:
            if (ctx && ctx->hSci) {
                SetFocus(ctx->hSci);
            }
            return 0;

        case WM_COMMAND:
            if (ctx) {
                return EditorFrame_SendCommand(hwnd, LOWORD(wParam)) ? 0 : 1;
            }
            break;

        case WM_NOTIFY: {
            const SCNotification *scn = (const SCNotification *)lParam;
            if (ctx && scn && scn->nmhdr.hwndFrom == ctx->hSci) {
                switch (scn->nmhdr.code) {
                    case SCN_MODIFIED:
                        if ((scn->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) != 0) {
                            ctx->modified = TRUE;
                            NotifyFormState(ctx);
                        }
                        break;
                    case SCN_SAVEPOINTLEFT:
                        ctx->modified = TRUE;
                        NotifyFormState(ctx);
                        break;
                    case SCN_SAVEPOINTREACHED:
                        ctx->modified = FALSE;
                        NotifyFormState(ctx);
                        break;
                    case SCN_UPDATEUI:
                        NotifyFormState(ctx);
                        break;
                }
            }
            return 0;
        }

        case WM_NCDESTROY:
            if (ctx) {
                CleanupFrameCtx(ctx);
                SetWindowLongPtrA(hwnd, GWLP_USERDATA, 0);
            }
            return 0;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

BOOL WINAPI EditorFrame_RegisterClass(HINSTANCE hInstance) {
    WNDCLASSEXA wc;
    HINSTANCE hInst = hInstance ? hInstance : g_hDllInst;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_GLOBALCLASS | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = EditorFrame_WndProc;
    wc.cbWndExtra    = sizeof(LONG_PTR);
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = EDITOR_FRAME_CLASS_NAME;

    if (!RegisterClassExA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return FALSE;
    }
    return TRUE;
}

void WINAPI EditorFrame_UnregisterClass(HINSTANCE hInstance) {
    UnregisterClassA(EDITOR_FRAME_CLASS_NAME, hInstance ? hInstance : g_hDllInst);
}

HWND WINAPI EditorFrame_Create(
    HWND        hParentForm,
    HINSTANCE   hInstance,
    UINT        frameId,
    int         x,
    int         y,
    int         width,
    int         height,
    const char *initialText
) {
    HINSTANCE hInst = hInstance ? hInstance : g_hDllInst;
    if (!hParentForm || !IsWindow(hParentForm)) return NULL;
    EditorFrame_RegisterClass(hInst);

    return CreateWindowExA(
        0,
        EDITOR_FRAME_CLASS_NAME,
        initialText ? initialText : "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN,
        x, y, width, height,
        hParentForm,
        (HMENU)(UINT_PTR)frameId,
        hInst,
        NULL
    );
}

void WINAPI EditorFrame_Destroy(HWND hFrame) {
    if (hFrame && IsWindow(hFrame)) {
        DestroyWindow(hFrame);
    }
}

HWND WINAPI EditorFrame_GetScintillaHwnd(HWND hFrame) {
    EDITOR_FRAME_CTX *ctx = GetFrameCtx(hFrame);
    return ctx ? ctx->hSci : NULL;
}

BOOL WINAPI EditorFrame_AttachMenu(HWND hFrame, HMENU hMenu) {
    EDITOR_FRAME_CTX *ctx = GetFrameCtx(hFrame);
    if (!ctx) return FALSE;
    ctx->hAttachedMenu = hMenu;
    SyncMenuChecks(ctx);
    return TRUE;
}

BOOL WINAPI EditorFrame_SendCommand(HWND hFrame, WORD cmdId) {
    EDITOR_FRAME_CTX *ctx = GetFrameCtx(hFrame);
    if (!ctx || !ctx->hSci) return FALSE;

    switch (cmdId) {
        case CMD_EDITOR_NEW:
            EditorFrame_InsertTemplate(hFrame, ctx->lang, ctx->theme);
            return TRUE;
        case CMD_EDITOR_UNDO:
            SendMessageA(ctx->hSci, SCI_UNDO, 0, 0);
            return TRUE;
        case CMD_EDITOR_REDO:
            SendMessageA(ctx->hSci, SCI_REDO, 0, 0);
            return TRUE;
        case CMD_EDITOR_CUT:
            SendMessageA(ctx->hSci, SCI_CUT, 0, 0);
            return TRUE;
        case CMD_EDITOR_COPY:
            SendMessageA(ctx->hSci, SCI_COPY, 0, 0);
            return TRUE;
        case CMD_EDITOR_PASTE:
            SendMessageA(ctx->hSci, SCI_PASTE, 0, 0);
            return TRUE;
        case CMD_EDITOR_SELECT_ALL:
            SendMessageA(ctx->hSci, SCI_SELECTALL, 0, 0);
            return TRUE;

        case CMD_EDITOR_THEME_DARK:
            EditorFrame_SetTheme(hFrame, EDITOR_THEME_OBSIDIAN);
            return TRUE;
        case CMD_EDITOR_THEME_LIGHT:
            EditorFrame_SetTheme(hFrame, EDITOR_THEME_LIGHT);
            return TRUE;
        case CMD_EDITOR_THEME_TOGGLE:
            EditorFrame_SetTheme(hFrame, (ctx->theme == EDITOR_THEME_OBSIDIAN) ? EDITOR_THEME_LIGHT : EDITOR_THEME_OBSIDIAN);
            return TRUE;

        case CMD_EDITOR_WRAP_TOGGLE:
            EditorFrame_SetViewFlags(hFrame, !ctx->wordWrap, ctx->showWhitespace, ctx->showEol, ctx->showIndentGuides);
            return TRUE;
        case CMD_EDITOR_WS_TOGGLE:
            EditorFrame_SetViewFlags(hFrame, ctx->wordWrap, !ctx->showWhitespace, ctx->showEol, ctx->showIndentGuides);
            return TRUE;
        case CMD_EDITOR_EOL_TOGGLE:
            EditorFrame_SetViewFlags(hFrame, ctx->wordWrap, ctx->showWhitespace, !ctx->showEol, ctx->showIndentGuides);
            return TRUE;
        case CMD_EDITOR_GUIDES_TOGGLE:
            EditorFrame_SetViewFlags(hFrame, ctx->wordWrap, ctx->showWhitespace, ctx->showEol, !ctx->showIndentGuides);
            return TRUE;

        case CMD_EDITOR_LANG_C:
            EditorFrame_SetLanguage(hFrame, EDITOR_LANG_C);
            return TRUE;
        case CMD_EDITOR_LANG_PYTHON:
            EditorFrame_SetLanguage(hFrame, EDITOR_LANG_PYTHON);
            return TRUE;
        case CMD_EDITOR_LANG_JSON:
            EditorFrame_SetLanguage(hFrame, EDITOR_LANG_JSON);
            return TRUE;
        case CMD_EDITOR_LANG_BATCH:
            EditorFrame_SetLanguage(hFrame, EDITOR_LANG_BATCH);
            return TRUE;
        case CMD_EDITOR_LANG_PLAIN:
            EditorFrame_SetLanguage(hFrame, EDITOR_LANG_PLAIN);
            return TRUE;
        case CMD_EDITOR_LANG_CYCLE:
            EditorFrame_SetLanguage(hFrame, (EDITOR_LANG_MODE)(((int)ctx->lang + 1) % 5));
            return TRUE;
    }

    return FALSE;
}

void WINAPI EditorFrame_SetTheme(HWND hFrame, EDITOR_THEME_MODE theme) {
    EDITOR_FRAME_CTX *ctx = GetFrameCtx(hFrame);
    if (!ctx) return;
    ctx->theme = theme;
    ApplyLexerInternal(ctx);
    SyncMenuChecks(ctx);
    NotifyFormState(ctx);
}

void WINAPI EditorFrame_SetLanguage(HWND hFrame, EDITOR_LANG_MODE lang) {
    EDITOR_FRAME_CTX *ctx = GetFrameCtx(hFrame);
    if (!ctx) return;
    ctx->lang = lang;
    ApplyLexerInternal(ctx);
    SyncMenuChecks(ctx);
    NotifyFormState(ctx);
}

void WINAPI EditorFrame_SetViewFlags(
    HWND hFrame,
    BOOL wordWrap,
    BOOL showWhitespace,
    BOOL showEol,
    BOOL showIndentGuides
) {
    EDITOR_FRAME_CTX *ctx = GetFrameCtx(hFrame);
    if (!ctx || !ctx->hSci) return;

    ctx->wordWrap         = wordWrap;
    ctx->showWhitespace   = showWhitespace;
    ctx->showEol          = showEol;
    ctx->showIndentGuides = showIndentGuides;

    SendMessageA(ctx->hSci, SCI_SETWRAPMODE, wordWrap ? SC_WRAP_WORD : SC_WRAP_NONE, 0);
    SendMessageA(ctx->hSci, SCI_SETVIEWWS, showWhitespace ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE, 0);
    SendMessageA(ctx->hSci, SCI_SETVIEWEOL, showEol ? 1 : 0, 0);
    SendMessageA(ctx->hSci, SCI_SETINDENTATIONGUIDES, showIndentGuides ? SC_IV_REAL : SC_IV_NONE, 0);

    SyncMenuChecks(ctx);
    NotifyFormState(ctx);
}

BOOL WINAPI EditorFrame_SetText(HWND hFrame, const char *utf8Text) {
    EDITOR_FRAME_CTX *ctx = GetFrameCtx(hFrame);
    if (!ctx || !ctx->hSci) return FALSE;
    SendMessageA(ctx->hSci, SCI_SETTEXT, 0, (LPARAM)(utf8Text ? utf8Text : ""));
    SendMessageA(ctx->hSci, SCI_SETSAVEPOINT, 0, 0);
    ctx->modified = FALSE;
    NotifyFormState(ctx);
    return TRUE;
}

char *WINAPI EditorFrame_GetTextAlloc(HWND hFrame, size_t *outLen) {
    EDITOR_FRAME_CTX *ctx = GetFrameCtx(hFrame);
    int len;
    char *buf;

    if (outLen) *outLen = 0;
    if (!ctx || !ctx->hSci) return NULL;

    len = (int)SendMessageA(ctx->hSci, SCI_GETTEXTLENGTH, 0, 0);
    if (len < 0) return NULL;

    buf = (char *)HeapAlloc(GetProcessHeap(), 0, (size_t)len + 1);
    if (!buf) return NULL;

    SendMessageA(ctx->hSci, SCI_GETTEXT, (WPARAM)(len + 1), (LPARAM)buf);
    buf[len] = '\0';
    if (outLen) *outLen = (size_t)len;
    return buf;
}

void WINAPI EditorFrame_FreeText(char *buf) {
    if (buf) {
        HeapFree(GetProcessHeap(), 0, buf);
    }
}

BOOL WINAPI EditorFrame_FindNext(HWND hFrame, const char *needle, int startPos) {
    EDITOR_FRAME_CTX *ctx = GetFrameCtx(hFrame);
    int docLen, matchStart, matchEnd;
    struct Sci_TextToFind ttf;

    if (!ctx || !ctx->hSci || !needle || needle[0] == '\0') return FALSE;

    docLen = (int)SendMessageA(ctx->hSci, SCI_GETTEXTLENGTH, 0, 0);
    if (docLen <= 0) return FALSE;
    if (startPos < 0) startPos = 0;
    if (startPos > docLen) startPos = docLen;

    ttf.chrg.cpMin = startPos;
    ttf.chrg.cpMax = docLen;
    ttf.lpstrText  = (char *)needle;
    matchStart = (int)SendMessageA(ctx->hSci, SCI_FINDTEXT, SCFIND_MATCHCASE, (LPARAM)&ttf);

    if (matchStart < 0 && startPos > 0) {
        ttf.chrg.cpMin = 0;
        ttf.chrg.cpMax = startPos;
        matchStart = (int)SendMessageA(ctx->hSci, SCI_FINDTEXT, SCFIND_MATCHCASE, (LPARAM)&ttf);
    }

    if (matchStart < 0) return FALSE;
    matchEnd = matchStart + lstrlenA(needle);
    SendMessageA(ctx->hSci, SCI_SETSEL, (WPARAM)matchStart, (LPARAM)matchEnd);
    SendMessageA(ctx->hSci, SCI_SCROLLCARET, 0, 0);
    return TRUE;
}

void WINAPI EditorFrame_GetCaretStats(
    HWND hFrame,
    int *outLine,
    int *outCol,
    int *outLineCount,
    int *outCharCount
) {
    EDITOR_FRAME_CTX *ctx = GetFrameCtx(hFrame);
    int pos = 0, line = 0, col = 0, lines = 1, chars = 0;

    if (ctx && ctx->hSci) {
        pos   = (int)SendMessageA(ctx->hSci, SCI_GETCURRENTPOS, 0, 0);
        line  = (int)SendMessageA(ctx->hSci, SCI_LINEFROMPOSITION, (WPARAM)pos, 0);
        col   = (int)SendMessageA(ctx->hSci, SCI_GETCOLUMN, (WPARAM)pos, 0);
        lines = (int)SendMessageA(ctx->hSci, SCI_GETLINECOUNT, 0, 0);
        chars = (int)SendMessageA(ctx->hSci, SCI_GETTEXTLENGTH, 0, 0);
    }

    if (outLine)      *outLine      = line + 1;
    if (outCol)       *outCol       = col + 1;
    if (outLineCount) *outLineCount = lines > 0 ? lines : 1;
    if (outCharCount) *outCharCount = chars > 0 ? chars : 0;
}

void WINAPI EditorFrame_SetSavePoint(HWND hFrame) {
    EDITOR_FRAME_CTX *ctx = GetFrameCtx(hFrame);
    if (!ctx || !ctx->hSci) return;
    SendMessageA(ctx->hSci, SCI_SETSAVEPOINT, 0, 0);
    ctx->modified = FALSE;
    NotifyFormState(ctx);
}

BOOL WINAPI EditorFrame_IsModified(HWND hFrame) {
    EDITOR_FRAME_CTX *ctx = GetFrameCtx(hFrame);
    return ctx ? ctx->modified : FALSE;
}

void WINAPI EditorFrame_InsertTemplate(HWND hFrame, EDITOR_LANG_MODE lang, EDITOR_THEME_MODE theme) {
    EDITOR_FRAME_CTX *ctx = GetFrameCtx(hFrame);
    const char *tpl = "// Frame ready\nint main(void) {\n    return 0;\n}\n";

    if (!ctx || !ctx->hSci) return;
    ctx->lang  = lang;
    ctx->theme = theme;
    ApplyLexerInternal(ctx);

    switch (lang) {
        case EDITOR_LANG_PYTHON:
            tpl = "# Frame ready\ndef main():\n    print(\"ready\")\n\nif __name__ == \"__main__\":\n    main()\n";
            break;
        case EDITOR_LANG_JSON:
            tpl = "{\n  \"status\": \"ready\",\n  \"frame\": \"MercwarEditorFrame\"\n}\n";
            break;
        case EDITOR_LANG_BATCH:
            tpl = "@echo off\r\necho Frame ready\r\n";
            break;
        case EDITOR_LANG_PLAIN:
            tpl = "Frame ready.\r\n";
            break;
        case EDITOR_LANG_C:
        default:
            tpl = "// Frame ready\nint main(void) {\n    return 0;\n}\n";
            break;
    }

    SendMessageA(ctx->hSci, SCI_SETTEXT, 0, (LPARAM)tpl);
    SendMessageA(ctx->hSci, SCI_SETSAVEPOINT, 0, 0);
    ctx->modified = FALSE;
    SyncMenuChecks(ctx);
    NotifyFormState(ctx);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)lpvReserved;
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            g_hDllInst = hinstDLL;
            DisableThreadLibraryCalls(hinstDLL);
            EditorFrame_RegisterClass(hinstDLL);
            break;
        case DLL_PROCESS_DETACH:
            EditorFrame_UnregisterClass(hinstDLL);
            g_hDllInst = NULL;
            break;
    }
    return TRUE;
}