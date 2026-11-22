#include "editor_host.h"
#include "Scintilla.h"

static void Editor_FormatFileTitle(const EDITOR_HOST_STATE *st, char *outTitle, size_t cchOut) {
    const char *baseName = "Untitled";
    const char *p;

    if (st->currentFilePath[0] != '\0') {
        baseName = st->currentFilePath;
        for (p = st->currentFilePath; *p != '\0'; ++p) {
            if (*p == '\\' || *p == '/') {
                baseName = p + 1;
            }
        }
    }

    _snprintf_s(
        outTitle,
        cchOut,
        _TRUNCATE,
        "%s%s - %s",
        baseName,
        st->isDirty ? " *" : "",
        HOST_WINDOW_TITLE
    );
}

void Editor_GetExeDir(char *outDir, DWORD cchOut) {
    char exePath[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, exePath, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        char *lastSlash = exePath + len;
        while (lastSlash > exePath && *lastSlash != '\\' && *lastSlash != '/') {
            --lastSlash;
        }
        if (lastSlash > exePath) {
            *lastSlash = '\0';
            lstrcpynA(outDir, exePath, (int)cchOut);
            return;
        }
    }
    lstrcpynA(outDir, ".", (int)cchOut);
}

void Editor_ResolveSiblingPath(const char *fileName, char *outPath, DWORD cchOut) {
    char exeDir[MAX_PATH];
    Editor_GetExeDir(exeDir, MAX_PATH);
    if ((size_t)(lstrlenA(exeDir) + 1 + lstrlenA(fileName) + 1) <= (size_t)cchOut) {
        lstrcpyA(outPath, exeDir);
        lstrcatA(outPath, "\\");
        lstrcatA(outPath, fileName);
        return;
    }
    lstrcpynA(outPath, fileName, (int)cchOut);
}

BOOL Editor_LoadControlDll(EDITOR_HOST_STATE *st) {
    char dllPath[MAX_PATH];
    char msgBuf[512];
    DWORD err = 0;

    if (!st) {
        return FALSE;
    }

    Editor_GetExeDir(st->exeDir, MAX_PATH);
    SetDllDirectoryA(st->exeDir);
    Editor_ResolveSiblingPath(CTRL_DLL_NAME, dllPath, MAX_PATH);

    st->hCtrlDll = LoadLibraryA(dllPath);
    if (!st->hCtrlDll) {
        st->hCtrlDll = LoadLibraryA(CTRL_DLL_NAME);
    }
    if (!st->hCtrlDll) {
        err = GetLastError();
    }

    SetDllDirectoryA(NULL);

    if (!st->hCtrlDll) {
        _snprintf_s(
            msgBuf,
            sizeof(msgBuf),
            _TRUNCATE,
            "Failed to load %s (Win32 error %lu).\nExpected path: %s",
            CTRL_DLL_NAME,
            (unsigned long)err,
            dllPath
        );
        MessageBoxA(st->hwndMain, msgBuf, "DLL Load Error", MB_ICONERROR);
        return FALSE;
    }

    return TRUE;
}

void Editor_UnloadControlDll(EDITOR_HOST_STATE *st) {
    if (!st) {
        return;
    }
    if (st->hwndCtrl && IsWindow(st->hwndCtrl)) {
        DestroyWindow(st->hwndCtrl);
        st->hwndCtrl = NULL;
        st->hwndSci = NULL;
    }
    if (st->hCtrlDll) {
        FreeLibrary(st->hCtrlDll);
        st->hCtrlDll = NULL;
    }
}

