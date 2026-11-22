#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "editor_frame_api.h"
#include "cborg_tree.h"
#include "cborg_symtree.h"

#define CBORG_CLASS_NAME      "CborgEditorForm"
#define CBORG_WINDOW_TITLE    "Cborg Editor"

#define IDC_CBORG_TREE        3001
#define IDC_CBORG_FRAME       3002
#define IDC_CBORG_STATUS      3003
#define IDC_CBORG_SYMTREE     3004

#define IDC_BTN_NEW           3101
#define IDC_BTN_OPEN          3102
#define IDC_BTN_SAVE          3103
#define IDC_BTN_SAVEAS        3104
#define IDC_BTN_THEME         3105
#define IDC_BTN_LANG          3106

#define IDM_FILE_NEW          3201
#define IDM_FILE_OPEN         3202
#define IDM_FILE_SAVE         3203
#define IDM_FILE_SAVEAS       3204
#define IDM_FILE_EXIT         3205

#define MAX_FILE_BYTES        (32 * 1024 * 1024)

typedef struct {
    HWND              hForm;
    HWND              hTree;
    HWND              hSymTree;
    HWND              hFrame;
    HWND              hStatus;
    HWND              btnNew;
    HWND              btnOpen;
    HWND              btnSave;
    HWND              btnSaveAs;
    HWND              btnTheme;
    HWND              btnLang;
    HFONT             hFont;
    int               treeWidth;
    int               line;
    int               col;
    int               lineCount;
    int               lastLineCount;
    int               charCount;
    BOOL              modified;
    EDITOR_THEME_MODE theme;
    EDITOR_LANG_MODE  lang;
    char              filePath[MAX_PATH];
    char              selectedNode[MAX_PATH];
} CBORG_STATE;


static void Cborg_GetBaseName(const CBORG_STATE *st, char *outName, size_t cchOut) {
    const char *base = "Untitled";
    const char *p;
    if (st && st->filePath[0] != '\0') {
        base = st->filePath;
        for (p = st->filePath; *p != '\0'; ++p) {
            if (*p == '\\' || *p == '/') {
                base = p + 1;
            }
        }
    }
    _snprintf_s(outName, cchOut, _TRUNCATE, "%s", base);
}

static void Cborg_RefreshTitleAndStatus(CBORG_STATE *st, const char *note) {
    char base[MAX_PATH];
    char title[MAX_PATH + 64];
    char status[512];
    const char *themeStr = (st->theme == EDITOR_THEME_LIGHT) ? "LIGHT" : "OBSIDIAN";
    const char *langStr = "C";

    if (!st) return;

    Cborg_GetBaseName(st, base, sizeof(base));
    _snprintf_s(
        title,
        sizeof(title),
        _TRUNCATE,
        "%s%s - %s",
        base,
        st->modified ? " *" : "",
        CBORG_WINDOW_TITLE
    );
    if (st->hForm && IsWindow(st->hForm)) {
        SetWindowTextA(st->hForm, title);
    }

    switch (st->lang) {
        case EDITOR_LANG_PYTHON: langStr = "PYTHON"; break;
        case EDITOR_LANG_JSON:   langStr = "JSON";   break;
        case EDITOR_LANG_BATCH:  langStr = "BATCH";  break;
        case EDITOR_LANG_PLAIN:  langStr = "TEXT";   break;
        case EDITOR_LANG_C:
        default:                 langStr = "C";      break;
    }

    _snprintf_s(
        status,
        sizeof(status),
        _TRUNCATE,
        "Tree: %s | Ln %d, Col %d | Lines: %d | Chars: %d | %s | %s%s%s",
        st->selectedNode[0] ? st->selectedNode : "(none)",
        st->line,
        st->col,
        st->lineCount,
        st->charCount,
        langStr,
        themeStr,
        (note && note[0]) ? " | " : "",
        (note && note[0]) ? note : ""
    );
    if (st->hStatus && IsWindow(st->hStatus)) {
        SetWindowTextA(st->hStatus, status);
    }
}

