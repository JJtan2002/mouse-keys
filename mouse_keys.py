"""
mouse_keys.py - Keyboard-to-mouse for keyboards without a numpad (Windows).

No third-party packages needed: uses a low-level keyboard hook + SendInput via ctypes.

Default controls
----------------
  F8            Toggle MOUSE MODE on/off (high beep = on, low beep = off)
  Shift+F8      Quit

  While MOUSE MODE is ON:
  Enter         Left click   (hold to drag)
  X             Right click
  C             Middle click
  Arrow keys    Move the cursor (accelerates while held)
  PageUp/Down   Scroll wheel up/down

With PASS_THROUGH on, mapped keys also keep their normal function (Enter still presses Enter).
Edit the CONFIG section below to change any binding.
"""

import ctypes
import threading
import time
import winsound
from ctypes import wintypes

# ============================== CONFIG ==============================
VK = {  # virtual-key codes: https://learn.microsoft.com/windows/win32/inputdev/virtual-key-codes
    "F8": 0x77, "ENTER": 0x0D, "Z": 0x5A, "X": 0x58, "C": 0x43,
    "LEFT": 0x25, "UP": 0x26, "RIGHT": 0x27, "DOWN": 0x28,
    "PGUP": 0x21, "PGDN": 0x22, "SHIFT": 0x10,
}

TOGGLE_KEY = VK["F8"]
BUTTON_KEYS = {             # key -> mouse button
    VK["ENTER"]: "left",
    VK["X"]: "right",
    VK["C"]: "middle",
}
MOVE_KEYS = {               # key -> (dx, dy) direction
    VK["LEFT"]: (-1, 0), VK["RIGHT"]: (1, 0),
    VK["UP"]: (0, -1), VK["DOWN"]: (0, 1),
}
SCROLL_KEYS = {VK["PGUP"]: 120, VK["PGDN"]: -120}

START_SPEED = 2.0           # pixels per tick when movement starts
MAX_SPEED = 25.0            # pixels per tick cap
ACCEL = 0.5                 # speed gained per tick while held
TICK = 0.010                # seconds between movement updates
BEEP = True                 # audible feedback when toggling
PASS_THROUGH = True         # True: mapped keys ALSO type normally (e.g. Z types "z" AND clicks)
                            # False: mapped keys are swallowed and only act as the mouse
START_ENABLED = False       # True: mouse mode is already ON when the script starts
# ====================================================================

user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

LRESULT = ctypes.c_ssize_t
ULONG_PTR = ctypes.c_size_t
WH_KEYBOARD_LL = 13
WM_KEYDOWN, WM_KEYUP, WM_SYSKEYDOWN, WM_SYSKEYUP = 0x100, 0x101, 0x104, 0x105
LLKHF_INJECTED = 0x10
INPUT_MOUSE = 0
MOUSEEVENTF_MOVE, MOUSEEVENTF_WHEEL = 0x0001, 0x0800
BUTTON_FLAGS = {"left": (0x0002, 0x0004), "right": (0x0008, 0x0010), "middle": (0x0020, 0x0040)}


class KBDLLHOOKSTRUCT(ctypes.Structure):
    _fields_ = [("vkCode", wintypes.DWORD), ("scanCode", wintypes.DWORD),
                ("flags", wintypes.DWORD), ("time", wintypes.DWORD),
                ("dwExtraInfo", ULONG_PTR)]


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wintypes.LONG), ("dy", wintypes.LONG),
                ("mouseData", wintypes.LONG), ("dwFlags", wintypes.DWORD),
                ("time", wintypes.DWORD), ("dwExtraInfo", ULONG_PTR)]


class KEYBDINPUT(ctypes.Structure):  # only here so the union has the correct size
    _fields_ = [("wVk", wintypes.WORD), ("wScan", wintypes.WORD),
                ("dwFlags", wintypes.DWORD), ("time", wintypes.DWORD),
                ("dwExtraInfo", ULONG_PTR)]


class _INPUTUNION(ctypes.Union):
    _fields_ = [("mi", MOUSEINPUT), ("ki", KEYBDINPUT)]


class INPUT(ctypes.Structure):
    _fields_ = [("type", wintypes.DWORD), ("u", _INPUTUNION)]


HOOKPROC = ctypes.WINFUNCTYPE(LRESULT, ctypes.c_int, wintypes.WPARAM, wintypes.LPARAM)
user32.SetWindowsHookExW.argtypes = [ctypes.c_int, HOOKPROC, wintypes.HINSTANCE, wintypes.DWORD]
user32.SetWindowsHookExW.restype = wintypes.HHOOK
user32.CallNextHookEx.argtypes = [wintypes.HHOOK, ctypes.c_int, wintypes.WPARAM, wintypes.LPARAM]
user32.CallNextHookEx.restype = LRESULT
user32.UnhookWindowsHookEx.argtypes = [wintypes.HHOOK]
user32.SendInput.argtypes = [wintypes.UINT, ctypes.POINTER(INPUT), ctypes.c_int]
user32.GetAsyncKeyState.argtypes = [ctypes.c_int]
user32.GetAsyncKeyState.restype = ctypes.c_short
user32.PostThreadMessageW.argtypes = [wintypes.DWORD, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
kernel32.GetModuleHandleW.argtypes = [wintypes.LPCWSTR]
kernel32.GetModuleHandleW.restype = wintypes.HMODULE


def send_mouse(flags, dx=0, dy=0, data=0):
    inp = INPUT(type=INPUT_MOUSE)
    inp.u.mi = MOUSEINPUT(dx, dy, data, flags, 0, 0)
    user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))