void Editor_UpdateWindowTitleAndStatus(EDITOR_HOST_STATE *st, const char *extraNote) {
    char titleBuf[MAX_PATH + 128];
    char statusBuf[STATUS_TEXT_MAX];
    int line = 1, col = 1, lines = 1, chars = 0;
    const char *themeStr = (st->themeMode == EDITOR_THEME_OBSIDIAN) ? "OBSIDIAN" : "LIGHT";
    const char *langStr = "C";

    if (!st) {
        return;
    }

    Editor_FormatFileTitle(st, titleBuf, sizeof(titleBuf));
    if (st->hwndMain && IsWindow(st->hwndMain)) {
        SetWindowTextA(st->hwndMain, titleBuf);
    }

    switch (st->langMode) {
        case EDITOR_LANG_PYTHON: langStr = "PYTHON"; break;
        case EDITOR_LANG_JSON:   langStr = "JSON";   break;
        case EDITOR_LANG_BATCH:  langStr = "BATCH";  break;
        case EDITOR_LANG_PLAIN:  langStr = "TEXT";   break;
        case EDITOR_LANG_C:
        default:                 langStr = "C";      break;
    }

    SciEngine_GetCaretStats(st->hwndSci, &line, &col, &lines, &chars);

    _snprintf_s(
        statusBuf,
        sizeof(statusBuf),
        _TRUNCATE,
        "Ln %d, Col %d | Lines: %d | Chars: %d | %s | %s%s%s",
        line,
        col,
        lines,
        chars,
        langStr,
        themeStr,
        (extraNote && extraNote[0] != '\0') ? " | " : "",
        (extraNote && extraNote[0] != '\0') ? extraNote : ""
    );

    lstrcpynA(st->lastStatus, statusBuf, STATUS_TEXT_MAX);
    if (st->hwndStatus && IsWindow(st->hwndStatus)) {
        SetWindowTextA(st->hwndStatus, st->lastStatus);
    }
}

BOOL Editor_PromptSaveIfDirty(EDITOR_HOST_STATE *st) {
    int ans;
    if (!st || !st->isDirty) {
        return TRUE;
    }
    ans = MessageBoxA(
        st->hwndMain,
        "The current buffer has unsaved changes. Save before continuing?",
        HOST_WINDOW_TITLE,
        MB_YESNOCANCEL | MB_ICONQUESTION
    );
    if (ans == IDCANCEL) {
        return FALSE;
    }
    if (ans == IDYES) {
        return Editor_SaveFile(st, FALSE);
    }
    return TRUE;
}

BOOL Editor_NewDocument(EDITOR_HOST_STATE *st) {
    if (!st || !Editor_PromptSaveIfDirty(st)) {
        return FALSE;
    }
    st->currentFilePath[0] = '\0';
    st->isDirty = FALSE;
    SciEngine_InsertStarterTemplate(st->hwndSci, st->langMode, st->themeMode);
    Editor_UpdateWindowTitleAndStatus(st, "New buffer ready");
    return TRUE;
}

static EDITOR_LANG_MODE Editor_DetectLangFromExtension(const char *path) {
    const char *dot = NULL;
    const char *p;
    if (!path || path[0] == '\0') {
        return EDITOR_LANG_C;
    }
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

BOOL Editor_OpenFilePath(EDITOR_HOST_STATE *st, const char *path) {
    HANDLE hFile;
    LARGE_INTEGER fileSize;
    DWORD bytesRead = 0;
    char *buf;

    if (!st || !path || path[0] == '\0') {
        return FALSE;
    }

    hFile = CreateFileA(
        path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxA(st->hwndMain, "Unable to open selected file.", "Open Error", MB_ICONERROR);
        return FALSE;
    }

    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart < 0 || fileSize.QuadPart >= SCI_BUF_MAX) {
        CloseHandle(hFile);
        MessageBoxA(st->hwndMain, "File is empty or exceeds editor buffer limit.", "Open Error", MB_ICONWARNING);
        return FALSE;
    }

    buf = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)fileSize.QuadPart + 1);
    if (!buf) {
        CloseHandle(hFile);
        return FALSE;
    }

    if (!ReadFile(hFile, buf, (DWORD)fileSize.QuadPart, &bytesRead, NULL)) {
        HeapFree(GetProcessHeap(), 0, buf);
        CloseHandle(hFile);
        MessageBoxA(st->hwndMain, "Failed while reading file contents.", "Open Error", MB_ICONERROR);
        return FALSE;
    }
    buf[bytesRead] = '\0';
    CloseHandle(hFile);

    lstrcpynA(st->currentFilePath, path, MAX_PATH);
    st->isDirty = FALSE;
    st->langMode = Editor_DetectLangFromExtension(path);

    SciEngine_SetBufferText(st->hwndSci, buf);
    SciEngine_ApplyLexer(st->hwndSci, st->langMode, st->themeMode);
    HeapFree(GetProcessHeap(), 0, buf);

    Editor_UpdateWindowTitleAndStatus(st, "File loaded");
    return TRUE;
}

