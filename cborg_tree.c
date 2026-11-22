#include "cborg_tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "comctl32.lib")

typedef struct {
    wchar_t *fullPath;
    BOOL     isFolder;
    BOOL     loaded;
} CBORG_NODE_META;

static CBORG_NODE_META *AllocNodeMeta(const wchar_t *path, BOOL isFolder, BOOL loaded) {
    CBORG_NODE_META *meta = (CBORG_NODE_META *)calloc(1, sizeof(CBORG_NODE_META));
    size_t len;
    if (!meta) return NULL;
    meta->isFolder = isFolder;
    meta->loaded   = loaded;
    if (path && path[0] != L'\0') {
        len = (size_t)lstrlenW(path) + 1;
        meta->fullPath = (wchar_t *)calloc(len, sizeof(wchar_t));
        if (meta->fullPath) {
            lstrcpynW(meta->fullPath, path, (int)len);
        }
    }
    return meta;
}

static void FreeNodeMeta(CBORG_NODE_META *meta) {
    if (!meta) return;
    if (meta->fullPath) {
        free(meta->fullPath);
        meta->fullPath = NULL;
    }
    free(meta);
}

static const wchar_t *GetDriveEmojiW(UINT driveType) {
    switch (driveType) {
        case DRIVE_REMOVABLE: return L"\xD83D\xDCBE"; /* 💾 */
        case DRIVE_FIXED:     return L"\xD83D\xDCBD"; /* 💽 */
        case DRIVE_REMOTE:    return L"\xD83C\xDF10"; /* 🌐 */
        case DRIVE_CDROM:     return L"\xD83D\xDCC0"; /* 💿 */
        case DRIVE_RAMDISK:   return L"\x26A1";         /* ⚡ */
        default:              return L"\x2753";         /* ❓ */
    }
}

static const wchar_t *GetFileEmojiW(const wchar_t *name) {
    const wchar_t *dot = NULL;
    const wchar_t *p;
    if (!name) return L"\xD83D\xDCC4";
    for (p = name; *p != L'\0'; ++p) {
        if (*p == L'.') dot = p;
    }
    if (!dot) return L"\xD83D\xDCC4";
    if (_wcsicmp(dot, L".c") == 0 || _wcsicmp(dot, L".h") == 0 ||
        _wcsicmp(dot, L".cpp") == 0 || _wcsicmp(dot, L".hpp") == 0) {
        return L"\x2699\xFE0F"; /* ⚙️ */
    }
    if (_wcsicmp(dot, L".py") == 0) return L"\xD83D\xDC0D"; /* 🐍 */
    if (_wcsicmp(dot, L".json") == 0 || _wcsicmp(dot, L".xml") == 0) return L"\xD83D\xDCCB"; /* 📋 */
    if (_wcsicmp(dot, L".bat") == 0 || _wcsicmp(dot, L".cmd") == 0 || _wcsicmp(dot, L".ps1") == 0) {
        return L"\x2328\xFE0F"; /* ⌨️ */
    }
    return L"\xD83D\xDCC4"; /* 📄 */
}

static void JoinPathW(const wchar_t *base, const wchar_t *child, wchar_t *outPath, int cchOut) {
    size_t baseLen;
    if (!base || !child || !outPath || cchOut <= 0) return;
    outPath[0] = L'\0';
    baseLen = (size_t)lstrlenW(base);
    if (baseLen > 0 && (base[baseLen - 1] == L'\\' || base[baseLen - 1] == L'/')) {
        _snwprintf_s(outPath, (size_t)cchOut, _TRUNCATE, L"%s%s", base, child);
    } else {
        _snwprintf_s(outPath, (size_t)cchOut, _TRUNCATE, L"%s\\%s", base, child);
    }
}

static void DeleteAllChildren(HWND hTree, HTREEITEM hParent) {
    HTREEITEM hChild = TreeView_GetChild(hTree, hParent);
    while (hChild) {
        HTREEITEM hNext = TreeView_GetNextSibling(hTree, hChild);
        TreeView_DeleteItem(hTree, hChild);
        hChild = hNext;
    }
}

