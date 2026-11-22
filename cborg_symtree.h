#ifndef CBORG_SYMTREE_H
#define CBORG_SYMTREE_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include "editor_frame_api.h"

HWND CborgSymTree_Create(
    HWND      hParent,
    HINSTANCE hInstance,
    UINT      treeId,
    int       x,
    int       y,
    int       width,
    int       height,
    HFONT     hFont
);

void CborgSymTree_ApplyTheme(HWND hTree, EDITOR_THEME_MODE theme);

/* Scans hFrame's text buffer and populates the function outline TreeView */
int CborgSymTree_RefreshFromFrame(HWND hTree, HWND hFrame, EDITOR_LANG_MODE lang);

/* Returns the 0-based line number stored on the currently selected tree item, or -1 */
int CborgSymTree_GetSelectedLine(HWND hTree);

/* Jumps hFrame's Scintilla editor to the selected function's line and focuses it */
BOOL CborgSymTree_JumpToSelected(HWND hTree, HWND hFrame);

#endif /* CBORG_SYMTREE_H */