BOOL Editor_OpenFileDialog(EDITOR_HOST_STATE *st) {
    OPENFILENAMEA ofn;
    char fileBuf[MAX_PATH];

    if (!st || !Editor_PromptSaveIfDirty(st)) {
        return FALSE;
    }

    fileBuf[0] = '\0';
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = st->hwndMain;
    ofn.lpstrFilter = "All Supported (*.c;*.h;*.py;*.json;*.bat;*.txt)\0*.c;*.h;*.cpp;*.hpp;*.py;*.json;*.bat;*.cmd;*.txt;*.md\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if (!GetOpenFileNameA(&ofn)) {
        return FALSE;
    }

    return Editor_OpenFilePath(st, fileBuf);
}

BOOL Editor_SaveFile(EDITOR_HOST_STATE *st, BOOL saveAs) {
    char targetPath[MAX_PATH];
    char *textBuf = NULL;
    size_t textLen = 0;
    HANDLE hFile;
    DWORD written = 0;

    if (!st) {
        return FALSE;
    }

    targetPath[0] = '\0';
    if (!saveAs && st->currentFilePath[0] != '\0') {
        lstrcpynA(targetPath, st->currentFilePath, MAX_PATH);
    } else {
        OPENFILENAMEA ofn;
        lstrcpynA(targetPath, st->currentFilePath[0] ? st->currentFilePath : "untitled.c", MAX_PATH);
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = st->hwndMain;
        ofn.lpstrFilter = "C Source (*.c)\0*.c\0Header (*.h)\0*.h\0Python (*.py)\0*.py\0JSON (*.json)\0*.json\0Batch (*.bat)\0*.bat\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = targetPath;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (!GetSaveFileNameA(&ofn)) {
            return FALSE;
        }
    }

    textBuf = SciEngine_GetBufferTextAlloc(st->hwndSci, &textLen);
    if (!textBuf) {
        MessageBoxA(st->hwndMain, "Failed to read editor text buffer.", "Save Error", MB_ICONERROR);
        return FALSE;
    }

    hFile = CreateFileA(
        targetPath,
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        SciEngine_FreeBufferText(textBuf);
        MessageBoxA(st->hwndMain, "Failed to open target file for writing.", "Save Error", MB_ICONERROR);
        return FALSE;
    }

    if (textLen > 0) {
        if (!WriteFile(hFile, textBuf, (DWORD)textLen, &written, NULL) || written != (DWORD)textLen) {
            CloseHandle(hFile);
            SciEngine_FreeBufferText(textBuf);
            MessageBoxA(st->hwndMain, "Failed while writing file bytes.", "Save Error", MB_ICONERROR);
            return FALSE;
        }
    }

    CloseHandle(hFile);
    SciEngine_FreeBufferText(textBuf);

    lstrcpynA(st->currentFilePath, targetPath, MAX_PATH);
    st->isDirty = FALSE;
    st->saveCount++;
    SendMessageA(st->hwndSci, SCI_SETSAVEPOINT, 0, 0);
    Editor_UpdateWindowTitleAndStatus(st, "Saved");
    return TRUE;
}

BOOL Editor_FindNextText(EDITOR_HOST_STATE *st, const char *needle, BOOL interactivePrompt) {
    int caretPos;
    BOOL found;

    if (!st || !st->hwndSci) {
        return FALSE;
    }

    if (interactivePrompt) {
        if (st->findQuery[0] == '\0') {
            lstrcpynA(st->findQuery, "main", FIND_TEXT_MAX);
        }
    }

    if (!needle || needle[0] == '\0') {
        return FALSE;
    }

    lstrcpynA(st->findQuery, needle, FIND_TEXT_MAX);
    caretPos = (int)SendMessageA(st->hwndSci, SCI_GETCURRENTPOS, 0, 0);
    found = SciEngine_FindAndSelectNext(st->hwndSci, st->findQuery, caretPos);
    st->searchCount++;

    SetFocus(st->hwndSci);
    Editor_UpdateWindowTitleAndStatus(st, found ? "Match selected" : "No match found");
    return found;
}

