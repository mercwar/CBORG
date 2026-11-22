#ifndef CBORG_TREE_H
#define CBORG_TREE_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include "editor_frame_api.h"
#define IDM_TREE_RUN_FILE        5111

/* Programming context menu command IDs sent to parent form via WM_COMMAND */
#define IDM_TREE_NEW_FILE        5101
#define IDM_TREE_NEW_FOLDER      5102
#define IDM_TREE_OPEN_FILE       5103
#define IDM_TREE_EXPAND_NODE     5104
#define IDM_TREE_COPY_PATH       5105
#define IDM_TREE_OPEN_CMD        5106
#define IDM_TREE_BUILD_TARGET    5107
#define IDM_TREE_REFRESH_DRIVES  5108
#define IDM_TREE_RENAME_ITEM     5109
#define IDM_TREE_DELETE_ITEM     5110

HWND CborgTree_Create(
    HWND              hParent,
    HINSTANCE         hInstance,
    UINT              treeId,
    int               x,
    int               y,
    int               width,
    int               height,
    HFONT             hFont
);

HTREEITEM CborgTree_AddItemW(
    HWND              hTree,
    HTREEITEM         hParent,
    const wchar_t    *label,
    const wchar_t    *fullPath,
    BOOL              isFolder,
    BOOL              hasChildren,
    BOOL              bold
);

int  CborgTree_LoadAllDrives(HWND hTree, EDITOR_THEME_MODE theme);
void CborgTree_PopulateDefault(HWND hTree, EDITOR_THEME_MODE theme);
void CborgTree_ApplyTheme(HWND hTree, EDITOR_THEME_MODE theme);

BOOL CborgTree_ExpandDirectory(HWND hTree, HTREEITEM hItem);
BOOL CborgTree_GetSelectedPathW(HWND hTree, wchar_t *outPath, int cchOut, BOOL *outIsFolder);
BOOL CborgTree_GetSelectedPath(HWND hTree, char *outPath, int cchOut, BOOL *outIsFolder);
BOOL CborgTree_GetSelectedText(HWND hTree, char *outBuf, int cchOut);
BOOL CborgTree_ShowContextMenu(HWND hTree, HWND hOwnerForm, POINT screenPt);
void CborgTree_FreeAllPaths(HWND hTree);
/* Callback signature for drop-in tree context menu items */
typedef void (*CBORG_TREE_ACTION_FN)(HWND hTree, HWND hOwnerForm, const wchar_t *fullPath, BOOL isFolder);

typedef struct {
    UINT                 cmdId;
    const wchar_t       *label;      /* Use NULL for a menu separator */
    CBORG_TREE_ACTION_FN fnAction;   /* Callback executed immediately on click */
} CBORG_TREE_MENU_ENTRY;

/* Subclass/hook right-click directly onto hTree so NM_RCLICK + Shift+F10 work automatically */
void CborgTree_AttachRightClickHook(HWND hTree, HWND hOwnerForm);

#endif /* CBORG_TREE_H */