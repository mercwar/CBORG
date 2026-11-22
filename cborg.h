#ifndef CBORG_TREE_H
#define CBORG_TREE_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include "editor_frame_api.h"

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

HTREEITEM CborgTree_AddItem(
    HWND              hTree,
    HTREEITEM         hParent,
    const char       *label,
    LPARAM            param,
    BOOL              bold
);

void CborgTree_PopulateDefault(HWND hTree, EDITOR_THEME_MODE theme);
void CborgTree_ApplyTheme(HWND hTree, EDITOR_THEME_MODE theme);
BOOL CborgTree_GetSelectedText(HWND hTree, char *outBuf, int cchOut);

#endif /* CBORG_TREE_H */