HMENU Editor_BuildMenuBar(void) {
    HMENU hMenuBar = CreateMenu();
    HMENU hFile = CreatePopupMenu();
    HMENU hEdit = CreatePopupMenu();
    HMENU hView = CreatePopupMenu();
    HMENU hLang = CreatePopupMenu();

    AppendMenuA(hFile, MF_STRING, IDM_FILE_NEW, "&New\tCtrl+N");
    AppendMenuA(hFile, MF_STRING, IDM_FILE_OPEN, "&Open...\tCtrl+O");
    AppendMenuA(hFile, MF_STRING, IDM_FILE_SAVE, "&Save\tCtrl+S");
    AppendMenuA(hFile, MF_STRING, IDM_FILE_SAVEAS, "Save &As...");
    AppendMenuA(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hFile, MF_STRING, IDM_FILE_EXIT, "E&xit");

    AppendMenuA(hEdit, MF_STRING, IDM_EDIT_UNDO, "&Undo\tCtrl+Z");
    AppendMenuA(hEdit, MF_STRING, IDM_EDIT_REDO, "&Redo\tCtrl+Y");
    AppendMenuA(hEdit, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hEdit, MF_STRING, IDM_EDIT_CUT, "Cu&t\tCtrl+X");
    AppendMenuA(hEdit, MF_STRING, IDM_EDIT_COPY, "&Copy\tCtrl+C");
    AppendMenuA(hEdit, MF_STRING, IDM_EDIT_PASTE, "&Paste\tCtrl+V");
    AppendMenuA(hEdit, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hEdit, MF_STRING, IDM_EDIT_SELECTALL, "Select &All\tCtrl+A");
    AppendMenuA(hEdit, MF_STRING, IDM_EDIT_FIND, "&Find Next\tF3");

    AppendMenuA(hView, MF_STRING, IDM_VIEW_THEME_OBSIDIAN, "&Obsidian Dark Theme");
    AppendMenuA(hView, MF_STRING, IDM_VIEW_THEME_LIGHT, "&Light Theme");
    AppendMenuA(hView, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hView, MF_STRING, IDM_VIEW_WORDWRAP, "&Word Wrap");
    AppendMenuA(hView, MF_STRING, IDM_VIEW_WHITESPACE, "Show &Whitespace");
    AppendMenuA(hView, MF_STRING, IDM_VIEW_EOL, "Show &EOL");
    AppendMenuA(hView, MF_STRING, IDM_VIEW_INDENTGUIDES, "Show &Indent Guides");

    AppendMenuA(hLang, MF_STRING, IDM_LANG_C, "&C / C++");
    AppendMenuA(hLang, MF_STRING, IDM_LANG_PYTHON, "&Python");
    AppendMenuA(hLang, MF_STRING, IDM_LANG_JSON, "&JSON");
    AppendMenuA(hLang, MF_STRING, IDM_LANG_BATCH, "&Batch (.bat)");
    AppendMenuA(hLang, MF_STRING, IDM_LANG_PLAIN, "&Plain Text");

    AppendMenuA(hMenuBar, MF_POPUP, (UINT_PTR)hFile, "&File");
    AppendMenuA(hMenuBar, MF_POPUP, (UINT_PTR)hEdit, "&Edit");
    AppendMenuA(hMenuBar, MF_POPUP, (UINT_PTR)hView, "&View");
    AppendMenuA(hMenuBar, MF_POPUP, (UINT_PTR)hLang, "&Language");

    return hMenuBar;
}

static HWND Editor_MakeButton(HWND parent, HINSTANCE hInst, const char *label, int id) {
    HWND btn = CreateWindowExA(
        0,
        "BUTTON",
        label,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        0, 0, 76, 24,
        parent,
        (HMENU)(UINT_PTR)id,
        hInst,
        NULL
    );
    return btn;
}

