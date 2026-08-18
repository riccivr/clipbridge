clipbridge - universal clipboard bridge powered by unipaste
========================================================================
`clipbridge` is a lightweight, cross-platform clipboard listener and background
daemon that integrates desktop graphical clipboards directly with the
[`unipaste`](https://github.com/riccivr/unipaste) formatting engine.

The Problem
-----------
When you copy rich content from modern desktop applications (such as Slack,
Microsoft Teams, Discord, Google Chrome, web email, or Electron apps), the system
clipboard receives multiple formats: rich HTML (`CF_HTML` / `text/html`) and a
default plain-text representation (`CF_UNICODETEXT` / `text/plain`).

However, the application-generated fallback plain text is almost always
**flattened and mangled**:
* Markdown and HTML tables collapse into unaligned, single-line text blobs.
* Code blocks lose indentation, line breaks, and language syntax tags.
* Bullet points and numbered task lists lose hierarchy.
* Hyperlinks lose their target URLs completely.

When you paste into plain-text destinations (**Notepad.exe**, terminal, vim,
nano, or IDE source code files), the target application only accepts plain text,
resulting in unreadable output.

How clipbridge Works
--------------------
`clipbridge` monitors system clipboard updates using native, zero-polling OS
event listeners. Whenever rich HTML is placed on the clipboard, `clipbridge`
passes the HTML payload through the **`unipaste` formatting engine** and
upgrades the plain-text clipboard slot with perfectly aligned tables, code fences,
lists, and clean typography:

```
┌─────────────────────────────────────────────────────────────┐
│ Source App (Slack / Teams / Chrome / Web Browser / Discord) │
└──────────────────────────────┬──────────────────────────────┘
                               │ Copy (Ctrl+C / Cmd+C)
                               ▼
┌─────────────────────────────────────────────────────────────┐
│                    System Clipboard                         │
│  ├── [HTML Format]       : <table>...<tr><td>...            │
│  └── [Plain Text Slot]   : Flawed / flattened text          │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               │ OS Event Listener
                               ▼
┌─────────────────────────────────────────────────────────────┐
│                         clipbridge                          │
│                              │                              │
│                              ▼                              │
│          ┌───────────────────────────────────────┐          │
│          │       unipaste Formatting Engine      │          │
│          │  • Dynamic ASCII / Markdown Tables    │          │
│          │  • Code Block & Language Preservation │          │
│          │  • Hierarchical Lists & Checkboxes    │          │
│          │  • HTML Entity & Unicode Decoding     │          │
│          │  • Privacy & Secret Flag Awareness    │          │
│          └───────────────────┬───────────────────┘          │
│                              │                              │
│  Updates Plain Text Slot ────┘                              │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────┐
│ Destination App (Notepad.exe / Terminal / Vim / Source Code)│
│                 Ctrl+V -> Clean, formatted text!            │
└─────────────────────────────────────────────────────────────┘
```

Features
--------
* **Powered by `unipaste`**: Leverages the high-performance, memory-safe, zero-dependency C99 parsing and formatting engine from `unipaste`.
* **Zero external dependencies**: Built in pure C99 with standard POSIX and Win32 APIs.
* **Non-intrusive**: Leaves the original rich HTML payload untouched on the clipboard while seamlessly upgrading the plaintext fallback.
* **Password Manager & Privacy Aware**: Automatically checks for and respects `Clipboard Viewer Ignore`, `CanIncludeInClipboardHistory`, and privacy exclusion flags (e.g. from 1Password, Bitwarden, KeePassXC).
* **Multi-platform support**:
  * **Windows**: Native Win32 `AddClipboardFormatListener` (0% CPU when idle, zero polling).
  * **Linux (Wayland)**: Works with `wl-clipboard` (`wl-paste` / `wl-copy`).
  * **Linux (X11)**: Works with `xclip` or `xsel`.
  * **macOS**: Works with `pbpaste` / `pbcopy`.
* **Flexible operation modes**:
  * Continuous background watcher (`clipbridge -w`).
  * One-shot hotkey synchronization (`clipbridge -1`).
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

### Formatting Options (Forwarded to unipaste)
* `-m mode`: Output mode: `plain` (default), `markdown`, `terminal`.
* `-t table`: Table format: `grid` (default), `markdown`, `tsv`, `simple`.
* `-l link`: Link format: `bracket` (default), `inline`, `text`, `footnote`.
* `-u`: Use Unicode box-drawing characters for tables (`┌─┬─┐`).
* `-r`: Emit Windows CRLF (`\r\n`) line endings.
* `-v`: Print version information.
* `-h`: Display help message.

Desktop Integration & Hotkeys
-----------------------------
### Windows (Startup / Autostart)
To have `clipbridge` monitor the clipboard automatically on Windows:
1. Compile `clipbridge.exe` or place it in your `%PATH%`.
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
Description=Universal Clipboard Bridge Daemon (powered by unipaste)
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
