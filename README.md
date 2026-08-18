clipbridge - universal clipboard bridge & background synchronization daemon
=============================================================================
`clipbridge` connects your desktop graphical clipboard directly to the `unipaste`
formatting engine.

When running in the background, `clipbridge` listens for clipboard updates. Whenever
you copy rich text from Slack, Microsoft Teams, Discord, Google Chrome, or other rich
editors, `clipbridge` automatically renders the optimal plain text (with properly aligned
tables, code blocks, and lists) and updates the clipboard's plain-text representation.

When you switch to **Notepad.exe**, terminal, vim, or any plain text field and press
`Ctrl+V`, you get clean, beautiful formatted text instead of mangled garbage!

Features
--------
* **Zero external dependencies**: Built in pure C99 with standard POSIX and Win32 APIs.
* **Non-intrusive**: Preserves original rich HTML on the clipboard while upgrading the plaintext fallback.
* **Multi-platform**:
  * **Windows**: Event-driven Win32 `AddClipboardFormatListener` (0% CPU idle, zero polling).
  * **Linux (Wayland)**: Works with `wl-clipboard` (`wl-paste` / `wl-copy`).
  * **Linux (X11)**: Works with `xclip` or `xsel`.
  * **macOS**: Works with `pbpaste` / `pbcopy`.
* **Flexible operation modes**:
  * Continuous background watcher (`clipbridge -w`).
  * One-shot hotkey sync (`clipbridge -1`).
  * Direct terminal paste (`clipbridge -p`).

Installation
------------
To build and install `clipbridge`:

    make
    make install

Usage
-----
```
clipbridge [-w1pruv] [-m mode] [-t table] [-l link]
```

### Actions
* `-w`: Watch clipboard continuously in background and auto-format (default).
* `-1`: Perform a single one-shot sync and exit (ideal for hotkey bindings).
* `-p`: Print formatted clipboard content directly to standard output.

### Formatting Options
* `-m mode`: Output mode: `plain` (default), `markdown`, `terminal`.
* `-t table`: Table format: `grid` (default), `markdown`, `tsv`, `simple`.
* `-l link`: Link format: `bracket` (default), `inline`, `text`, `footnote`.
* `-u`: Use Unicode box-drawing characters for tables.
* `-r`: Emit Windows CRLF (`\r\n`) line endings.
* `-v`: Print version information.
* `-h`: Display help message.

Desktop Integration & Hotkeys
-----------------------------
### Windows (Startup / Autostart)
To have `clipbridge` monitor the clipboard automatically on Windows:
1. Compile `clipbridge.exe` or place it in your path.
2. Create a shortcut to `clipbridge.exe` in `shell:startup`.

### Linux (i3 / Sway / Hyprland Hotkeys)
Bind a hotkey to format the current clipboard on demand:

**i3 / Sway (`~/.config/sway/config` or `~/.config/i3/config`):**
```
bindsym $mod+Alt+v exec clipbridge -1
```

**Hyprland (`~/.config/hypr/hyprland.conf`):**
```
bind = $mainMod ALT, V, exec, clipbridge -1
```

### Linux (Systemd User Service)
Create `~/.config/systemd/user/clipbridge.service`:
```ini
[Unit]
Description=Universal Clipboard Bridge Daemon
After=graphical-session.target

[Service]
ExecStart=/usr/local/bin/clipbridge -w
Restart=always

[Install]
WantedBy=graphical-session.target
```
Enable and start:
```bash
systemctl --user enable --now clipbridge
```

License
-------
MIT License. See LICENSE file for details.