static EDITOR_LANG_MODE Cborg_DetectLangFromPath(const char *path) {
    const char *dot = NULL;
    const char *p;
    if (!path || path[0] == '\0') return EDITOR_LANG_C;
    for (p = path; *p != '\0'; ++p) {
        if (*p == '.') dot = p;
        else if (*p == '\\' || *p == '/') dot = NULL;
    }
    if (!dot) return EDITOR_LANG_C;
    if (lstrcmpiA(dot, ".py") == 0) return EDITOR_LANG_PYTHON;
    if (lstrcmpiA(dot, ".json") == 0) return EDITOR_LANG_JSON;
    if (lstrcmpiA(dot, ".bat") == 0 || lstrcmpiA(dot, ".cmd") == 0) return EDITOR_LANG_BATCH;
    if (lstrcmpiA(dot, ".txt") == 0 || lstrcmpiA(dot, ".log") == 0 || lstrcmpiA(dot, ".md") == 0) return EDITOR_LANG_PLAIN;
    return EDITOR_LANG_C;
}

static BOOL Cborg_SaveToPath(CBORG_STATE *st, const char *path) {
    size_t textLen = 0;
    char *buf;
    HANDLE hFile;
    DWORD written = 0;

    if (!st || !st->hFrame || !path || path[0] == '\0') return FALSE;

    buf = EditorFrame_GetTextAlloc(st->hFrame, &textLen);
    if (!buf) {
        MessageBoxA(st->hForm, "Failed to read text buffer.", "Save Error", MB_ICONERROR);
        return FALSE;
    }

    hFile = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        EditorFrame_FreeText(buf);
        MessageBoxA(st->hForm, "Failed to open destination file.", "Save Error", MB_ICONERROR);
        return FALSE;
    }

    if (textLen > 0) {
        if (!WriteFile(hFile, buf, (DWORD)textLen, &written, NULL) || written != (DWORD)textLen) {
            CloseHandle(hFile);
            EditorFrame_FreeText(buf);
            MessageBoxA(st->hForm, "Failed while writing file.", "Save Error", MB_ICONERROR);
            return FALSE;
        }
    }

    CloseHandle(hFile);
    EditorFrame_FreeText(buf);

    lstrcpynA(st->filePath, path, MAX_PATH);
    EditorFrame_SetSavePoint(st->hFrame);
    st->modified = FALSE;
    Cborg_RefreshTitleAndStatus(st, "Saved");
    return TRUE;
}

static BOOL Cborg_SaveFile(CBORG_STATE *st, BOOL saveAs) {
    char pathBuf[MAX_PATH];
    OPENFILENAMEA ofn;

    if (!st) return FALSE;

    if (!saveAs && st->filePath[0] != '\0') {
        return Cborg_SaveToPath(st, st->filePath);
    }

    lstrcpynA(pathBuf, st->filePath[0] ? st->filePath : "untitled.c", MAX_PATH);
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = st->hForm;
    ofn.lpstrFilter = "C/C++ (*.c;*.h;*.cpp;*.hpp)\0*.c;*.h;*.cpp;*.hpp\0Text (*.txt;*.md;*.log)\0*.txt;*.md;*.log\0Python (*.py)\0*.py\0JSON (*.json)\0*.json\0Batch (*.bat;*.cmd)\0*.bat;*.cmd\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile   = pathBuf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameA(&ofn)) {
        return FALSE;
    }

    return Cborg_SaveToPath(st, pathBuf);
}

static BOOL Cborg_PromptSaveIfDirty(CBORG_STATE *st) {
    char base[MAX_PATH];
    char msg[MAX_PATH + 128];
    int ans;

    if (!st || !st->modified) return TRUE;

    Cborg_GetBaseName(st, base, sizeof(base));
    _snprintf_s(msg, sizeof(msg), _TRUNCATE, "Save changes to %s before continuing?", base);
    ans = MessageBoxA(st->hForm, msg, CBORG_WINDOW_TITLE, MB_YESNOCANCEL | MB_ICONQUESTION);
    if (ans == IDCANCEL) return FALSE;
    if (ans == IDYES) return Cborg_SaveFile(st, FALSE);
    return TRUE;
}

