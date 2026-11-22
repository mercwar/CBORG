# Cborg Editor (`cborg.exe`)

A lightweight, native Win32 C code editor and workspace runner built on **Scintilla** and **Lexilla**. Designed for zero-bloat editing, fast file-tree navigation, live symbol outlining, and direct script/binary execution on Windows.

---

## Features

* **Native Win32 + Scintilla Engine (`cborg.dll`)**
  * Hardware-crisp text rendering powered by `Scintilla.dll` and `Lexilla.dll` wrapped inside a custom `ScintillaCodeControl` DLL (`cborg.dll`).
  * Syntax highlighting for **C/C++**, **Python**, **JSON**, **Batch**, and **Plain Text**, plus Obsidian Dark and Light UI themes.
  * Configurable view options: word wrap, visible whitespace, end-of-line (EOL) markers, and indentation guides.
* **Dual-Pane Sidebar**
  * **Live File & Drive Explorer (`cborg_tree.c`):** Lazy-loading directory tree across all local and network drives with file-type icons and double-click file opening.
  * **Live Symbol Outline (`cborg_symtree.c`):** Automatically parses the active buffer into **Types & Structs**, **Globals & Defines**, and **Functions**, jumping straight to the definition line and centering it in the viewport on click.
* **Drop-In Right-Click Context Menu**
  * Right-clicking any file or folder in the explorer provides instant actions via an extensible function-pointer table:
    * **Open File in Editor**
    * **Run File** (executes `.exe`, `.bat`, `.cmd`, `.php`, `.py`, etc. directly in its parent working directory)
    * **Expand / Toggle**
    * **Copy Full Path** (Unicode clipboard copy)
    * **Open CMD Here** (opens `cmd.exe` directly in the target directory)
    * **Refresh All Drives**

---

## Project Structure

```text
├── cborg.c              # Main Win32 host window, toolbar, menus, file I/O, and layout
├── cborg_tree.c / .h    # Drive/folder TreeView & right-click context menu action table
├── cborg_symtree.c / .h # Code parser & symbol outline TreeView (Types, Globals, Functions)
├── editor_dll.c         # Custom Win32 control wrapper compiled into cborg.dll
├── editor_frame_api.h   # Shared theme, language, and editor frame interface definitions
├── cborg.def            # Module-definition file for cborg.dll exports
├── cborg.rc             # Win32 application icon & resource script
├── build_dll.bat        # Builds cborg.dll and cborg.lib via MSVC
└── build.bat            # Compiles cborg.rc and links cborg.exe against cborg.lib
```

---

## Runtime Dependencies

Place these binaries together in the same folder when distributing `cborg.exe`:

| File | Purpose |
| :--- | :--- |
| `cborg.exe` | Main editor host executable |
| `cborg.dll` | Custom Win32 Scintilla control wrapper library |
| `Scintilla.dll` | Core Scintilla editing component |
| `Lexilla.dll` | Language lexer library for syntax highlighting |

---

## Building from Source (Windows / MSVC)

### Prerequisites
* **Visual Studio 2019 / 2022** (or Build Tools for Visual Studio) with the **Desktop development with C++** workload (`cl.exe`, `nmake.exe`, `rc.exe`).
* **Scintilla** and **Lexilla** source trees placed side-by-side in the project root (`scintilla\` and `lexilla\`).

### 1. Build `Scintilla.dll` and `Lexilla.dll`
From an **x64 Native Tools Command Prompt for VS**:
```bat
pushd scintilla\win32 && nmake -f scintilla.mak && popd
pushd lexilla\src && nmake -f lexilla.mak && popd
copy /Y scintilla\bin\Scintilla.dll .
copy /Y lexilla\bin\Lexilla.dll .
```

### 2. Build `cborg.dll`
Run `build_dll.bat` (auto-detects Visual Studio via `vswhere.exe` and initializes `vcvars64.bat`):
```bat
cl /nologo /W4 /O2 /TC /I. /Iscintilla\include /Ilexilla\include editor_dll.c ^
    /link /DLL /DEF:cborg.def user32.lib gdi32.lib /OUT:cborg.dll /IMPLIB:cborg.lib
```

### 3. Build `cborg.exe`
Run `build.bat` to compile `cborg.rc` and link the executable:
```bat
rc /nologo /fo cborg.res cborg.rc
cl /nologo /W4 /O2 /TC /I. /Iscintilla\include cborg.c cborg_tree.c cborg_symtree.c ^
    /link cborg.res user32.lib gdi32.lib comdlg32.lib comctl32.lib shell32.lib cborg.lib /SUBSYSTEM:WINDOWS /OUT:cborg.exe
```

---

## Extending the Right-Click Menu

To add a new action to the file explorer context menu:
1. Add a new command ID in `cborg_tree.h` (e.g., `#define IDM_TREE_MY_ACTION 5112`).
2. Write a `static void Action_MyAction(HWND hTree, HWND hOwnerForm, const wchar_t *fullPath, BOOL isFolder)` function in `cborg_tree.c`.
3. Register `{ IDM_TREE_MY_ACTION, L"My Action", Action_MyAction }` inside `g_TreeMenuEntries[]`.

---

## Command-Line Usage

Open a file directly on startup:
```bat
cborg.exe "path\(\to\file.\)c"
```
