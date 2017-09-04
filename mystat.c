#include <windows.h>
#include <strsafe.h>

#ifdef _DEBUG
#define dprintf(...) dbgprintf(__VA_ARGS__)
#else
#define dprintf(...) ((void)0)
#endif

static HANDLE stat_file = INVALID_HANDLE_VALUE;

#ifdef USE_BUFIO
#define stat_bufszie 4096

static BYTE stat_buf[stat_bufszie];
static DWORD stat_bufp = 0;
#endif

static HHOOK kbd_hook = NULL;
static DWORD has_ctrl = 0;
static DWORD has_alt = 0;
static DWORD prev_m = 0;

static volatile LONG isquit = 0;
static volatile LONG kbd_cur = 0;
static LONG kbd_stat[2][256] = {0};

void dbgprintf(LPCTSTR format, ...) {
    TCHAR s[256];
    va_list args;
    va_start(args, format);
    StringCchVPrintf(s, 256, format, args);
    va_end(args);
    OutputDebugString(s);
}

LRESULT CALLBACK kbd_proc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT *hs = (KBDLLHOOKSTRUCT *)lParam;
        dprintf(TEXT("[mystat] [%lu] ==>"
                    "vkCode: %lu, scanCode: %lu, flags: %lu, time: %lu, dwExtraInfo: %lu"), 
                wParam, hs->vkCode, hs->scanCode, hs->flags, hs->time, hs->dwExtraInfo);

        switch (wParam) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            switch (hs->vkCode) {
            case VK_LCONTROL:
            case VK_RCONTROL:
                has_ctrl = 1;
                break;
            case VK_LMENU:
            case VK_RMENU:
                has_alt = 1;
                break;
            case 'M':
                if (has_ctrl && has_alt) {
                    prev_m = 1;
                }
                break;
            case 'Y':
                if (prev_m && has_ctrl && has_alt) {
                    dprintf(TEXT("[mystat] post quit"));
                    UnhookWindowsHookEx(kbd_hook);
                    PostQuitMessage(0);
                }
            default:
                prev_m = 0;
            }
            InterlockedIncrement(&kbd_stat[kbd_cur][hs->vkCode]);
            break;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            switch (hs->vkCode) {
            case VK_LCONTROL:
            case VK_RCONTROL:
                has_ctrl = 0;
                break;
            case VK_LMENU:
            case VK_RMENU:
                has_alt = 0;
            }
            break;
        }
    }
    return CallNextHookEx(kbd_hook, nCode, wParam, lParam);
}

#ifdef USE_BUFIO
void flush_stat() {
    if (stat_bufp == 0) {
        return;
    }
    DWORD written;
    BOOL ok = WriteFile(stat_file, stat_buf, stat_bufp, &written, NULL);
    if (!ok) {
        dprintf(TEXT("[mystat] WriteFile() failed: %lu"), GetLastError());
    }
    stat_bufp = 0;
}
#endif

void write_stat(void *data, size_t n) {
#ifdef USE_BUFIO
    if (stat_bufp + n > stat_bufszie) {
        flush_stat();
    }
    CopyMemory(&stat_buf[stat_bufp], data, n);
    stat_bufp += n;
#else
    DWORD written;
    BOOL ok = WriteFile(stat_file, data, n, &written, NULL);
    if (!ok) {
        dprintf(TEXT("[mystat] WriteFile() failed: %lu"), GetLastError());
    }
#endif
}

DWORD WINAPI stat_proc(LPVOID lpParameter) {
    dprintf(TEXT("[mystat] stat_proc() start"));
    struct key_stat {
        DWORD code;
        DWORD times;
    };
    struct {
        FILETIME at;
        DWORD count;
        struct key_stat key[256];
    } stat;
    DWORD offset = 0;
    while (!isquit) {
        Sleep(1000 - offset);
        DWORD before = GetTickCount();
        LONG cur = kbd_cur;
        kbd_cur = (cur + 1) % 2;
        GetSystemTimeAsFileTime(&stat.at);
        stat.count = 0;
        for (int i = 0; i < 256; i++) {
            if (kbd_stat[cur][i] == 0) {
                continue;
            }
            stat.key[stat.count].code = (DWORD)i;
            stat.key[stat.count].times = (DWORD)kbd_stat[cur][i];
            stat.count++;
        }
        if (stat.count > 0) {
            write_stat(&stat, sizeof(FILETIME) + sizeof(DWORD) + stat.count * sizeof(struct key_stat));
        }
        ZeroMemory(&kbd_stat[cur], sizeof(LONG) * 256);
        offset = GetTickCount() - before;
        dprintf(TEXT("[mystat] stat cost: %dms"), offset);
    }
#ifdef USE_BUFIO
    flush_stat();
#endif
    dprintf(TEXT("[mystat] stat_proc() exit"));
    return 0;
}

int main() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    TCHAR stat_name[48];
    StringCchPrintf(stat_name, 48, TEXT("%d%02d%02d%02d%02d%02d-%d.stat"),
            t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, GetCurrentProcessId());
    stat_file = CreateFile(
            stat_name,
            GENERIC_WRITE,
            FILE_SHARE_READ,
            NULL,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            NULL);
    if (stat_file == INVALID_HANDLE_VALUE) {
        dprintf(TEXT("[mystat] CreateFile() failed: %lu"), GetLastError());
        return 1;
    }
    kbd_hook = SetWindowsHookEx(WH_KEYBOARD_LL, kbd_proc, NULL, 0);
    if (kbd_hook == NULL) {
        dprintf(TEXT("[mystat] SetWindowsHookEx() failed: %lu"), GetLastError());
        return 1;
    }
    dprintf(TEXT("[mystat] SetWindowsHookEx() ok"));
    HANDLE stat_th = CreateThread(NULL, 0, stat_proc, NULL, 0, NULL);
    if (stat_th == NULL) {
        UnhookWindowsHookEx(kbd_hook);
        dprintf(TEXT("[mystat] CreateThread() failed: %lu"), GetLastError());
        return 1;
    }
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        DispatchMessageW(&msg);
    }
    isquit = 1;
    WaitForSingleObject(stat_th, INFINITE);
    CloseHandle(stat_th);
    CloseHandle(stat_file);
    dprintf(TEXT("[mystat] exit"));
    return (int)msg.wParam;
}