def beep(freq):
    if BEEP:
        threading.Thread(target=winsound.Beep, args=(freq, 80), daemon=True).start()


class MouseKeys:
    def __init__(self):
        self.enabled = START_ENABLED
        self.held = set()          # mapped keys currently held (for auto-repeat filtering)
        self.running = True
        self.main_thread_id = kernel32.GetCurrentThreadId()
        self._proc = HOOKPROC(self._hook)   # keep a reference so it isn't garbage-collected
        self.hook = None

    # ---------- state helpers ----------
    def release_all(self):
        for vk in list(self.held):
            if vk in BUTTON_KEYS:
                send_mouse(BUTTON_FLAGS[BUTTON_KEYS[vk]][1])
        self.held.clear()

    def set_enabled(self, on):
        if not on:
            self.release_all()
        self.enabled = on
        print(f"Mouse mode: {'ON ' if on else 'OFF'}", flush=True)
        beep(1200 if on else 600)

    # ---------- keyboard hook ----------
    def _hook(self, n_code, w_param, l_param):
        if n_code == 0:
            kb = ctypes.cast(l_param, ctypes.POINTER(KBDLLHOOKSTRUCT)).contents
            if not (kb.flags & LLKHF_INJECTED):
                if self._handle(kb.vkCode, w_param in (WM_KEYDOWN, WM_SYSKEYDOWN)):
                    return 1  # swallow the key
        return user32.CallNextHookEx(None, n_code, w_param, l_param)

    def _handle(self, vk, down):
        if vk == TOGGLE_KEY:
            if down and vk not in self.held:
                self.held.add(vk)
                if user32.GetAsyncKeyState(VK["SHIFT"]) & 0x8000:
                    self.quit()
                else:
                    self.set_enabled(not self.enabled)
            elif not down:
                self.held.discard(vk)
            return True

        if not self.enabled:
            return False

        if vk in BUTTON_KEYS:
            down_flag, up_flag = BUTTON_FLAGS[BUTTON_KEYS[vk]]
            if down and vk not in self.held:
                self.held.add(vk)
                send_mouse(down_flag)
            elif not down and vk in self.held:
                self.held.discard(vk)
                send_mouse(up_flag)
            return not PASS_THROUGH

        if vk in MOVE_KEYS:
            (self.held.add if down else self.held.discard)(vk)
            return not PASS_THROUGH

        if vk in SCROLL_KEYS:
            if down:  # auto-repeat here is desirable: holding scrolls continuously
                send_mouse(MOUSEEVENTF_WHEEL, data=SCROLL_KEYS[vk])
            return not PASS_THROUGH

        return False

    # ---------- cursor movement loop ----------
    def _move_loop(self):
        speed = START_SPEED
        carry_x = carry_y = 0.0
        while self.running:
            dx = sum(MOVE_KEYS[k][0] for k in list(self.held) if k in MOVE_KEYS)
            dy = sum(MOVE_KEYS[k][1] for k in list(self.held) if k in MOVE_KEYS)
            if self.enabled and (dx or dy):
                carry_x += dx * speed
                carry_y += dy * speed
                ix, iy = int(carry_x), int(carry_y)
                carry_x -= ix
                carry_y -= iy
                if ix or iy:
                    send_mouse(MOUSEEVENTF_MOVE, ix, iy)
                speed = min(MAX_SPEED, speed + ACCEL)
            else:
                speed = START_SPEED
                carry_x = carry_y = 0.0
            time.sleep(TICK)

    # ---------- lifecycle ----------
    def quit(self):
        print("Quitting...", flush=True)
        self.release_all()
        self.running = False
        user32.PostThreadMessageW(self.main_thread_id, 0x0012, 0, 0)  # WM_QUIT

    def run(self):
        self.hook = user32.SetWindowsHookExW(WH_KEYBOARD_LL, self._proc,
                                             kernel32.GetModuleHandleW(None), 0)
        if not self.hook:
            raise ctypes.WinError(ctypes.get_last_error())
        threading.Thread(target=self._move_loop, daemon=True).start()

        print(__doc__)
        print(f"Running. Mouse mode: {'ON' if self.enabled else 'OFF'}  "
              f"(F8 toggle, Shift+F8 quit, pass-through {'ON' if PASS_THROUGH else 'OFF'})", flush=True)

        msg = wintypes.MSG()
        try:
            while user32.GetMessageW(ctypes.byref(msg), None, 0, 0) > 0:
                user32.TranslateMessage(ctypes.byref(msg))
                user32.DispatchMessageW(ctypes.byref(msg))
        finally:
            self.running = False
            self.release_all()
            user32.UnhookWindowsHookEx(self.hook)


if __name__ == "__main__":
    MouseKeys().run()