void Editor_CreateChromeControls(EDITOR_HOST_STATE *st) {
    HINSTANCE hInst;

    if (!st || !st->hwndMain) {
        return;
    }

    hInst = (HINSTANCE)GetWindowLongPtrA(st->hwndMain, GWLP_HINSTANCE);
    st->hUiFont = CreateFontA(
        -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI"
    );
    st->hChromeBrush = CreateSolidBrush(RGB(22, 27, 36));

    st->btnOpen   = Editor_MakeButton(st->hwndMain, hInst, "Open", IDC_TOOLBAR_OPEN);
    st->btnSave   = Editor_MakeButton(st->hwndMain, hInst, "Save", IDC_TOOLBAR_SAVE);
    st->btnSaveAs = Editor_MakeButton(st->hwndMain, hInst, "Save As", IDC_TOOLBAR_SAVEAS);
    st->btnFind   = Editor_MakeButton(st->hwndMain, hInst, "Find Next", IDC_TOOLBAR_FIND);
    st->btnTheme  = Editor_MakeButton(st->hwndMain, hInst, "Theme", IDC_TOOLBAR_THEME);
    st->btnLang   = Editor_MakeButton(st->hwndMain, hInst, "Lang", IDC_TOOLBAR_LANG);

    st->hwndStatus = CreateWindowExA(
        0,
        "STATIC",
        "",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 400, 22,
        st->hwndMain,
        (HMENU)(UINT_PTR)IDC_STATUS_BAR,
        hInst,
        NULL
    );

    if (st->hUiFont) {
        HWND ctrls[7];
        int i;
        ctrls[0] = st->btnOpen;
        ctrls[1] = st->btnSave;
        ctrls[2] = st->btnSaveAs;
        ctrls[3] = st->btnFind;
        ctrls[4] = st->btnTheme;
        ctrls[5] = st->btnLang;
        ctrls[6] = st->hwndStatus;
        for (i = 0; i < 7; ++i) {
            if (ctrls[i]) {
                SendMessageA(ctrls[i], WM_SETFONT, (WPARAM)st->hUiFont, TRUE);
            }
        }
    }
}

void Editor_LayoutChildWindows(EDITOR_HOST_STATE *st, int width, int height) {
    const int barH = 32;
    const int statusH = 22;
    int btnW = 74;
    int btnH = 24;
    int gap = 6;
    int x = 8;
    int y = 4;
    int editorTop;
    int editorH;

    if (!st) {
        return;
    }
    if (width < 100) width = 100;
    if (height < 100) height = 100;

    editorTop = barH;
    editorH = height - barH - statusH;
    if (editorH < 20) editorH = 20;

    if (st->btnOpen)   MoveWindow(st->btnOpen,   x, y, btnW, btnH, TRUE); x += btnW + gap;
    if (st->btnSave)   MoveWindow(st->btnSave,   x, y, btnW, btnH, TRUE); x += btnW + gap;
    if (st->btnSaveAs) MoveWindow(st->btnSaveAs, x, y, btnW, btnH, TRUE); x += btnW + gap;
    if (st->btnFind)   MoveWindow(st->btnFind,   x, y, btnW + 10, btnH, TRUE); x += btnW + 10 + gap;
    if (st->btnTheme)  MoveWindow(st->btnTheme,  x, y, btnW, btnH, TRUE); x += btnW + gap;
    if (st->btnLang)   MoveWindow(st->btnLang,   x, y, btnW, btnH, TRUE);

    if (st->hwndCtrl) {
        MoveWindow(st->hwndCtrl, 0, editorTop, width, editorH, TRUE);
    }

    if (st->hwndStatus) {
        MoveWindow(st->hwndStatus, 6, height - statusH + 2, width - 12, statusH - 3, TRUE);
    }
}