static void Cborg_NewFile(CBORG_STATE *st) {
    if (!st || !Cborg_PromptSaveIfDirty(st)) return;

    st->filePath[0] = '\0';
    st->modified = FALSE;
    EditorFrame_InsertTemplate(st->hFrame, st->lang, st->theme);
    EditorFrame_GetCaretStats(st->hFrame, &st->line, &st->col, &st->lineCount, &st->charCount);
    Cborg_RefreshTitleAndStatus(st, "New file");
}

static BOOL Cborg_OpenFilePath(CBORG_STATE *st, const char *path) {
    HANDLE hFile;
    LARGE_INTEGER fSize;
    DWORD bytesRead = 0;
    char *buf;

    if (!st || !st->hFrame || !path || path[0] == '\0') return FALSE;

    hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxA(st->hForm, "Unable to open file.", "Open Error", MB_ICONERROR);
        return FALSE;
    }

    if (!GetFileSizeEx(hFile, &fSize) || fSize.QuadPart < 0 || fSize.QuadPart >= MAX_FILE_BYTES) {
        CloseHandle(hFile);
        MessageBoxA(st->hForm, "File size is invalid or too large.", "Open Error", MB_ICONWARNING);
        return FALSE;
    }

    buf = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)fSize.QuadPart + 1);
    if (!buf) {
        CloseHandle(hFile);
        return FALSE;
    }

    if (!ReadFile(hFile, buf, (DWORD)fSize.QuadPart, &bytesRead, NULL)) {
        HeapFree(GetProcessHeap(), 0, buf);
        CloseHandle(hFile);
        MessageBoxA(st->hForm, "Failed reading file.", "Open Error", MB_ICONERROR);
        return FALSE;
    }
    buf[bytesRead] = '\0';
    CloseHandle(hFile);

    lstrcpynA(st->filePath, path, MAX_PATH);
    st->lang = Cborg_DetectLangFromPath(path);
    EditorFrame_SetLanguage(st->hFrame, st->lang);
    EditorFrame_SetText(st->hFrame, buf);
    HeapFree(GetProcessHeap(), 0, buf);

    st->modified = FALSE;
    EditorFrame_GetCaretStats(st->hFrame, &st->line, &st->col, &st->lineCount, &st->charCount);
    Cborg_RefreshTitleAndStatus(st, "Opened");
    return TRUE;
}

static void Cborg_OpenFileDialog(CBORG_STATE *st) {
    char pathBuf[MAX_PATH];
    OPENFILENAMEA ofn;

    if (!st || !Cborg_PromptSaveIfDirty(st)) return;

    pathBuf[0] = '\0';
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = st->hForm;
    ofn.lpstrFilter = "All Supported (*.c;*.h;*.cpp;*.hpp;*.py;*.json;*.bat;*.cmd;*.txt;*.md)\0*.c;*.h;*.cpp;*.hpp;*.py;*.json;*.bat;*.cmd;*.txt;*.md\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile   = pathBuf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if (GetOpenFileNameA(&ofn)) {
        Cborg_OpenFilePath(st, pathBuf);
    }
}