static void SetItemHasChildrenFlag(HWND hTree, HTREEITEM hItem, int cChildren) {
    TVITEMW it;
    if (!hTree || !hItem) return;
    ZeroMemory(&it, sizeof(it));
    it.mask      = TVIF_CHILDREN;
    it.hItem     = hItem;
    it.cChildren = cChildren;
    SendMessageW(hTree, TVM_SETITEMW, 0, (LPARAM)&it);
}

HWND CborgTree_Create(
    HWND              hParent,
    HINSTANCE         hInstance,
    UINT              treeId,
    int               x,
    int               y,
    int               width,
    int               height,
    HFONT             hFont
) {
    INITCOMMONCONTROLSEX icc;
    HWND hTree;

    if (!hParent || !IsWindow(hParent)) {
        return NULL;
    }

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

    if (hTree) {
        if (hFont) {
            SendMessageW(hTree, WM_SETFONT, (WPARAM)hFont, TRUE);
        }
        CborgTree_AttachRightClickHook(hTree, hParent);
    }

    return hTree;

    return hTree;

 
}


HTREEITEM CborgTree_AddItemW(
    HWND              hTree,
    HTREEITEM         hParent,
    const wchar_t    *label,
    const wchar_t    *fullPath,
    BOOL              isFolder,
    BOOL              hasChildren,
    BOOL              bold
) {
    TVINSERTSTRUCTW tvis;
    CBORG_NODE_META *meta;
    HTREEITEM hItem;

    if (!hTree || !IsWindow(hTree) || !label) {
        return NULL;
    }

    meta = AllocNodeMeta(fullPath, isFolder, FALSE);

    ZeroMemory(&tvis, sizeof(tvis));
    tvis.hParent        = hParent ? hParent : TVI_ROOT;
    tvis.hInsertAfter   = TVI_LAST;
    tvis.item.mask      = TVIF_TEXT | TVIF_PARAM | TVIF_STATE | TVIF_CHILDREN;
    tvis.item.pszText   = (LPWSTR)label;
    tvis.item.lParam    = (LPARAM)meta;
    tvis.item.cChildren = hasChildren ? 1 : 0;
    tvis.item.stateMask = bold ? TVIS_BOLD : 0;
    tvis.item.state     = bold ? TVIS_BOLD : 0;

    hItem = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tvis);
    if (!hItem && meta) {
        FreeNodeMeta(meta);
    }
    return hItem;
}

void CborgTree_ApplyTheme(HWND hTree, EDITOR_THEME_MODE theme) {
    if (!hTree || !IsWindow(hTree)) {
        return;
    }

    if (theme == EDITOR_THEME_LIGHT) {
        TreeView_SetBkColor(hTree, RGB(244, 246, 250));
        TreeView_SetTextColor(hTree, RGB(24, 28, 36));
    } else {
        TreeView_SetBkColor(hTree, RGB(10, 14, 22));
        TreeView_SetTextColor(hTree, RGB(0, 240, 255));
    }
}

