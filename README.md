# mouse-keys

A small Windows tool that lets you use your keyboard as a mouse. It's made for keyboards **without a number pad**, where the built-in Windows Mouse Keys feature doesn't work.

Available as both a **pure Python script** (zero install beyond Python) and a **native Win32 C implementation** (standalone 24 KB binary, < 1 MB private memory).

## Usage

### Option 1: Native C Executable (Recommended for lowest latency & memory)
Compile with GCC or Clang:
```powershell
gcc -O2 -s -o mouse_keys.exe mouse_keys.c
.\mouse_keys.exe
```

### Option 2: Python Script
Double-click `run_mouse_keys.bat`, or run:
```powershell
py mouse_keys.py
```

| Key | Action |
|---|---|
| **Tap Shift** (on its own) | Turn mouse mode on/off (high beep = on, low beep = off) |
| **Tap Ctrl** (on its own) | Switch profile: **WASD mode** $\leftrightarrow$ **Arrow keys mode** |
| **[WASD Mode]** | `W / A / S / D` = Move cursor, `Z` = Left click (hold to drag) |
| **[Arrow Keys Mode]** | `Arrows` = Move cursor, `Enter` = Left click (hold to drag) |
| **X** | Right click |
| **C** | Middle click |
| **PageUp / PageDown** | Scroll wheel up / down |
| **Shift+F8** | Quit |

By default, keys keep their normal function **and** act as the mouse (`PASS_THROUGH = True`).

**How toggles work:**
- **Shift Tap:** Tapping Shift alone within 0.35 s toggles mouse mode. Typing capitals (Shift+letter), Shift-clicking, or holding Shift never triggers the toggle.
- **Ctrl Tap:** Tapping Ctrl alone within 0.35 s toggles between the WASD profile and the Arrow keys profile. Common shortcuts (`Ctrl+C`, `Ctrl+V`, etc.) never switch profiles.

## Configuration

Edit the `CONFIG` section at the top of `mouse_keys.py`:

- `BUTTON_KEYS`, `MOVE_KEYS`, `SCROLL_KEYS`: which keys do what ([virtual-key codes](https://learn.microsoft.com/windows/win32/inputdev/virtual-key-codes))
- `TOGGLE_KEYS`: keys that toggle when tapped alone (default: both Shifts; use `{VK["RSHIFT"]}` for Right Shift only)
- `TAP_MAX_SECONDS`: how quick a tap must be
- `QUIT_KEY`: Shift + this key quits
- `PASS_THROUGH`: `True` = keys also type normally; `False` = keys act only as the mouse
- `START_ENABLED`: start with mouse mode already on
- `START_SPEED`, `MAX_SPEED`, `ACCEL`: how fast the cursor moves
- `BEEP`: sound when toggling

## Notes

- Clicks won't reach programs running as administrator unless you also run this script as administrator (a Windows restriction).
- To start it automatically when you log in, put a shortcut to `run_mouse_keys.bat` in `shell:startup`.

## License

MIT - see [LICENSE](LICENSE).