static HMENU Cborg_BuildMenuBar(void) {
    HMENU hBar  = CreateMenu();
    HMENU hFile = CreatePopupMenu();
    HMENU hEdit = CreatePopupMenu();
    HMENU hView = CreatePopupMenu();
    HMENU hLang = CreatePopupMenu();

    AppendMenuA(hFile, MF_STRING, IDM_FILE_NEW,    "&New");
    AppendMenuA(hFile, MF_STRING, IDM_FILE_OPEN,   "&Open...");
    AppendMenuA(hFile, MF_STRING, IDM_FILE_SAVE,   "&Save");
    AppendMenuA(hFile, MF_STRING, IDM_FILE_SAVEAS, "Save &As...");
    AppendMenuA(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hFile, MF_STRING, IDM_FILE_EXIT,   "E&xit");

    AppendMenuA(hEdit, MF_STRING, CMD_EDITOR_UNDO,       "&Undo");
    AppendMenuA(hEdit, MF_STRING, CMD_EDITOR_REDO,       "&Redo");
    AppendMenuA(hEdit, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hEdit, MF_STRING, CMD_EDITOR_CUT,        "Cu&t");
    AppendMenuA(hEdit, MF_STRING, CMD_EDITOR_COPY,       "&Copy");
    AppendMenuA(hEdit, MF_STRING, CMD_EDITOR_PASTE,      "&Paste");
    AppendMenuA(hEdit, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hEdit, MF_STRING, CMD_EDITOR_SELECT_ALL, "Select &All");

    AppendMenuA(hView, MF_STRING, CMD_EDITOR_THEME_DARK,    "&Obsidian Dark");
    AppendMenuA(hView, MF_STRING, CMD_EDITOR_THEME_LIGHT,   "&Light");
    AppendMenuA(hView, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hView, MF_STRING, CMD_EDITOR_WRAP_TOGGLE,   "&Word Wrap");
    AppendMenuA(hView, MF_STRING, CMD_EDITOR_WS_TOGGLE,     "Show &Whitespace");
    AppendMenuA(hView, MF_STRING, CMD_EDITOR_EOL_TOGGLE,    "Show &EOL");
    AppendMenuA(hView, MF_STRING, CMD_EDITOR_GUIDES_TOGGLE, "Show &Indent Guides");

    AppendMenuA(hLang, MF_STRING, CMD_EDITOR_LANG_C,      "&C / C++");
    AppendMenuA(hLang, MF_STRING, CMD_EDITOR_LANG_PYTHON, "&Python");
    AppendMenuA(hLang, MF_STRING, CMD_EDITOR_LANG_JSON,   "&JSON");
    AppendMenuA(hLang, MF_STRING, CMD_EDITOR_LANG_BATCH,  "&Batch");
    AppendMenuA(hLang, MF_STRING, CMD_EDITOR_LANG_PLAIN,  "&Plain Text");

    AppendMenuA(hBar, MF_POPUP, (UINT_PTR)hFile, "&File");
    AppendMenuA(hBar, MF_POPUP, (UINT_PTR)hEdit, "&Edit");
    AppendMenuA(hBar, MF_POPUP, (UINT_PTR)hView, "&View");
    AppendMenuA(hBar, MF_POPUP, (UINT_PTR)hLang, "&Language");

    return hBar;
}

static HWND Cborg_MakeButton(HWND parent, HINSTANCE hInst, const char *label, int id) {
    return CreateWindowExA(
        0, "BUTTON", label,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        0, 0, 74, 24,
        parent, (HMENU)(UINT_PTR)id, hInst, NULL
    );
}

static void Cborg_Layout(CBORG_STATE *st, int width, int height);