BOOL CborgTree_ExpandDirectory(HWND hTree, HTREEITEM hItem) {
    TVITEMW item;
    CBORG_NODE_META *meta;
    wchar_t pattern[MAX_PATH];
    WIN32_FIND_DATAW ffd;
    HANDLE hFind;
    int added = 0;

    if (!hTree || !IsWindow(hTree) || !hItem) {
        return FALSE;
    }

    ZeroMemory(&item, sizeof(item));
    item.mask  = TVIF_PARAM;
    item.hItem = hItem;
    if (!SendMessageW(hTree, TVM_GETITEMW, 0, (LPARAM)&item)) {
        return FALSE;
    }

    meta = (CBORG_NODE_META *)item.lParam;
    if (!meta || !meta->isFolder || !meta->fullPath || meta->fullPath[0] == L'\0') {
        return FALSE;
    }

    /* Already populated once: keep existing children & expand state stable */
    if (meta->loaded) {
        return TreeView_GetChild(hTree, hItem) != NULL;
    }

    DeleteAllChildren(hTree, hItem);
    JoinPathW(meta->fullPath, L"*", pattern, ARRAYSIZE(pattern));

    hFind = FindFirstFileW(pattern, &ffd);
    if (hFind == INVALID_HANDLE_VALUE) {
        meta->loaded = TRUE;
        SetItemHasChildrenFlag(hTree, hItem, 0);
        return FALSE;
    }

    /* Pass 1: folders */
    do {
        wchar_t childPath[MAX_PATH];
        wchar_t disp[MAX_PATH + 16];
        if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) {
            continue;
        }
        if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            JoinPathW(meta->fullPath, ffd.cFileName, childPath, ARRAYSIZE(childPath));
            _snwprintf_s(disp, ARRAYSIZE(disp), _TRUNCATE, L"\xD83D\xDCC1 %s", ffd.cFileName);
            CborgTree_AddItemW(hTree, hItem, disp, childPath, TRUE, TRUE, FALSE);
            ++added;
        }
    } while (FindNextFileW(hFind, &ffd) != 0);
    FindClose(hFind);

    /* Pass 2: files */
    hFind = FindFirstFileW(pattern, &ffd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            wchar_t childPath[MAX_PATH];
            wchar_t disp[MAX_PATH + 16];
            if (wcscmp(ffd.cFileName, L".") == 0 || wcscmp(ffd.cFileName, L"..") == 0) {
                continue;
            }
            if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                JoinPathW(meta->fullPath, ffd.cFileName, childPath, ARRAYSIZE(childPath));
                _snwprintf_s(disp, ARRAYSIZE(disp), _TRUNCATE, L"%s %s", GetFileEmojiW(ffd.cFileName), ffd.cFileName);
                CborgTree_AddItemW(hTree, hItem, disp, childPath, FALSE, FALSE, FALSE);
                ++added;
            }
        } while (FindNextFileW(hFind, &ffd) != 0);
        FindClose(hFind);
    }

    meta->loaded = TRUE;
    SetItemHasChildrenFlag(hTree, hItem, added > 0 ? 1 : 0);
    return added > 0;
}

int CborgTree_LoadAllDrives(HWND hTree, EDITOR_THEME_MODE theme) {
    wchar_t driveStrings[520];
    HTREEITEM hDrivesRoot;
    int count = 0;

    if (!hTree || !IsWindow(hTree)) {
        return 0;
    }

    SendMessageW(hTree, WM_SETREDRAW, FALSE, 0);
    TreeView_DeleteAllItems(hTree);
    hDrivesRoot = CborgTree_AddItemW(hTree, NULL, L"\xD83D\xDCBB Computer", NULL, FALSE, TRUE, TRUE);

    ZeroMemory(driveStrings, sizeof(driveStrings));
    if (GetLogicalDriveStringsW(518, driveStrings) > 0) {
        const wchar_t *p = driveStrings;
        while (*p != L'\0') {
            UINT drvType = GetDriveTypeW(p);
            wchar_t volName[MAX_PATH] = L"";
            wchar_t label[MAX_PATH + 32];
            const wchar_t *emoji = GetDriveEmojiW(drvType);
            HTREEITEM hDrv;

            GetVolumeInformationW(p, volName, MAX_PATH, NULL, NULL, NULL, NULL, 0);
            if (volName[0] != L'\0') {
                _snwprintf_s(label, MAX_PATH + 32, _TRUNCATE, L"%s %s (%s)", emoji, p, volName);
            } else {
                _snwprintf_s(label, MAX_PATH + 32, _TRUNCATE, L"%s %s", emoji, p);
            }

            hDrv = CborgTree_AddItemW(hTree, hDrivesRoot, label, p, TRUE, TRUE, FALSE);
            if (hDrv && (_wcsicmp(p, L"C:\\") == 0 || count == 0)) {
                CborgTree_ExpandDirectory(hTree, hDrv);
                TreeView_Expand(hTree, hDrv, TVE_EXPAND);
            }
            ++count;
            p += lstrlenW(p) + 1;
        }
    }

    if (hDrivesRoot) {
        TreeView_Expand(hTree, hDrivesRoot, TVE_EXPAND);
    }
    CborgTree_ApplyTheme(hTree, theme);
    SendMessageW(hTree, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hTree, NULL, TRUE);
    return count;
}