void Editor_HandleCommand(EDITOR_HOST_STATE *st, WORD cmdId) {
    if (!st || !st->hwndSci) {
        return;
    }

    switch (cmdId) {
        case IDM_FILE_NEW:
            Editor_NewDocument(st);
            break;
        case IDM_FILE_OPEN:
        case IDC_TOOLBAR_OPEN:
            Editor_OpenFileDialog(st);
            break;
        case IDM_FILE_SAVE:
        case IDC_TOOLBAR_SAVE:
            Editor_SaveFile(st, FALSE);
            break;
        case IDM_FILE_SAVEAS:
        case IDC_TOOLBAR_SAVEAS:
            Editor_SaveFile(st, TRUE);
            break;
        case IDM_FILE_EXIT:
            PostMessageA(st->hwndMain, WM_CLOSE, 0, 0);
            break;

        case IDM_EDIT_UNDO:
            SendMessageA(st->hwndSci, SCI_UNDO, 0, 0);
            break;
        case IDM_EDIT_REDO:
            SendMessageA(st->hwndSci, SCI_REDO, 0, 0);
            break;
        case IDM_EDIT_CUT:
            SendMessageA(st->hwndSci, SCI_CUT, 0, 0);
            break;
        case IDM_EDIT_COPY:
            SendMessageA(st->hwndSci, SCI_COPY, 0, 0);
            break;
        case IDM_EDIT_PASTE:
            SendMessageA(st->hwndSci, SCI_PASTE, 0, 0);
            break;
        case IDM_EDIT_SELECTALL:
            SendMessageA(st->hwndSci, SCI_SELECTALL, 0, 0);
            break;
        case IDM_EDIT_FIND:
        case IDC_TOOLBAR_FIND:
            Editor_FindNextText(st, st->findQuery[0] ? st->findQuery : "main", TRUE);
            break;

        case IDM_VIEW_THEME_OBSIDIAN:
            st->themeMode = EDITOR_THEME_OBSIDIAN;
            SciEngine_ApplyLexer(st->hwndSci, st->langMode, st->themeMode);
            SciEngine_ApplyViewFlags(st->hwndSci, st->wordWrap, st->showWhitespace, st->showEol, st->showIndentGuides);
            Editor_UpdateWindowTitleAndStatus(st, "Obsidian theme active");
            break;
        case IDM_VIEW_THEME_LIGHT:
            st->themeMode = EDITOR_THEME_LIGHT;
            SciEngine_ApplyLexer(st->hwndSci, st->langMode, st->themeMode);
            SciEngine_ApplyViewFlags(st->hwndSci, st->wordWrap, st->showWhitespace, st->showEol, st->showIndentGuides);
            Editor_UpdateWindowTitleAndStatus(st, "Light theme active");
            break;
        case IDC_TOOLBAR_THEME:
            st->themeMode = (st->themeMode == EDITOR_THEME_OBSIDIAN) ? EDITOR_THEME_LIGHT : EDITOR_THEME_OBSIDIAN;
            SciEngine_ApplyLexer(st->hwndSci, st->langMode, st->themeMode);
            SciEngine_ApplyViewFlags(st->hwndSci, st->wordWrap, st->showWhitespace, st->showEol, st->showIndentGuides);
            Editor_UpdateWindowTitleAndStatus(st, "Theme toggled");
            break;

        case IDM_VIEW_WORDWRAP:
            st->wordWrap = !st->wordWrap;
            SciEngine_ApplyViewFlags(st->hwndSci, st->wordWrap, st->showWhitespace, st->showEol, st->showIndentGuides);
            Editor_UpdateWindowTitleAndStatus(st, st->wordWrap ? "Word wrap ON" : "Word wrap OFF");
            break;
        case IDM_VIEW_WHITESPACE:
            st->showWhitespace = !st->showWhitespace;
            SciEngine_ApplyViewFlags(st->hwndSci, st->wordWrap, st->showWhitespace, st->showEol, st->showIndentGuides);
            Editor_UpdateWindowTitleAndStatus(st, st->showWhitespace ? "Whitespace ON" : "Whitespace OFF");
            break;
        case IDM_VIEW_EOL:
            st->showEol = !st->showEol;
            SciEngine_ApplyViewFlags(st->hwndSci, st->wordWrap, st->showWhitespace, st->showEol, st->showIndentGuides);
            Editor_UpdateWindowTitleAndStatus(st, st->showEol ? "EOL ON" : "EOL OFF");
            break;
        case IDM_VIEW_INDENTGUIDES:
            st->showIndentGuides = !st->showIndentGuides;
            SciEngine_ApplyViewFlags(st->hwndSci, st->wordWrap, st->showWhitespace, st->showEol, st->showIndentGuides);
            Editor_UpdateWindowTitleAndStatus(st, st->showIndentGuides ? "Indent guides ON" : "Indent guides OFF");
            break;

        case IDM_LANG_C:
            st->langMode = EDITOR_LANG_C;
            SciEngine_ApplyLexer(st->hwndSci, st->langMode, st->themeMode);
            Editor_UpdateWindowTitleAndStatus(st, "Lexer: C/C++");
            break;
        case IDM_LANG_PYTHON:
            st->langMode = EDITOR_LANG_PYTHON;
            SciEngine_ApplyLexer(st->hwndSci, st->langMode, st->themeMode);
            Editor_UpdateWindowTitleAndStatus(st, "Lexer: Python");
            break;
        case IDM_LANG_JSON:
            st->langMode = EDITOR_LANG_JSON;
            SciEngine_ApplyLexer(st->hwndSci, st->langMode, st->themeMode);
            Editor_UpdateWindowTitleAndStatus(st, "Lexer: JSON");
            break;
        case IDM_LANG_BATCH:
            st->langMode = EDITOR_LANG_BATCH;
            SciEngine_ApplyLexer(st->hwndSci, st->langMode, st->themeMode);
            Editor_UpdateWindowTitleAndStatus(st, "Lexer: Batch");
            break;
        case IDM_LANG_PLAIN:
            st->langMode = EDITOR_LANG_PLAIN;
            SciEngine_ApplyLexer(st->hwndSci, st->langMode, st->themeMode);
            Editor_UpdateWindowTitleAndStatus(st, "Lexer: Plain Text");
            break;
        case IDC_TOOLBAR_LANG:
            st->langMode = (EDITOR_LANG_MODE)(((int)st->langMode + 1) % 5);
            SciEngine_ApplyLexer(st->hwndSci, st->langMode, st->themeMode);
            Editor_UpdateWindowTitleAndStatus(st, "Language cycled");
            break;
    }
}