static LRESULT CALLBACK Cborg_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    CBORG_STATE *st = (CBORG_STATE *)(LONG_PTR)GetWindowLongPtrA(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CREATE: {
            HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrA(hwnd, GWLP_HINSTANCE);
            RECT rc;

            st = (CBORG_STATE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(CBORG_STATE));
            if (!st) return -1;
            st->hForm     = hwnd;
            st->treeWidth = 215;
            st->theme     = EDITOR_THEME_OBSIDIAN;
            st->lang      = EDITOR_LANG_C;
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)st);

            st->hFont = CreateFontA(
                -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI"
            );

            st->btnNew    = Cborg_MakeButton(hwnd, hInst, "New",     IDC_BTN_NEW);
            st->btnOpen   = Cborg_MakeButton(hwnd, hInst, "Open",    IDC_BTN_OPEN);
            st->btnSave   = Cborg_MakeButton(hwnd, hInst, "Save",    IDC_BTN_SAVE);
            st->btnSaveAs = Cborg_MakeButton(hwnd, hInst, "Save As", IDC_BTN_SAVEAS);
            st->btnTheme  = Cborg_MakeButton(hwnd, hInst, "Theme",   IDC_BTN_THEME);
            st->btnLang   = Cborg_MakeButton(hwnd, hInst, "Lang",    IDC_BTN_LANG);

            st->hTree    = CborgTree_Create(hwnd, hInst, IDC_CBORG_TREE, 8, 34, 215, 310, st->hFont);
            st->hSymTree = CborgSymTree_Create(hwnd, hInst, IDC_CBORG_SYMTREE, 8, 350, 215, 190, st->hFont);

            GetClientRect(hwnd, &rc);
            st->hFrame = EditorFrame_Create(
                hwnd,
                hInst,
                IDC_CBORG_FRAME,
                231, 34,
                rc.right > 250 ? rc.right - 239 : 740,
                rc.bottom > 80 ? rc.bottom - 64 : 560,
                NULL
            );

            st->hStatus = CreateWindowExA(
                0, "STATIC", "",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                0, 0, 400, 22,
                hwnd, (HMENU)(UINT_PTR)IDC_CBORG_STATUS, hInst, NULL
            );

            if (!st->hTree || !st->hSymTree || !st->hFrame) {
                MessageBoxA(hwnd, "Failed to create TreeViews or MercwarEditorFrame.", "Init Error", MB_ICONERROR);
                return -1;
            }

