/*
 * Native Windows x64 GUI Bootloader for EasyColab (EasyColab.exe)
 *
 * - Compiled with SUBSYSTEM:WINDOWS (-mwindows -municode): zero console window flash.
 * - Full UTF-16 Unicode path support (handles spaces and CJK folder names).
 * - Syncs BOTH UCRT (_wputenv_s) and Win32 (SetEnvironmentVariableW) environment blocks
 *   so CPython's os.environ and Win32 DLL/child-process APIs see identical state.
 * - Loads _internal\runtime\python312.dll in-process via LoadLibraryExW so Windows
 *   Taskbar & Task Manager display EasyColab.exe with its embedded AppIcon.ico.
 * - Supports headless `--bridge-cli` and any standard Python CLI flags (-c, -m, *.py)
 *   by forwarding to _internal\runtime\python.exe with CREATE_NO_WINDOW.
 * - Enforces a single-instance mutex in GUI mode (activating the existing window if
 *   launched again unless `--new-window` is passed).
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

typedef int (*Py_Main_Func)(int argc, wchar_t **argv);

static BOOL file_exists_w(const wchar_t *path) {
    DWORD attr = GetFileAttributesW(path);
    return (attr != INVALID_FILE_ATTRIBUTES) && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static void set_env_both(const wchar_t *name, const wchar_t *value) {
    _wputenv_s(name, value);
    SetEnvironmentVariableW(name, value);
}

static BOOL ends_with_icase(const wchar_t *str, const wchar_t *suffix) {
    size_t slen = wcslen(str);
    size_t xlen = wcslen(suffix);
    if (slen < xlen) return FALSE;
    return _wcsicmp(str + (slen - xlen), suffix) == 0;
}

static BOOL is_headless_python_flag(const wchar_t *arg) {
    if (!arg || !arg[0]) return FALSE;
    if (wcscmp(arg, L"-c") == 0 ||
        wcscmp(arg, L"-m") == 0 ||
        wcscmp(arg, L"-u") == 0 ||
        wcscmp(arg, L"-I") == 0 ||
        wcscmp(arg, L"-S") == 0 ||
        wcscmp(arg, L"-E") == 0 ||
        wcscmp(arg, L"-B") == 0 ||
        wcscmp(arg, L"-X") == 0 ||
        wcscmp(arg, L"-V") == 0 ||
        wcscmp(arg, L"--version") == 0 ||
        wcsncmp(arg, L"--multiprocessing", 17) == 0 ||
        ends_with_icase(arg, L".py")) {
        return TRUE;
    }
    return FALSE;
}

static void append_quoted_arg(wchar_t *buf, size_t cap, const wchar_t *arg) {
    wcsncat(buf, L" \"", cap - wcslen(buf) - 1);
    for (const wchar_t *p = arg; *p; ++p) {
        if (*p == L'"') {
            wcsncat(buf, L"\\\"", cap - wcslen(buf) - 1);
        } else {
            size_t len = wcslen(buf);
            if (len + 2 < cap) {
                buf[len] = *p;
                buf[len + 1] = L'\0';
            }
        }
    }
    wcsncat(buf, L"\"", cap - wcslen(buf) - 1);
}

static int spawn_headless_python(const wchar_t *py_console_exe, wchar_t *cmdline) {
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    if (si.hStdInput || si.hStdOutput || si.hStdError) {
        si.dwFlags |= STARTF_USESTDHANDLES;
    }

    BOOL ok = CreateProcessW(
        py_console_exe,
        cmdline,
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &si,
        &pi
    );
    if (!ok) {
        return 1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)exit_code;
}

static int run_bridge_cli(
    const wchar_t *py_console_exe,
    const wchar_t *bridge_py,
    int argc,
    wchar_t **wargv
) {
    size_t cap = 32768;
    wchar_t *cmdline = (wchar_t *)calloc(cap, sizeof(wchar_t));
    if (!cmdline) return 1;

    _snwprintf(cmdline, cap - 1, L"\"%s\" -X utf8 \"%s\"", py_console_exe, bridge_py);
    for (int i = 2; i < argc; ++i) {
        append_quoted_arg(cmdline, cap, wargv[i]);
    }
    int rc = spawn_headless_python(py_console_exe, cmdline);
    free(cmdline);
    return rc;
}

static int run_python_passthrough(
    const wchar_t *py_console_exe,
    int argc,
    wchar_t **wargv
) {
    size_t cap = 32768;
    wchar_t *cmdline = (wchar_t *)calloc(cap, sizeof(wchar_t));
    if (!cmdline) return 1;

    _snwprintf(cmdline, cap - 1, L"\"%s\" -X utf8", py_console_exe);
    for (int i = 1; i < argc; ++i) {
        append_quoted_arg(cmdline, cap, wargv[i]);
    }
    int rc = spawn_headless_python(py_console_exe, cmdline);
    free(cmdline);
    return rc;
}

static BOOL CALLBACK activate_existing_window_cb(HWND hwnd, LPARAM lParam) {
    (void)lParam;
    if (!IsWindowVisible(hwnd)) return TRUE;
    wchar_t title[256];
    int len = GetWindowTextW(hwnd, title, 255);
    if (len > 0 && wcsstr(title, L"EasyColab") != NULL) {
        if (IsIconic(hwnd)) {
            ShowWindow(hwnd, SW_RESTORE);
        }
        SetForegroundWindow(hwnd);
        return FALSE;
    }
    return TRUE;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hInst;
    (void)hPrev;
    (void)lpCmdLine;
    (void)nCmdShow;

    wchar_t exe_path[4096];
    DWORD n = GetModuleFileNameW(NULL, exe_path, 4096);
    if (n == 0 || n >= 4096) {
        MessageBoxW(NULL, L"Failed to resolve EasyColab.exe path.", L"EasyColab", MB_ICONERROR | MB_OK);
        return 1;
    }

    wchar_t exe_dir[4096];
    wcsncpy(exe_dir, exe_path, 4095);
    exe_dir[4095] = L'\0';
    wchar_t *last_sep = wcsrchr(exe_dir, L'\\');
    if (!last_sep) last_sep = wcsrchr(exe_dir, L'/');
    if (last_sep) *last_sep = L'\0';

    wchar_t internal_dir[4096];
    wchar_t runtime_dir[4096];
    wchar_t py_dll_path[4096];
    wchar_t py_console_exe[4096];
    wchar_t entry_py_path[4096];
    wchar_t bridge_py_path[4096];
    wchar_t pyside_dir[4096];
    wchar_t shiboken_dir[4096];

    _snwprintf(internal_dir, 4095, L"%s\\_internal", exe_dir);
    _snwprintf(runtime_dir, 4095, L"%s\\runtime", internal_dir);
    _snwprintf(py_dll_path, 4095, L"%s\\python312.dll", runtime_dir);
    _snwprintf(py_console_exe, 4095, L"%s\\python.exe", runtime_dir);
    _snwprintf(entry_py_path, 4095, L"%s\\easycolab_win.py", internal_dir);
    _snwprintf(bridge_py_path, 4095, L"%s\\colab_bridge.py", internal_dir);
    _snwprintf(pyside_dir, 4095, L"%s\\site-packages\\PySide6", internal_dir);
    _snwprintf(shiboken_dir, 4095, L"%s\\site-packages\\shiboken6", internal_dir);

    if (!file_exists_w(py_dll_path) || !file_exists_w(entry_py_path)) {
        MessageBoxW(
            NULL,
            L"未找到 _internal 运行组件目录。\n"
            L"请先将 EasyColab-Windows-x64.zip 完整解压到文件夹后再双击运行 EasyColab.exe。\n\n"
            L"Missing _internal runtime directory. Please extract the full ZIP archive before running EasyColab.exe.",
            L"EasyColab 启动提示",
            MB_ICONWARNING | MB_OK
        );
        return 1;
    }

    /* Sync BOTH UCRT (_wenviron for Python os.environ) and Win32 environment blocks */
    set_env_both(L"PYTHONUTF8", L"1");
    set_env_both(L"PYTHONIOENCODING", L"utf-8");
    set_env_both(L"PYTHONNOUSERSITE", L"1");
    set_env_both(L"EASYCOLAB_FROZEN_EXE", exe_path);
    set_env_both(L"EASYCOLAB_INTERNAL_DIR", internal_dir);

    /* Prepend bundled DLL directories to PATH without breaking AddDllDirectory */
    DWORD old_len = GetEnvironmentVariableW(L"PATH", NULL, 0);
    size_t path_cap = (size_t)old_len + 16384;
    wchar_t *new_path = (wchar_t *)calloc(path_cap, sizeof(wchar_t));
    if (new_path) {
        _snwprintf(new_path, path_cap - 1, L"%s;%s;%s;", runtime_dir, pyside_dir, shiboken_dir);
        if (old_len > 0) {
            size_t prefix_len = wcslen(new_path);
            GetEnvironmentVariableW(L"PATH", new_path + prefix_len, (DWORD)(path_cap - prefix_len - 1));
        }
        set_env_both(L"PATH", new_path);
        free(new_path);
    }

    int argc = 0;
    wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wargv) {
        return 1;
    }

    /* 1. Fast headless path for `--bridge-cli` subprocess calls */
    if (argc >= 2 && wcscmp(wargv[1], L"--bridge-cli") == 0 && file_exists_w(py_console_exe)) {
        int rc = run_bridge_cli(py_console_exe, bridge_py_path, argc, wargv);
        LocalFree(wargv);
        return rc;
    }

    /* 2. Safety passthrough if anything invokes sys.executable with `-c`, `-m`, `*.py`, etc. */
    if (argc >= 2 && is_headless_python_flag(wargv[1]) && file_exists_w(py_console_exe)) {
        int rc = run_python_passthrough(py_console_exe, argc, wargv);
        LocalFree(wargv);
        return rc;
    }

    /* 3. Single-instance guard for GUI Studio mode (unless --new-window is passed) */
    BOOL allow_multi = FALSE;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(wargv[i], L"--new-window") == 0) {
            allow_multi = TRUE;
            break;
        }
    }
    HANDLE hSingleMutex = NULL;
    if (!allow_multi) {
        hSingleMutex = CreateMutexW(NULL, FALSE, L"Local\\EasyColabStudioWindowsSingleInstance");
        if (hSingleMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
            EnumWindows(activate_existing_window_cb, 0);
            CloseHandle(hSingleMutex);
            LocalFree(wargv);
            return 0;
        }
    }

    /* 4. Load PSF-signed python312.dll in-process for GUI Studio */
    SetCurrentDirectoryW(exe_dir);
    HMODULE hPy = LoadLibraryExW(py_dll_path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!hPy) {
        MessageBoxW(
            NULL,
            L"无法加载内置 Python 运行库 (python312.dll)。\nFailed to load bundled python312.dll.",
            L"EasyColab",
            MB_ICONERROR | MB_OK
        );
        if (hSingleMutex) CloseHandle(hSingleMutex);
        LocalFree(wargv);
        return 1;
    }

    Py_Main_Func pPy_Main = (Py_Main_Func)GetProcAddress(hPy, "Py_Main");
    if (!pPy_Main) {
        MessageBoxW(NULL, L"Failed to locate Py_Main in python312.dll.", L"EasyColab", MB_ICONERROR | MB_OK);
        if (hSingleMutex) CloseHandle(hSingleMutex);
        LocalFree(wargv);
        return 1;
    }

    /* Pass `-X utf8` explicitly in argv so UTF-8 mode works even with python312._pth isolated=1 */
    int py_argc = argc + 3;
    wchar_t **py_argv = (wchar_t **)calloc((size_t)(py_argc + 1), sizeof(wchar_t *));
    if (!py_argv) {
        if (hSingleMutex) CloseHandle(hSingleMutex);
        LocalFree(wargv);
        return 1;
    }
    py_argv[0] = exe_path;
    py_argv[1] = L"-X";
    py_argv[2] = L"utf8";
    py_argv[3] = entry_py_path;
    for (int i = 1; i < argc; ++i) {
        py_argv[i + 3] = wargv[i];
    }
    py_argv[py_argc] = NULL;

    int exit_status = pPy_Main(py_argc, py_argv);
    free(py_argv);
    if (hSingleMutex) CloseHandle(hSingleMutex);
    LocalFree(wargv);
    return exit_status;
}
