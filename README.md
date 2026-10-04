# mouse-keys

A small Windows tool that lets you use your keyboard as a mouse. It's made for keyboards **without a number pad**, where the built-in Windows Mouse Keys feature doesn't work.

There's nothing to install beyond Python 3. It uses only the standard library (`ctypes` to call the Windows API).

## Usage

Double-click `run_mouse_keys.bat`, or run:

```powershell
py mouse_keys.py
```

| Key | Action |
|---|---|
| **F8** | Turn mouse mode on/off (high beep = on, low beep = off) |
| **Shift+F8** | Quit |
| **Enter** | Left click (hold to drag) |
| **X** | Right click |
| **C** | Middle click |
| **Arrow keys** | Move the cursor (speeds up while held) |
| **PageUp / PageDown** | Scroll |

By default, keys keep their normal function **and** act as the mouse (`PASS_THROUGH = True`). For example, Enter still presses Enter and also left-clicks.

## Configuration

Edit the `CONFIG` section at the top of `mouse_keys.py`:

- `BUTTON_KEYS`, `MOVE_KEYS`, `SCROLL_KEYS`, `TOGGLE_KEY`: which keys do what ([virtual-key codes](https://learn.microsoft.com/windows/win32/inputdev/virtual-key-codes))
- `PASS_THROUGH`: `True` = keys also type normally; `False` = keys act only as the mouse
- `START_ENABLED`: start with mouse mode already on
- `START_SPEED`, `MAX_SPEED`, `ACCEL`: how fast the cursor moves
- `BEEP`: sound when toggling

## Notes

- Clicks won't reach programs running as administrator unless you also run this script as administrator (a Windows restriction).
- To start it automatically when you log in, put a shortcut to `run_mouse_keys.bat` in `shell:startup`.