if (st->hFont) {
    HWND ctrls[7];
    int i;
    ctrls[0] = st->btnNew;
    ctrls[1] = st->btnOpen;
    ctrls[2] = st->btnSave;
    ctrls[3] = st->btnSaveAs;
    ctrls[4] = st->btnTheme;
    ctrls[5] = st->btnLang;
    ctrls[6] = st->hStatus;
    for (i = 0; i < 7; ++i) {
        if (ctrls[i]) SendMessageA(ctrls[i], WM_SETFONT, (WPARAM)st->hFont, TRUE);
    }
}


            EditorFrame_AttachMenu(st->hFrame, GetMenu(hwnd));
            EditorFrame_SetTheme(st->hFrame, st->theme);
            EditorFrame_SetLanguage(st->hFrame, st->lang);
            CborgTree_PopulateDefault(st->hTree, st->theme);
            EditorFrame_GetCaretStats(st->hFrame, &st->line, &st->col, &st->lineCount, &st->charCount);

            CborgSymTree_ApplyTheme(st->hSymTree, st->theme);
            CborgSymTree_RefreshFromFrame(st->hSymTree, st->hFrame, st->lang);
            st->lastLineCount = st->lineCount;

            Cborg_Layout(st, rc.right, rc.bottom);
            Cborg_RefreshTitleAndStatus(st, "Ready");
            return 0;
        }

        case WM_SIZE:
            if (st) {
                Cborg_Layout(st, LOWORD(lParam), HIWORD(lParam));
            }
            return 0;

        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            if (!st) break;

            switch (id) {
                case IDM_FILE_NEW:
                case IDC_BTN_NEW:
                    Cborg_NewFile(st);
                    return 0;
                case IDM_FILE_OPEN:
                case IDC_BTN_OPEN:
                    Cborg_OpenFileDialog(st);
                    return 0;
                case IDM_FILE_SAVE:
                case IDC_BTN_SAVE:
                    Cborg_SaveFile(st, FALSE);
                    return 0;
                case IDM_FILE_SAVEAS:
                case IDC_BTN_SAVEAS:
                    Cborg_SaveFile(st, TRUE);
                    return 0;
                case IDM_FILE_EXIT:
                    PostMessageA(hwnd, WM_CLOSE, 0, 0);
                    return 0;
                case IDM_TREE_OPEN_FILE: {
                    char pathBuf[MAX_PATH];
                    BOOL isFolder = FALSE;
                    if (CborgTree_GetSelectedPath(st->hTree, pathBuf, MAX_PATH, &isFolder) && !isFolder) {
                        if (Cborg_PromptSaveIfDirty(st)) {
                            Cborg_OpenFilePath(st, pathBuf);
                        }
                    }
                    return 0;
                }
                case IDC_BTN_THEME:
                    EditorFrame_SendCommand(st->hFrame, CMD_EDITOR_THEME_TOGGLE);
                    CborgTree_ApplyTheme(st->hTree, st->theme);
                    CborgSymTree_ApplyTheme(st->hSymTree, st->theme);
                    return 0;
                case IDC_BTN_LANG:
                    EditorFrame_SendCommand(st->hFrame, CMD_EDITOR_LANG_CYCLE);
                    CborgSymTree_RefreshFromFrame(st->hSymTree, st->hFrame, st->lang);
                    return 0;
            }

            if (st->hFrame && EditorFrame_SendCommand(st->hFrame, id)) {
                CborgTree_ApplyTheme(st->hTree, st->theme);
                CborgSymTree_ApplyTheme(st->hSymTree, st->theme);
                CborgSymTree_RefreshFromFrame(st->hSymTree, st->hFrame, st->lang);
                return 0;
            }
            break;
        }

        case WM_NOTIFY: {
            const NMHDR *nm = (const NMHDR *)lParam;
            if (!st || !nm) break;

            if (nm->hwndFrom == st->hFrame && nm->code == NM_EDITOR_STATE_CHANGED) {
                const NM_EDITOR_FRAME_STATE *fs = (const NM_EDITOR_FRAME_STATE *)lParam;
                EDITOR_LANG_MODE prevLang = st->lang;
                st->modified  = fs->modified;
                st->line      = fs->line;
                st->col       = fs->col;
                st->lineCount = fs->lineCount;
                st->charCount = fs->charCount;
                st->theme     = (EDITOR_THEME_MODE)fs->themeMode;
                st->lang      = (EDITOR_LANG_MODE)fs->langMode;
                CborgTree_ApplyTheme(st->hTree, st->theme);
                CborgSymTree_ApplyTheme(st->hSymTree, st->theme);
                if (st->lineCount != st->lastLineCount || st->lang != prevLang) {
                    st->lastLineCount = st->lineCount;
                    CborgSymTree_RefreshFromFrame(st->hSymTree, st->hFrame, st->lang);
                }
                Cborg_RefreshTitleAndStatus(st, NULL);
                return 0;
            }

            if (nm->hwndFrom == st->hSymTree) {
                if (nm->code == TVN_SELCHANGEDW || nm->code == TVN_SELCHANGEDA || nm->code == NM_DBLCLK) {
                    CborgSymTree_JumpToSelected(st->hSymTree, st->hFrame);
                    return 0;
                }
            }

            if (nm->hwndFrom == st->hTree) {
                if (nm->code == TVN_DELETEITEMW || nm->code == TVN_DELETEITEMA) {
                    const NMTREEVIEWW *tvDel = (const NMTREEVIEWW *)lParam;
                    if (tvDel && tvDel->itemOld.lParam) {
                        struct {
                            wchar_t *fullPath;
                            BOOL     isFolder;
                            BOOL     loaded;
                        } *meta = (void *)tvDel->itemOld.lParam;
                        if (meta->fullPath) {
                            free(meta->fullPath);
                        }
                        free(meta);
                    }
                    return 0;
                }

                if (nm->code == TVN_ITEMEXPANDINGW || nm->code == TVN_ITEMEXPANDINGA) {
                    const NMTREEVIEWW *tvExp = (const NMTREEVIEWW *)lParam;
                    if (tvExp && tvExp->action == TVE_EXPAND && tvExp->itemNew.hItem) {
                        CborgTree_ExpandDirectory(st->hTree, tvExp->itemNew.hItem);
                    }
                    return 0;
                }

                if (nm->code == TVN_SELCHANGEDW || nm->code == TVN_SELCHANGEDA) {
                    char pathBuf[MAX_PATH];
                    BOOL isFolder = FALSE;
                    if (CborgTree_GetSelectedPath(st->hTree, pathBuf, MAX_PATH, &isFolder)) {
                        lstrcpynA(st->selectedNode, pathBuf, MAX_PATH);
                        Cborg_RefreshTitleAndStatus(st, isFolder ? "Folder / Drive" : "File");
                    }
                    return 0;
                }

                if (nm->code == NM_DBLCLK) {
                    DWORD msgPos = GetMessagePos();
                    POINT pt;
                    TVHITTESTINFO ht;
                    HTREEITEM hHit = NULL;

                    pt.x = (SHORT)LOWORD(msgPos);
                    pt.y = (SHORT)HIWORD(msgPos);
                    ScreenToClient(st->hTree, &pt);

                    ZeroMemory(&ht, sizeof(ht));
                    ht.pt = pt;
                    hHit = (HTREEITEM)SendMessageW(st->hTree, TVM_HITTEST, 0, (LPARAM)&ht);
                    if (!hHit) {
                        hHit = (HTREEITEM)SendMessageW(st->hTree, TVM_GETNEXTITEM, TVGN_CARET, 0);
                    }

                    if (hHit) {
                        TVITEMW it;
                        SendMessageW(st->hTree, TVM_SELECTITEM, TVGN_CARET, (LPARAM)hHit);

                        ZeroMemory(&it, sizeof(it));
                        it.mask  = TVIF_PARAM;
it.hItem = hHit;
if (SendMessageW(st->hTree, TVM_GETITEMW, 0, (LPARAM)&it) && it.lParam) {
const struct {
wchar_t *fullPath;
BOOL     isFolder;
BOOL     loaded;
} *meta = (const void *)it.lParam;
if (meta->fullPath && meta->fullPath != L'\0') {
char utf8Path[MAX_PATH];
WideCharToMultiByte(CP_UTF8, 0, meta->fullPath, -1, utf8Path, MAX_PATH, NULL, NULL);
lstrcpynA(st->selectedNode, utf8Path, MAX_PATH);
if (meta->isFolder) {
CborgTree_ExpandDirectory(st->hTree, hHit);
SendMessageW(st->hTree, TVM_EXPAND, TVE_EXPAND, (LPARAM)hHit);
Cborg_RefreshTitleAndStatus(st, "Expanded");
} else if (Cborg_PromptSaveIfDirty(st)) {
Cborg_OpenFilePath(st, utf8Path);
}
}
}
}
return 0;
}
}
break;
}
case WM_CLOSE:
if (st && !Cborg_PromptSaveIfDirty(st)) {
return 0;
}
DestroyWindow(hwnd);
return 0;
case WM_DESTROY:
if (st) {
if (st->hFrame && IsWindow(st->hFrame)) {
EditorFrame_Destroy(st->hFrame);
st->hFrame = NULL;
}
if (st->hFont) {
DeleteObject(st->hFont);
st->hFont = NULL;
}
HeapFree(GetProcessHeap(), 0, st);
SetWindowLongPtrA(hwnd, GWLP_USERDATA, 0);
}
PostQuitMessage(0);
return 0;
}
return DefWindowProcA(hwnd, msg, wParam, lParam);
}
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
WNDCLASSEXA wc;
HWND hwnd;
MSG msg;
(void)hPrevInstance;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = Cborg_WndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(101), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    wc.hIconSm       = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(101), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = CBORG_CLASS_NAME;