void CborgTree_PopulateDefault(HWND hTree, EDITOR_THEME_MODE theme) {
    CborgTree_LoadAllDrives(hTree, theme);
}

BOOL CborgTree_GetSelectedPathW(HWND hTree, wchar_t *outPath, int cchOut, BOOL *outIsFolder) {
    HTREEITEM hSel;
    TVITEMW item;
    CBORG_NODE_META *meta;

    if (outPath && cchOut > 0) outPath[0] = L'\0';
    if (outIsFolder) *outIsFolder = FALSE;
    if (!hTree || !IsWindow(hTree)) return FALSE;

    hSel = TreeView_GetSelection(hTree);
    if (!hSel) return FALSE;

    ZeroMemory(&item, sizeof(item));
    item.mask  = TVIF_PARAM;
    item.hItem = hSel;
    if (!SendMessageW(hTree, TVM_GETITEMW, 0, (LPARAM)&item)) return FALSE;

    meta = (CBORG_NODE_META *)item.lParam;
    if (!meta || !meta->fullPath || meta->fullPath[0] == L'\0') return FALSE;

    if (outPath && cchOut > 0) {
        lstrcpynW(outPath, meta->fullPath, cchOut);
    }
    if (outIsFolder) {
        *outIsFolder = meta->isFolder;
    }
    return TRUE;
}

BOOL CborgTree_GetSelectedPath(HWND hTree, char *outPath, int cchOut, BOOL *outIsFolder) {
    wchar_t wPath[MAX_PATH];
    if (!outPath || cchOut <= 0) return FALSE;
    outPath[0] = '\0';
    if (!CborgTree_GetSelectedPathW(hTree, wPath, MAX_PATH, outIsFolder)) return FALSE;
    WideCharToMultiByte(CP_UTF8, 0, wPath, -1, outPath, cchOut, NULL, NULL);
    return TRUE;
}

BOOL CborgTree_GetSelectedText(HWND hTree, char *outBuf, int cchOut) {
    BOOL isFolder = FALSE;
    return CborgTree_GetSelectedPath(hTree, outBuf, cchOut, &isFolder);
}

void CborgTree_FreeAllPaths(HWND hTree) {
    if (hTree && IsWindow(hTree)) {
        TreeView_DeleteAllItems(hTree);
    }
}
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

/* --- Drop-in action functions --- */

static void Action_OpenInEditor(HWND hTree, HWND hOwnerForm, const wchar_t *fullPath, BOOL isFolder) {
    (void)hTree;
    if (isFolder || !fullPath || fullPath[0] == L'\0') return;
    /* Forward to parent form's existing IDM_TREE_OPEN_FILE handler */
    SendMessageW(hOwnerForm, WM_COMMAND, MAKEWPARAM(IDM_TREE_OPEN_FILE, 0), 0);
}

static void Action_ExpandNode(HWND hTree, HWND hOwnerForm, const wchar_t *fullPath, BOOL isFolder) {
    HTREEITEM hSel = TreeView_GetSelection(hTree);
    (void)hOwnerForm;
    (void)fullPath;
    if (hSel && isFolder) {
        CborgTree_ExpandDirectory(hTree, hSel);
        TreeView_Expand(hTree, hSel, TVE_TOGGLE);
    }
}

