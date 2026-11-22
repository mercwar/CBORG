#ifndef EDITOR_FRAME_API_H
#define EDITOR_FRAME_API_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

#define EDITOR_FRAME_CLASS_NAME  "MercwarEditorFrame"
#define EDITOR_DLL_FILE_NAME     "cborg.dll"

/* Commands that any form menu, toolbar button, or shortcut can dispatch */
#define CMD_EDITOR_NEW           1001
#define CMD_EDITOR_UNDO          1101
#define CMD_EDITOR_REDO          1102
#define CMD_EDITOR_CUT           1103
#define CMD_EDITOR_COPY          1104
#define CMD_EDITOR_PASTE         1105
#define CMD_EDITOR_SELECT_ALL    1106

#define CMD_EDITOR_THEME_DARK    1201
#define CMD_EDITOR_THEME_LIGHT   1202
#define CMD_EDITOR_THEME_TOGGLE  1203
#define CMD_EDITOR_WRAP_TOGGLE   1204
#define CMD_EDITOR_WS_TOGGLE     1205
#define CMD_EDITOR_EOL_TOGGLE    1206
#define CMD_EDITOR_GUIDES_TOGGLE 1207

#define CMD_EDITOR_LANG_C        1301
#define CMD_EDITOR_LANG_PYTHON   1302
#define CMD_EDITOR_LANG_JSON     1303
#define CMD_EDITOR_LANG_BATCH    1304
#define CMD_EDITOR_LANG_PLAIN    1305
#define CMD_EDITOR_LANG_CYCLE    1306

/* Custom WM_NOTIFY code sent from frame HWND to parent form */
#define NM_EDITOR_STATE_CHANGED  (WM_APP + 401)

typedef struct {
    NMHDR hdr;
    BOOL  modified;
    int   line;
    int   col;
    int   lineCount;
    int   charCount;
    int   themeMode;
    int   langMode;
} NM_EDITOR_FRAME_STATE;

typedef enum {
    EDITOR_THEME_OBSIDIAN = 0,
    EDITOR_THEME_LIGHT    = 1
} EDITOR_THEME_MODE;

typedef enum {
    EDITOR_LANG_C = 0,
    EDITOR_LANG_PYTHON,
    EDITOR_LANG_JSON,
    EDITOR_LANG_BATCH,
    EDITOR_LANG_PLAIN
} EDITOR_LANG_MODE;

#ifdef __cplusplus
extern "C" {
#endif

BOOL WINAPI EditorFrame_RegisterClass(HINSTANCE hInstance);
void WINAPI EditorFrame_UnregisterClass(HINSTANCE hInstance);

HWND WINAPI EditorFrame_Create(
    HWND        hParentForm,
    HINSTANCE   hInstance,
    UINT        frameId,
    int         x,
    int         y,
    int         width,
    int         height,
    const char *initialText
);
void WINAPI EditorFrame_Destroy(HWND hFrame);
HWND WINAPI EditorFrame_GetScintillaHwnd(HWND hFrame);

/* Connect a form's menu or dispatch commands from any form control */
BOOL WINAPI EditorFrame_AttachMenu(HWND hFrame, HMENU hMenu);
BOOL WINAPI EditorFrame_SendCommand(HWND hFrame, WORD cmdId);

/* Styling, lexer & view configuration */
void WINAPI EditorFrame_SetTheme(HWND hFrame, EDITOR_THEME_MODE theme);
void WINAPI EditorFrame_SetLanguage(HWND hFrame, EDITOR_LANG_MODE lang);
void WINAPI EditorFrame_SetViewFlags(
    HWND hFrame,
    BOOL wordWrap,
    BOOL showWhitespace,
    BOOL showEol,
    BOOL showIndentGuides
);

/* Buffer, search & state API */
BOOL  WINAPI EditorFrame_SetText(HWND hFrame, const char *utf8Text);
char *WINAPI EditorFrame_GetTextAlloc(HWND hFrame, size_t *outLen);
void  WINAPI EditorFrame_FreeText(char *buf);
BOOL  WINAPI EditorFrame_FindNext(HWND hFrame, const char *needle, int startPos);
void  WINAPI EditorFrame_GetCaretStats(
    HWND hFrame,
    int *outLine,
    int *outCol,
    int *outLineCount,
    int *outCharCount
);
void  WINAPI EditorFrame_SetSavePoint(HWND hFrame);
BOOL  WINAPI EditorFrame_IsModified(HWND hFrame);
void  WINAPI EditorFrame_InsertTemplate(HWND hFrame, EDITOR_LANG_MODE lang, EDITOR_THEME_MODE theme);

#ifdef __cplusplus
}
#endif

#endif /* EDITOR_FRAME_API_H */