void Editor_HandleNotify(EDITOR_HOST_STATE *st, const NMHDR *nm) {
    const SCNotification *scn;
    if (!st || !nm) {
        return;
    }

    scn = (const SCNotification *)nm;
    switch (scn->nmhdr.code) {
        case SCN_MODIFIED:
            if ((scn->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)) != 0) {
                st->isDirty = TRUE;
                st->editCount++;
                Editor_UpdateWindowTitleAndStatus(st, NULL);
            }
            break;
        case SCN_UPDATEUI:
            Editor_UpdateWindowTitleAndStatus(st, NULL);
            break;
        case SCN_SAVEPOINTLEFT:
            st->isDirty = TRUE;
            Editor_UpdateWindowTitleAndStatus(st, NULL);
            break;
        case SCN_SAVEPOINTREACHED:
            st->isDirty = FALSE;
            Editor_UpdateWindowTitleAndStatus(st, NULL);
            break;
    }
}

static LRESULT CALLBACK Editor_MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    EDITOR_HOST_STATE *st = (EDITOR_HOST_STATE *)(LONG_PTR)GetWindowLongPtrA(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CREATE: {
            RECT rc;

            st = (EDITOR_HOST_STATE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(EDITOR_HOST_STATE));
            if (!st) {
                return -1;
            }
            st->hwndMain = hwnd;
            st->themeMode = EDITOR_THEME_OBSIDIAN;
            st->langMode = EDITOR_LANG_C;
            st->showIndentGuides = TRUE;
            lstrcpynA(st->findQuery, "main", FIND_TEXT_MAX);
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)st);

            if (!Editor_LoadControlDll(st)) {
                HeapFree(GetProcessHeap(), 0, st);
                SetWindowLongPtrA(hwnd, GWLP_USERDATA, 0);
                return -1;
            }

            Editor_CreateChromeControls(st);

            GetClientRect(hwnd, &rc);
            st->hwndCtrl = CreateWindowExA(
                0,
                CTRL_CLASS_NAME,
                "",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN,
                0, 32,
                rc.right > 0 ? rc.right : 960,
                (rc.bottom - 54) > 0 ? (rc.bottom - 54) : 620,
                hwnd,
                (HMENU)(UINT_PTR)IDC_CODE_CTRL,
                GetModuleHandleA(NULL),
                NULL
            );

            if (!st->hwndCtrl) {
                DWORD ctrlErr = GetLastError();
                char errBuf[256];
                _snprintf_s(
                    errBuf,
                    sizeof(errBuf),
                    _TRUNCATE,
                    "Failed to create %s window (Win32 error %lu).",
                    CTRL_CLASS_NAME,
                    (unsigned long)ctrlErr
                );
                MessageBoxA(hwnd, errBuf, "Control Error", MB_ICONERROR);
                Editor_UnloadControlDll(st);
                HeapFree(GetProcessHeap(), 0, st);
                SetWindowLongPtrA(hwnd, GWLP_USERDATA, 0);
                return -1;
            }

            st->hwndSci = SciEngine_FindChildScintilla(st->hwndCtrl);
            if (!st->hwndSci) {
                st->hwndSci = st->hwndCtrl;
            }

            SciEngine_InsertStarterTemplate(st->hwndSci, st->langMode, st->themeMode);
            SciEngine_ApplyViewFlags(st->hwndSci, st->wordWrap, st->showWhitespace, st->showEol, st->showIndentGuides);
            Editor_LayoutChildWindows(st, rc.right, rc.bottom);
            Editor_UpdateWindowTitleAndStatus(st, "Ready");
            SetFocus(st->hwndSci);
            return 0;
        }

        case WM_SIZE:
            if (st) {
                Editor_LayoutChildWindows(st, LOWORD(lParam), HIWORD(lParam));
            }
            return 0;

        case WM_COMMAND:
            if (st) {
                Editor_HandleCommand(st, LOWORD(wParam));
            }
            return 0;

        case WM_NOTIFY:
            if (st) {
                Editor_HandleNotify(st, (const NMHDR *)lParam);
            }
            return 0;

        case WM_SETFOCUS:
            if (st && st->hwndSci) {
                SetFocus(st->hwndSci);
            }
            return 0;

        case WM_CLOSE:
            if (st && !Editor_PromptSaveIfDirty(st)) {
                return 0;
            }
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (st) {
                if (st->hUiFont) {
                    DeleteObject(st->hUiFont);
                    st->hUiFont = NULL;
                }
                if (st->hChromeBrush) {
                    DeleteObject(st->hChromeBrush);
                    st->hChromeBrush = NULL;
                }
                Editor_UnloadControlDll(st);
                HeapFree(GetProcessHeap(), 0, st);
                SetWindowLongPtrA(hwnd, GWLP_USERDATA, 0);
            }
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASSA wc;
    HMENU hMenuBar;
    HWND hwnd;
    MSG msg;

    (void)hPrevInstance;

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = Editor_MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = HOST_CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);

    if (!RegisterClassA(&wc)) {
        MessageBoxA(NULL, "Failed to register host window class.", "Error", MB_ICONERROR);
        return 1;
    }

    hMenuBar = Editor_BuildMenuBar();

    hwnd = CreateWindowExA(
        0,
        HOST_CLASS_NAME,
        HOST_WINDOW_TITLE,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1024, 720,
        NULL,
        hMenuBar,
        hInstance,
        NULL
    );

    if (!hwnd) {
        MessageBoxA(NULL, "Failed to create host window.", "Error", MB_ICONERROR);
        return 1;
    }

    if (lpCmdLine && lpCmdLine[0] != '\0') {
        EDITOR_HOST_STATE *st = (EDITOR_HOST_STATE *)(LONG_PTR)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
        char cleanPath[MAX_PATH];
        int len = lstrlenA(lpCmdLine);
        lstrcpynA(cleanPath, lpCmdLine, MAX_PATH);
        if (len >= 2 && cleanPath[0] == '"' && cleanPath[len - 1] == '"') {
            cleanPath[len - 1] = '\0';
            memmove(cleanPath, cleanPath + 1, (size_t)len - 1);
        }
        if (st && cleanPath[0] != '\0') {
            Editor_OpenFilePath(st, cleanPath);
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