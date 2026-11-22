#include "cborg_symtree.h"
#include "Scintilla.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "comctl32.lib")

typedef struct {
    HWND hwndFound;
} SCI_FIND_CTX;

static BOOL CALLBACK FindScintillaChildProc(HWND hwnd, LPARAM lParam) {
    char cls[64];
    SCI_FIND_CTX *ctx = (SCI_FIND_CTX *)lParam;
    if (GetClassNameA(hwnd, cls, sizeof(cls)) > 0 && lstrcmpiA(cls, "Scintilla") == 0) {
        ctx->hwndFound = hwnd;
        return FALSE;
    }
    return TRUE;
}

static HWND ResolveNestedScintilla(HWND hFrame) {
    SCI_FIND_CTX ctx = { NULL };
    if (!hFrame || !IsWindow(hFrame)) return NULL;
    EnumChildWindows(hFrame, FindScintillaChildProc, (LPARAM)&ctx);
    return ctx.hwndFound;
}

static BOOL IsIdentChar(char c) {
    return (isalnum((unsigned char)c) || c == '_');
}

static BOOL IsControlKeyword(const char *tok, size_t len) {
    static const char *const kw[] = {
        "if", "else", "for", "while", "switch", "return", "sizeof",
        "catch", "typedef", "struct", "union", "enum", "case", "do",
        "const", "static", "extern", "volatile", "void", "int", "char",
        "short", "long", "float", "double", "unsigned", "signed", "BOOL"
    };
    size_t i;
    for (i = 0; i < ARRAYSIZE(kw); ++i) {
        if (strlen(kw[i]) == len && strncmp(kw[i], tok, len) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

static HTREEITEM AddSymbolItem(HWND hTree, HTREEITEM hRoot, const char *label, int lineZeroBased, BOOL bold) {
    TVINSERTSTRUCTA tvis;
    ZeroMemory(&tvis, sizeof(tvis));
    tvis.hParent        = hRoot ? hRoot : TVI_ROOT;
    tvis.hInsertAfter   = TVI_LAST;
    tvis.item.mask      = TVIF_TEXT | TVIF_PARAM | TVIF_STATE;
    tvis.item.pszText   = (LPSTR)label;
    tvis.item.lParam    = (LPARAM)(INT_PTR)(lineZeroBased + 1);
    tvis.item.stateMask = bold ? TVIS_BOLD : 0;
    tvis.item.state     = bold ? TVIS_BOLD : 0;
    return (HTREEITEM)SendMessageA(hTree, TVM_INSERTITEMA, 0, (LPARAM)&tvis);
}

static size_t SkipWs(const char *buf, size_t len, size_t i, int *ioLine) {
    while (i < len) {
        if (buf[i] == '\n') {
            if (ioLine) ++(*ioLine);
            ++i;
        } else if (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\r') {
            ++i;
        } else {
            break;
        }
    }
    return i;
}

/* Parses C/C++ Types (struct/enum/typedef), Globals/Macros, and Functions */
static int ParseCSymbols(HWND hTree, HTREEITEM hTypes, HTREEITEM hGlobals, HTREEITEM hFuncs, const char *buf, size_t len) {
    int total = 0;
    int braceDepth = 0;
    int line = 0;
    BOOL inLineComment = FALSE;
    BOOL inBlockComment = FALSE;
    BOOL inString = FALSE;
    char quoteChar = 0;
    size_t i = 0;

    while (i < len) {
        char c = buf[i];
        char next = (i + 1 < len) ? buf[i + 1] : '\0';

        if (c == '\n') {
            inLineComment = FALSE;
            ++line;
            ++i;
            continue;
        }
        if (inLineComment) { ++i; continue; }
        if (inBlockComment) {
            if (c == '*' && next == '/') { inBlockComment = FALSE; i += 2; }
            else { ++i; }
            continue;
        }
        if (inString) {
            if (c == '\\' && next != '\0') { i += 2; continue; }
            if (c == quoteChar) { inString = FALSE; }
            ++i;
            continue;
        }

        if (c == '/' && next == '/') { inLineComment = TRUE; i += 2; continue; }
        if (c == '/' && next == '*') { inBlockComment = TRUE; i += 2; continue; }
        if (c == '"' || c == '\'') { inString = TRUE; quoteChar = c; ++i; continue; }

        /* Preprocessor #define at file scope */
        if (braceDepth == 0 && c == '#') {
            int defLine = line;
            size_t p = i + 1;
            while (p < len && (buf[p] == ' ' || buf[p] == '\t')) ++p;
            if ((len - p) > 7 && strncmp(buf + p, "define", 6) == 0 && !IsIdentChar(buf[p + 6])) {
                size_t nameStart = p + 6;
                size_t nameEnd;
                while (nameStart < len && (buf[nameStart] == ' ' || buf[nameStart] == '\t')) ++nameStart;
                nameEnd = nameStart;
                while (nameEnd < len && IsIdentChar(buf[nameEnd])) ++nameEnd;
                if (nameEnd > nameStart) {
                    char label[256];
                    size_t nLen = nameEnd - nameStart;
                    if (nLen > 180) nLen = 180;
                    _snprintf_s(label, sizeof(label), _TRUNCATE, "#%.*s  [Ln %d]", (int)nLen, buf + nameStart, defLine + 1);
                    AddSymbolItem(hTree, hGlobals, label, defLine, FALSE);
                    ++total;
                }
            }
            while (i < len && buf[i] != '\n') ++i;
            continue;
        }

        if (c == '{') { ++braceDepth; ++i; continue; }
        if (c == '}') { if (braceDepth > 0) --braceDepth; ++i; continue; }

        /* File-scope declarations: typedefs, structs, enums, globals, and functions */
        if (braceDepth == 0 && (isalpha((unsigned char)c) || c == '_')) {
            int declLine = line;
            size_t lastIdentStart = 0;
            size_t lastIdentEnd = 0;
            int lastIdentLine = declLine;
            BOOL isTypedef = FALSE;
            BOOL isTagType = FALSE;

            while (i < len && braceDepth == 0) {
                if (buf[i] == '\n') { ++line; ++i; continue; }
                if (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\r' || buf[i] == '*') { ++i; continue; }
                if (buf[i] == '/' && i + 1 < len && buf[i + 1] == '/') {
                    while (i < len && buf[i] != '\n') ++i;
                    continue;
                }
                if (buf[i] == '/' && i + 1 < len && buf[i + 1] == '*') {
                    i += 2;
                    while (i + 1 < len && !(buf[i] == '*' && buf[i + 1] == '/')) {
                        if (buf[i] == '\n') ++line;
                        ++i;
                    }
                    if (i + 1 < len) i += 2;
                    continue;
                }

                if (isalpha((unsigned char)buf[i]) || buf[i] == '_') {
                    size_t idStart = i;
                    int idLine = line;
                    while (i < len && IsIdentChar(buf[i])) ++i;
                    if ((i - idStart) == 7 && strncmp(buf + idStart, "typedef", 7) == 0) {
                        isTypedef = TRUE;
                    } else if (((i - idStart) == 6 && strncmp(buf + idStart, "struct", 6) == 0) ||
                               ((i - idStart) == 4 && strncmp(buf + idStart, "enum", 4) == 0) ||
                               ((i - idStart) == 5 && strncmp(buf + idStart, "union", 5) == 0)) {
                        isTagType = TRUE;
                    }
                    lastIdentStart = idStart;
                    lastIdentEnd = i;
                    lastIdentLine = idLine;
                    continue;
                }

                /* Case A: Function declaration or definition: lastIdent(...) */
                if (buf[i] == '(' && lastIdentEnd > lastIdentStart &&
                    !IsControlKeyword(buf + lastIdentStart, lastIdentEnd - lastIdentStart)) {
                    int paren = 1;
                    size_t p = i + 1;
                    while (p < len && paren > 0) {
                        if (buf[p] == '\n') ++line;
                        else if (buf[p] == '(') ++paren;
                        else if (buf[p] == ')') --paren;
                        ++p;
                    }
                    p = SkipWs(buf, len, p, &line);
                    if (p < len && buf[p] == '{') {
                        char label[256];
                        size_t nLen = lastIdentEnd - lastIdentStart;
                        if (nLen > 180) nLen = 180;
                        _snprintf_s(label, sizeof(label), _TRUNCATE, "%.*s()  [Ln %d]",
                                    (int)nLen, buf + lastIdentStart, lastIdentLine + 1);
                        AddSymbolItem(hTree, hFuncs, label, lastIdentLine, FALSE);
                        ++total;
                        i = p; /* Let outer loop enter '{' and track braceDepth */
                        break;
                    } else {
                        /* Prototype ending in ';' — skip to ';' */
                        while (p < len && buf[p] != ';') {
                            if (buf[p] == '\n') ++line;
                            ++p;
                        }
                        i = (p < len) ? p + 1 : p;
                        break;
                    }
                }

                /* Case B: struct/enum/typedef block: "{ ... } TypeName;" */
                if (buf[i] == '{') {
                    int depth = 1;
                    size_t p = i + 1;
                    while (p < len && depth > 0) {
                        if (buf[p] == '\n') ++line;
                        else if (buf[p] == '{') ++depth;
                        else if (buf[p] == '}') --depth;
                        ++p;
                    }
                    /* Read trailing typedef name before ';' if present */
                    while (p < len && buf[p] != ';') {
                        if (buf[p] == '\n') ++line;
                        else if (isalpha((unsigned char)buf[p]) || buf[p] == '_') {
                            lastIdentStart = p;
                            lastIdentLine = line;
                            while (p < len && IsIdentChar(buf[p])) ++p;
                            lastIdentEnd = p;
                            continue;
                        }
                        ++p;
                    }
                    if ((isTypedef || isTagType) && lastIdentEnd > lastIdentStart) {
                        char label[256];
                        size_t nLen = lastIdentEnd - lastIdentStart;
                        if (nLen > 180) nLen = 180;
_snprintf_s(label, sizeof(label), _TRUNCATE, "%.*s  [Ln %d]",
(int)nLen, buf + lastIdentStart, lastIdentLine + 1);
AddSymbolItem(hTree, hTypes, label, lastIdentLine, FALSE);
++total;
}
i = (p < len) ? p + 1 : p;
break;
}
/* Case C: Global variable or simple typedef ending in ';' or '=' */
if ((buf[i] == ';' || buf[i] == '=' || buf[i] == '[') && lastIdentEnd > lastIdentStart &&
!IsControlKeyword(buf + lastIdentStart, lastIdentEnd - lastIdentStart)) {
char label[256];
size_t nLen = lastIdentEnd - lastIdentStart;
HTREEITEM targetGroup = isTypedef ? hTypes : hGlobals;
if (nLen > 180) nLen = 180;
_snprintf_s(label, sizeof(label), _TRUNCATE, "%.*s  [Ln %d]",
(int)nLen, buf + lastIdentStart, lastIdentLine + 1);
AddSymbolItem(hTree, targetGroup, label, lastIdentLine, FALSE);
++total;
while (i < len && buf[i] != ';') {
if (buf[i] == '\n') ++line;
++i;
}
if (i < len) ++i;
break;
}
++i;
}
continue;
}
++i;
}
return total;
}
static int ParsePythonSymbols(HWND hTree, HTREEITEM hTypes, HTREEITEM hFuncs, const char *buf, size_t len) {
int total = 0;
int line = 0;
size_t i = 0;
while (i < len) {
size_t lineStart = i;
while (i < len && buf[i] != '\n') ++i;
{
size_t p = lineStart;
while (p < i && (buf[p] == ' ' || buf[p] == '\t')) ++p;
if ((i - p) > 6 && strncmp(buf + p, "class ", 6) == 0) {
size_t s = p + 6, e;
while (s < i && (buf[s] == ' ' || buf[s] == '\t')) ++s;
e = s;
while (e < i && IsIdentChar(buf[e])) ++e;
if (e > s) {
char label[256];
_snprintf_s(label, sizeof(label), _TRUNCATE, "%.*s  [Ln %d]", (int)(e - s), buf + s, line + 1);
AddSymbolItem(hTree, hTypes, label, line, FALSE);
++total;
}
} else if ((i - p) > 4 && strncmp(buf + p, "def ", 4) == 0) {
size_t s = p + 4, e;
while (s < i && (buf[s] == ' ' || buf[s] == '\t')) ++s;
e = s;
while (e < i && IsIdentChar(buf[e])) ++e;
if (e > s) {
char label[256];
_snprintf_s(label, sizeof(label), _TRUNCATE, "%.*s()  [Ln %d]", (int)(e - s), buf + s, line + 1);
AddSymbolItem(hTree, hFuncs, label, line, FALSE);
++total;
}
}
}
if (i < len && buf[i] == '\n') { ++i; ++line; }
}
return total;
}
HWND CborgSymTree_Create(
HWND      hParent,
HINSTANCE hInstance,
UINT      treeId,
int       x,
int       y,
int       width,
int       height,
HFONT     hFont
) {
INITCOMMONCONTROLSEX icc;
HWND hTree;
if (!hParent || !IsWindow(hParent)) return NULL;
ZeroMemory(&icc, sizeof(icc));
icc.dwSize = sizeof(icc);
icc.dwICC  = ICC_TREEVIEW_CLASSES;
InitCommonControlsEx(&icc);
hTree = CreateWindowExW(
WS_EX_CLIENTEDGE,
WC_TREEVIEWW,
L"",
WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS,
x, y, width, height,
hParent,
(HMENU)(UINT_PTR)treeId,
hInstance ? hInstance : GetModuleHandleW(NULL),
NULL
);
if (hTree && hFont) {
SendMessageW(hTree, WM_SETFONT, (WPARAM)hFont, TRUE);
}
return hTree;
}
void CborgSymTree_ApplyTheme(HWND hTree, EDITOR_THEME_MODE theme) {
if (!hTree || !IsWindow(hTree)) return;
if (theme == EDITOR_THEME_LIGHT) {
TreeView_SetBkColor(hTree, RGB(244, 246, 250));
TreeView_SetTextColor(hTree, RGB(24, 28, 36));
} else {
TreeView_SetBkColor(hTree, RGB(10, 14, 22));
TreeView_SetTextColor(hTree, RGB(0, 240, 255));
}
}
int CborgSymTree_RefreshFromFrame(HWND hTree, HWND hFrame, EDITOR_LANG_MODE lang) {
size_t len = 0;
char *buf;
HTREEITEM hTypes;
HTREEITEM hGlobals;
HTREEITEM hFuncs;
int found = 0;
if (!hTree || !IsWindow(hTree)) return 0;
SendMessageA(hTree, WM_SETREDRAW, FALSE, 0);
TreeView_DeleteAllItems(hTree);
hTypes   = AddSymbolItem(hTree, NULL, "Types & Structs", -1, TRUE);
hGlobals = AddSymbolItem(hTree, NULL, "Globals & Defines", -1, TRUE);
hFuncs   = AddSymbolItem(hTree, NULL, "Functions", -1, TRUE);
if (hFrame && IsWindow(hFrame)) {
buf = EditorFrame_GetTextAlloc(hFrame, &len);
if (buf) {
if (lang == EDITOR_LANG_PYTHON) {
found = ParsePythonSymbols(hTree, hTypes, hFuncs, buf, len);
} else {
found = ParseCSymbols(hTree, hTypes, hGlobals, hFuncs, buf, len);
}
EditorFrame_FreeText(buf);
}
}
if (hTypes)   TreeView_Expand(hTree, hTypes, TVE_EXPAND);
if (hGlobals) TreeView_Expand(hTree, hGlobals, TVE_EXPAND);
if (hFuncs)   TreeView_Expand(hTree, hFuncs, TVE_EXPAND);
SendMessageA(hTree, WM_SETREDRAW, TRUE, 0);
InvalidateRect(hTree, NULL, TRUE);
return found;
}
int CborgSymTree_GetSelectedLine(HWND hTree) {
HTREEITEM hSel;
TVITEMA it;
if (!hTree || !IsWindow(hTree)) return -1;
hSel = TreeView_GetSelection(hTree);
if (!hSel) return -1;
ZeroMemory(&it, sizeof(it));
it.mask  = TVIF_PARAM;
it.hItem = hSel;
if (!SendMessageA(hTree, TVM_GETITEMA, 0, (LPARAM)&it) || it.lParam <= 0) {
return -1;
}
return (int)(INT_PTR)it.lParam - 1;
}
BOOL CborgSymTree_JumpToSelected(HWND hTree, HWND hFrame) {
int line = CborgSymTree_GetSelectedLine(hTree);
HWND hSci;
Sci_Position pos;
int linesOnScreen;
int topLine;
if (line < 0 || !hFrame || !IsWindow(hFrame)) return FALSE;
hSci = ResolveNestedScintilla(hFrame);
if (!hSci) return FALSE;
SendMessageA(hSci, SCI_ENSUREVISIBLEENFORCEPOLICY, (WPARAM)line, 0);
pos = (Sci_Position)SendMessageA(hSci, SCI_POSITIONFROMLINE, (WPARAM)line, 0);
SendMessageA(hSci, SCI_GOTOPOS, (WPARAM)pos, 0);
/* Center the target line cleanly in the Scintilla viewport */
linesOnScreen = (int)SendMessageA(hSci, SCI_LINESONSCREEN, 0, 0);
topLine = line - (linesOnScreen / 3);
if (topLine < 0) topLine = 0;
SendMessageA(hSci, SCI_SETFIRSTVISIBLELINE, (WPARAM)topLine, 0);
SetFocus(hSci);
return TRUE;
}