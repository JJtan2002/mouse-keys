/*
 * mouse_keys.c - Lightweight keyboard-to-mouse for keyboards without a numpad.
 *
 * Direct Win32 implementation (pure C, zero runtime dependencies).
 * RAM Footprint: ~1.5 - 2.5 MB Working Set (vs ~14 MB in Python).
 * Binary Size: ~30 - 50 KB.
 *
 * Controls:
 *   Tap Shift (alone) : Toggle Mouse Mode ON/OFF
 *   Shift + F8        : Quit
 *   Enter             : Left click (hold to drag)
 *   X                 : Right click
 *   C                 : Middle click
 *   Arrow Keys        : Move cursor (accelerates while held)
 *   PageUp / PageDown : Scroll wheel up / down
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include <stdio.h>

/* ============================== CONFIG ============================== */
#define TAP_MAX_MS       350     /* Max ms Shift can be held to count as a solo tap */
#define START_SPEED      2.0f    /* Initial pixels per tick */
#define MAX_SPEED        25.0f   /* Speed ceiling */
#define ACCEL            0.5f    /* Speed gained per tick while held */
#define TICK_MS          10      /* Movement thread poll frequency */
#define PASS_THROUGH     true    /* true = keys type AND trigger mouse action */
#define START_ENABLED    false   /* Initial state on launch */
#define ENABLE_BEEP      true    /* Audio feedback on toggle */
/* ==================================================================== */

/* State variables */
static volatile bool g_running = true;
static volatile bool g_enabled = START_ENABLED;
static DWORD g_main_thread_id = 0;
static HHOOK g_kb_hook = NULL;
static HHOOK g_mouse_hook = NULL;
static HANDLE g_move_thread = NULL;

/* Key holding bitmask / tracking */
static volatile bool g_key_held[256] = { false };

/* Shift tap detection */
static DWORD g_tap_key = 0;
static DWORD g_tap_time = 0;
static bool  g_tap_clean = false;

/* Helper: Send mouse events via Win32 SendInput */
static void send_mouse(DWORD flags, LONG dx, LONG dy, DWORD data) {
    INPUT input = { 0 };
    input.type = INPUT_MOUSE;
    input.mi.dx = dx;
    input.mi.dy = dy;
    input.mi.mouseData = data;
    input.mi.dwFlags = flags;
    SendInput(1, &input, sizeof(INPUT));
}

/* Audio feedback */
static void play_beep(DWORD freq, DWORD duration) {
    if (ENABLE_BEEP) {
        /* MessageBeep or Beep in separate thread / quick async */
        Beep(freq, duration);
    }
}

/* Reset all pressed mouse buttons to prevent sticking */
static void release_all(void) {
    if (g_key_held[VK_RETURN]) {
        send_mouse(MOUSEEVENTF_LEFTUP, 0, 0, 0);
    }
    if (g_key_held['X']) {
        send_mouse(MOUSEEVENTF_RIGHTUP, 0, 0, 0);
    }
    if (g_key_held['C']) {
        send_mouse(MOUSEEVENTF_MIDDLEUP, 0, 0, 0);
    }
    for (int i = 0; i < 256; i++) {
        g_key_held[i] = false;
    }
}

static void set_enabled(bool on) {
    if (!on) {
        release_all();
    }
    g_enabled = on;
    printf("Mouse mode: %s\n", on ? "ON" : "OFF");
    fflush(stdout);
    if (ENABLE_BEEP) {
        play_beep(on ? 1200 : 600, 80);
    }
}

/* Background thread for smooth cursor movement with sub-pixel carry */
static DWORD WINAPI move_thread_proc(LPVOID lpParam) {
    (void)lpParam;
    float speed = START_SPEED;
    float carry_x = 0.0f;
    float carry_y = 0.0f;

    while (g_running) {
        int dx = 0;
        int dy = 0;

        if (g_enabled) {
            if (g_key_held[VK_LEFT])  dx -= 1;
            if (g_key_held[VK_RIGHT]) dx += 1;
            if (g_key_held[VK_UP])    dy -= 1;
            if (g_key_held[VK_DOWN])  dy += 1;
        }

        if (dx != 0 || dy != 0) {
            carry_x += (float)dx * speed;
            carry_y += (float)dy * speed;

            int ix = (int)carry_x;
            int iy = (int)carry_y;

            carry_x -= (float)ix;
            carry_y -= (float)iy;

            if (ix != 0 || iy != 0) {
                send_mouse(MOUSEEVENTF_MOVE, ix, iy, 0);
            }

            speed += ACCEL;
            if (speed > MAX_SPEED) {
                speed = MAX_SPEED;
            }
        } else {
            speed = START_SPEED;
            carry_x = 0.0f;
            carry_y = 0.0f;
        }

        Sleep(TICK_MS);
    }
    return 0;
}

/* Shift tap detection */
static void track_shift_tap(DWORD vk, bool down) {
    if (vk == VK_LSHIFT || vk == VK_RSHIFT) {
        if (down) {
            if (g_tap_key == 0) {
                g_tap_key = vk;
                g_tap_time = GetTickCount();
                g_tap_clean = true;
            } else if (vk != g_tap_key) {
                g_tap_clean = false;
            }
        } else if (vk == g_tap_key) {
            if (g_tap_clean && (GetTickCount() - g_tap_time <= TAP_MAX_MS)) {
                set_enabled(!g_enabled);
            }
            g_tap_key = 0;
        }
    } else if (down) {
        /* Any other key down cancels solo Shift tap (e.g. typing capital letters) */
        g_tap_clean = false;
    }
}