static void Action_CopyPath(HWND hTree, HWND hOwnerForm, const wchar_t *fullPath, BOOL isFolder) {
    size_t bytes;
    HGLOBAL hMem;
    wchar_t *dst;
    (void)hTree;
    (void)isFolder;
    if (!fullPath || fullPath[0] == L'\0') return;
    if (!OpenClipboard(hOwnerForm)) return;
    EmptyClipboard();
    bytes = (size_t)(lstrlenW(fullPath) + 1) * sizeof(wchar_t);
    hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        dst = (wchar_t *)GlobalLock(hMem);
        if (dst) {
            memcpy(dst, fullPath, bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        } else {
            GlobalFree(hMem);
        }
    }
    CloseClipboard();
}

static void Action_OpenCmdHere(HWND hTree, HWND hOwnerForm, const wchar_t *fullPath, BOOL isFolder) {
    wchar_t dirPath[MAX_PATH];
    wchar_t params[MAX_PATH + 32];
    (void)hTree;
    if (!fullPath || fullPath[0] == L'\0') return;
    lstrcpynW(dirPath, fullPath, MAX_PATH);
    if (!isFolder) {
        wchar_t *slash = wcsrchr(dirPath, L'\\');
        if (slash && slash > dirPath) {
            if (slash == dirPath + 2 && dirPath[1] == L':') {
                *(slash + 1) = L'\0'; /* Keep "C:\" */
            } else {
                *slash = L'\0';
            }
        }
    }
    _snwprintf_s(params, ARRAYSIZE(params), _TRUNCATE, L"/K cd /d \"%s\"", dirPath);
    ShellExecuteW(hOwnerForm, L"open", L"cmd.exe", params, dirPath, SW_SHOWNORMAL);
}