if (!RegisterClassExA(&wc)) {
MessageBoxA(NULL, "Failed to register Cborg window class.", "Error", MB_ICONERROR);
return 1;
}
hwnd = CreateWindowExA(
0,
CBORG_CLASS_NAME,
CBORG_WINDOW_TITLE,
WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
CW_USEDEFAULT, CW_USEDEFAULT, 1080, 680,
NULL,
Cborg_BuildMenuBar(),
hInstance,
NULL
);
if (!hwnd) {
MessageBoxA(NULL, "Failed to create Cborg window.", "Error", MB_ICONERROR);
return 1;
}
if (lpCmdLine && lpCmdLine != '\0') {
CBORG_STATE *st = (CBORG_STATE *)(LONG_PTR)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
char cleanPath[MAX_PATH];
int len = lstrlenA(lpCmdLine);
lstrcpynA(cleanPath, lpCmdLine, MAX_PATH);
if (len >= 2 && cleanPath[0] == '"' && cleanPath[len - 1] == '"') {
cleanPath[len - 1] = '\0';
memmove(cleanPath, cleanPath + 1, (size_t)len - 1);
}
if (st && cleanPath != '\0') {
Cborg_OpenFilePath(st, cleanPath);
}
}
ShowWindow(hwnd, nCmdShow);
UpdateWindow(hwnd);
while (GetMessageA(&msg, NULL, 0, 0) > 0) {
TranslateMessage(&msg);
DispatchMessageA(&msg);
}
return (int)msg.wParam;
}
static void Cborg_Layout(CBORG_STATE *st, int width, int height) {
    const int barH = 34;
    const int statusH = 22;
    const int pad = 8;
    int x = pad;
    int y = 5;
    int btnW = 72;
    int btnH = 24;
    int gap = 6;
    int bodyTop = barH;
    int bodyH;
    int treeW;
    int frameX;
    int frameW;

    if (!st) return;
    if (width < 200) width = 200;
    if (height < 160) height = 160;

    if (st->btnNew)    MoveWindow(st->btnNew,    x, y, btnW,      btnH, TRUE); x += btnW + gap;
    if (st->btnOpen)   MoveWindow(st->btnOpen,   x, y, btnW,      btnH, TRUE); x += btnW + gap;
    if (st->btnSave)   MoveWindow(st->btnSave,   x, y, btnW,      btnH, TRUE); x += btnW + gap;
    if (st->btnSaveAs) MoveWindow(st->btnSaveAs, x, y, btnW + 10, btnH, TRUE); x += (btnW + 10) + gap;
    if (st->btnTheme)  MoveWindow(st->btnTheme,  x, y, btnW,      btnH, TRUE); x += btnW + gap;
    if (st->btnLang)   MoveWindow(st->btnLang,   x, y, btnW,      btnH, TRUE);

    bodyH = height - barH - statusH - pad;
    if (bodyH < 40) bodyH = 40;

    treeW = st->treeWidth > 100 ? st->treeWidth : 210;
    if (treeW > width / 2) treeW = width / 2;

    if (st->hTree && IsWindow(st->hTree)) {
        int fileTreeH = (bodyH * 62) / 100;
        int symTreeY  = bodyTop + fileTreeH + gap;
        int symTreeH  = bodyH - fileTreeH - gap;
        if (symTreeH < 60) symTreeH = 60;

        MoveWindow(st->hTree, pad, bodyTop, treeW, fileTreeH, TRUE);
        if (st->hSymTree && IsWindow(st->hSymTree)) {
            MoveWindow(st->hSymTree, pad, symTreeY, treeW, symTreeH, TRUE);
        }
    }


    frameX = pad + treeW + gap;
    frameW = width - frameX - pad;
    if (frameW < 60) frameW = 60;

    if (st->hFrame && IsWindow(st->hFrame)) {
        MoveWindow(st->hFrame, frameX, bodyTop, frameW, bodyH, TRUE);
    }

    if (st->hStatus && IsWindow(st->hStatus)) {
        MoveWindow(st->hStatus, pad, height - statusH - 2, width - (pad * 2), statusH, TRUE);
    }
}