/* Handle key press events */
static bool handle_key(DWORD vk, bool down) {
    track_shift_tap(vk, down);

    /* Shift is never swallowed so typing works normally */
    if (vk == VK_LSHIFT || vk == VK_RSHIFT) {
        return false;
    }

    /* Shift + F8 = Quit */
    if (vk == VK_F8 && down && (GetAsyncKeyState(VK_SHIFT) & 0x8000)) {
        printf("Quitting...\n");
        fflush(stdout);
        release_all();
        g_running = false;
        PostThreadMessage(g_main_thread_id, WM_QUIT, 0, 0);
        return true;
    }

    if (!g_enabled) {
        return false;
    }

    /* Mouse clicks */
    if (vk == VK_RETURN) {
        if (down && !g_key_held[vk]) {
            g_key_held[vk] = true;
            send_mouse(MOUSEEVENTF_LEFTDOWN, 0, 0, 0);
        } else if (!down && g_key_held[vk]) {
            g_key_held[vk] = false;
            send_mouse(MOUSEEVENTF_LEFTUP, 0, 0, 0);
        }
        return !PASS_THROUGH;
    }

    if (vk == 'X') {
        if (down && !g_key_held[vk]) {
            g_key_held[vk] = true;
            send_mouse(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0);
        } else if (!down && g_key_held[vk]) {
            g_key_held[vk] = false;
            send_mouse(MOUSEEVENTF_RIGHTUP, 0, 0, 0);
        }
        return !PASS_THROUGH;
    }

    if (vk == 'C') {
        if (down && !g_key_held[vk]) {
            g_key_held[vk] = true;
            send_mouse(MOUSEEVENTF_MIDDLEDOWN, 0, 0, 0);
        } else if (!down && g_key_held[vk]) {
            g_key_held[vk] = false;
            send_mouse(MOUSEEVENTF_MIDDLEUP, 0, 0, 0);
        }
        return !PASS_THROUGH;
    }

    /* Cursor movement keys */
    if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN) {
        g_key_held[vk] = down;
        return !PASS_THROUGH;
    }

    /* Scroll wheel keys */
    if (vk == VK_PRIOR) { /* PageUp */
        if (down) {
            send_mouse(MOUSEEVENTF_WHEEL, 0, 0, 120);
        }
        return !PASS_THROUGH;
    }
    if (vk == VK_NEXT) { /* PageDown */
        if (down) {
            send_mouse(MOUSEEVENTF_WHEEL, 0, 0, (DWORD)-120);
        }
        return !PASS_THROUGH;
    }

    return false;
}

/* Low-level keyboard hook callback */
static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT *kb = (KBDLLHOOKSTRUCT *)lParam;
        if (!(kb->flags & LLKHF_INJECTED)) {
            bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            if (handle_key(kb->vkCode, down)) {
                return 1; /* Swallow keystroke */
            }
        }
    }
    return CallNextHookEx(g_kb_hook, nCode, wParam, lParam);
}

/* Low-level mouse hook callback (cancels Shift tap if user Shift+clicks) */
static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_tap_key != 0) {
        switch (wParam) {
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN:
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
                g_tap_clean = false;
                break;
            default:
                break;
        }
    }
    return CallNextHookEx(g_mouse_hook, nCode, wParam, lParam);
}

int main(void) {
    g_main_thread_id = GetCurrentThreadId();
    HINSTANCE hInst = GetModuleHandle(NULL);

    g_kb_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInst, 0);
    if (!g_kb_hook) {
        fprintf(stderr, "Failed to install keyboard hook! (Error: %lu)\n", GetLastError());
        return 1;
    }

    g_mouse_hook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, hInst, 0);
    if (!g_mouse_hook) {
        fprintf(stderr, "Failed to install mouse hook! (Error: %lu)\n", GetLastError());
        UnhookWindowsHookEx(g_kb_hook);
        return 1;
    }

    g_move_thread = CreateThread(NULL, 0, move_thread_proc, NULL, 0, NULL);
    if (!g_move_thread) {
        fprintf(stderr, "Failed to create movement thread!\n");
        UnhookWindowsHookEx(g_mouse_hook);
        UnhookWindowsHookEx(g_kb_hook);
        return 1;
    }

    printf("=========================================\n");
    printf(" Mouse Keys (Native C Edition)\n");
    printf("=========================================\n");
    printf("Controls:\n");
    printf("  Tap Shift alone : Toggle Mouse Mode ON/OFF\n");
    printf("  Enter           : Left click (hold to drag)\n");
    printf("  X               : Right click\n");
    printf("  C               : Middle click\n");
    printf("  Arrow Keys      : Move cursor (accelerates)\n");
    printf("  PgUp / PgDn     : Scroll wheel\n");
    printf("  Shift + F8      : Quit\n");
    printf("-----------------------------------------\n");
    printf("Running. Mouse mode: %s\n", g_enabled ? "ON" : "OFF");
    fflush(stdout);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    /* Cleanup */
    g_running = false;
    release_all();
    if (g_move_thread) {
        WaitForSingleObject(g_move_thread, 200);
        CloseHandle(g_move_thread);
    }
    if (g_mouse_hook) UnhookWindowsHookEx(g_mouse_hook);
    if (g_kb_hook)    UnhookWindowsHookEx(g_kb_hook);

    return 0;
}