static void Action_RefreshDrives(HWND hTree, HWND hOwnerForm, const wchar_t *fullPath, BOOL isFolder) {
    (void)hOwnerForm;
    (void)fullPath;
    (void)isFolder;
    CborgTree_LoadAllDrives(hTree, EDITOR_THEME_OBSIDIAN);
}
static LRESULT CALLBACK CborgTree_SubclassProc(
    HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData
) {
    (void)uIdSubclass;
    switch (uMsg) {
        case WM_RBUTTONDOWN: {
            TVHITTESTINFO ht;
            ZeroMemory(&ht, sizeof(ht));
            ht.pt.x = (SHORT)LOWORD(lParam);
            ht.pt.y = (SHORT)HIWORD(lParam);
            if (TreeView_HitTest(hwnd, &ht) && ht.hItem) {
                TreeView_SelectItem(hwnd, ht.hItem);
            }
            SetFocus(hwnd);
            return 0;
        }
        case WM_RBUTTONUP: {
            POINT pt;
            pt.x = (SHORT)LOWORD(lParam);
            pt.y = (SHORT)HIWORD(lParam);
            ClientToScreen(hwnd, &pt);
            CborgTree_ShowContextMenu(hwnd, (HWND)dwRefData, pt);
            return 0;
        }
        case WM_CONTEXTMENU: {
            if ((int)(SHORT)LOWORD(lParam) == -1 && (int)(SHORT)HIWORD(lParam) == -1) {
                POINT pt = { -1, -1 };
                CborgTree_ShowContextMenu(hwnd, (HWND)dwRefData, pt);
                return 0;
            }
            break;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, CborgTree_SubclassProc, uIdSubclass);
            break;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}
static void Action_RunFile(HWND hTree, HWND hOwnerForm, const wchar_t *fullPath, BOOL isFolder) {
    wchar_t workDir[MAX_PATH];
    wchar_t *slash;
    (void)hTree;

    if (!fullPath || fullPath[0] == L'\0') return;

    lstrcpynW(workDir, fullPath, MAX_PATH);
    if (!isFolder) {
        slash = wcsrchr(workDir, L'\\');
        if (slash && slash > workDir) {
            if (slash == workDir + 2 && workDir[1] == L':') {
                *(slash + 1) = L'\0'; /* Keep "C:\" */
            } else {
                *slash = L'\0';
            }
        }
    }

    ShellExecuteW(hOwnerForm, L"open", fullPath, NULL, workDir, SW_SHOWNORMAL);
}

static const CBORG_TREE_MENU_ENTRY g_TreeMenuEntries[] = {
    { IDM_TREE_OPEN_FILE,      L"\xD83D\xDCC2 Open File in Editor",   Action_OpenInEditor  },
    { IDM_TREE_RUN_FILE,       L"\x25B6\xFE0F Run File",              Action_RunFile       },
    { IDM_TREE_EXPAND_NODE,    L"\xD83D\xDCC1 Expand / Toggle",        Action_ExpandNode    },
    { 0,                       NULL,                                   NULL                 },
    { IDM_TREE_COPY_PATH,      L"\xD83D\xDCCB Copy Full Path",         Action_CopyPath      },
    { IDM_TREE_OPEN_CMD,       L"\x2328\xFE0F Open CMD Here",          Action_OpenCmdHere   },
    { 0,                       NULL,                                   NULL                 },
    { IDM_TREE_REFRESH_DRIVES, L"\xD83D\xDD04 Refresh All Drives",     Action_RefreshDrives }
};


BOOL CborgTree_ShowContextMenu(HWND hTree, HWND hOwnerForm, POINT screenPt) {
    HMENU hMenu;
    UINT cmd;
    size_t i;
    wchar_t selPath[MAX_PATH] = L"";
    BOOL isFolder = FALSE;
    HWND target = (hOwnerForm && IsWindow(hOwnerForm)) ? hOwnerForm : GetParent(hTree);

    if (!hTree || !IsWindow(hTree) || !target) return FALSE;

    if (screenPt.x == -1 && screenPt.y == -1) {
        HTREEITEM hSel = TreeView_GetSelection(hTree);
        RECT rcItem;
        if (hSel && TreeView_GetItemRect(hTree, hSel, &rcItem, TRUE)) {
            screenPt.x = rcItem.left + 12;
            screenPt.y = rcItem.bottom;
            ClientToScreen(hTree, &screenPt);
        } else {
            GetCursorPos(&screenPt);
        }
    } else {
        TVHITTESTINFO ht;
        ZeroMemory(&ht, sizeof(ht));
        ht.pt = screenPt;
        ScreenToClient(hTree, &ht.pt);
        if (TreeView_HitTest(hTree, &ht) && ht.hItem) {
            TreeView_SelectItem(hTree, ht.hItem);
        }
    }

    hMenu = CreatePopupMenu();
    if (!hMenu) return FALSE;

    for (i = 0; i < ARRAYSIZE(g_TreeMenuEntries); ++i) {
        if (g_TreeMenuEntries[i].label == NULL) {
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        } else {
            AppendMenuW(hMenu, MF_STRING, g_TreeMenuEntries[i].cmdId, g_TreeMenuEntries[i].label);
        }
    }

    SetForegroundWindow(target);
    cmd = TrackPopupMenu(
        hMenu,
        TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_RIGHTBUTTON,
        screenPt.x, screenPt.y, 0, target, NULL
    );
    DestroyMenu(hMenu);

    if (cmd == 0) return FALSE;

    CborgTree_GetSelectedPathW(hTree, selPath, MAX_PATH, &isFolder);

    for (i = 0; i < ARRAYSIZE(g_TreeMenuEntries); ++i) {
        if (g_TreeMenuEntries[i].cmdId == cmd && g_TreeMenuEntries[i].fnAction) {
            g_TreeMenuEntries[i].fnAction(hTree, target, selPath, isFolder);
            return TRUE;
        }
    }

    SendMessageW(target, WM_COMMAND, MAKEWPARAM((WORD)cmd, 0), (LPARAM)hTree);
    return TRUE;
}

void CborgTree_AttachRightClickHook(HWND hTree, HWND hOwnerForm) {
    if (hTree && IsWindow(hTree)) {
        SetWindowSubclass(hTree, CborgTree_SubclassProc, 1, (DWORD_PTR)hOwnerForm);
    }
}
