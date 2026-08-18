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
* **Zero external dependencies**: Built in pure C99 with standard POSIX, Win32, and AppKit APIs.
* **Non-intrusive**: Leaves the original rich HTML payload untouched on the clipboard while seamlessly upgrading the plaintext fallback.
* **Password Manager & Privacy Aware**: Automatically checks for and respects `Clipboard Viewer Ignore`, `CanIncludeInClipboardHistory`, and `org.nspasteboard.ConcealedType` privacy exclusion flags (e.g. from 1Password, Bitwarden, KeePassXC, Apple Keychain).
* **Multi-platform support**:
  * **macOS**: Native Cocoa/AppKit `NSPasteboard` integration with changeCount tracking.
  * **Windows**: Native Win32 `AddClipboardFormatListener` (0% CPU when idle, zero polling).
  * **Linux (Wayland)**: Works with `wl-clipboard` (`wl-paste` / `wl-copy`).
  * **Linux (X11)**: Works with `xclip` or `xsel`.
* **Flexible operation modes**:
  * Continuous background watcher (`clipbridge -w`).
  * One-shot hotkey synchronization (`clipbridge -1`).
  * Direct terminal paste (`clipbridge -p`).

Installation & Platform Setup
-----------------------------

### Option A: The "You Do It" / Suckless DIY Approach (Recommended)
True to suckless software philosophy, `clipbridge` requires zero packaging layers and can be compiled and configured directly with standard POSIX tooling:

#### 1. Compile & Install from Source (macOS, Linux, BSD)
```sh
git clone https://github.com/riccivr/clipbridge.git
cd clipbridge
make
sudo make install
```
*(On macOS, `make` links against `-framework AppKit -framework Foundation` using native clang with 0 external dependencies).*

#### 2. Manual Background Daemon on macOS (Launchd)
Run `clipbridge` as a native user daemon on macOS without third-party app wrappers:
```sh
mkdir -p ~/Library/LaunchAgents
cat << 'EOF' > ~/Library/LaunchAgents/com.riccivr.clipbridge.plist
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.riccivr.clipbridge</string>
    <key>ProgramArguments</key>
    <array>
        <string>/usr/local/bin/clipbridge</string>
        <string>-w</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
</dict>
</plist>
EOF
launchctl load ~/Library/LaunchAgents/com.riccivr.clipbridge.plist
```

#### 3. Manual Hotkey Binding on macOS
To trigger one-shot formatting (`clipbridge -1`) with a global keyboard shortcut:
* **With `skhd` (`~/.config/skhd/skhdrc`):**
  ```
  cmd + alt - v : /usr/local/bin/clipbridge -1
  ```
* **With Shortcuts.app:** Create a Quick Action shortcut running `/usr/local/bin/clipbridge -1` and assign a keyboard shortcut.

#### 4. Linux Setup (i3 / Sway / Hyprland / Systemd)
* **i3 / Sway (`~/.config/sway/config`):**
  ```
  bindsym $mod+Alt+v exec clipbridge -1
  ```
* **Hyprland (`~/.config/hypr/hyprland.conf`):**
  ```
  bind = $mainMod ALT, V, exec, clipbridge -1
  ```
* **Systemd User Daemon (`~/.config/systemd/user/clipbridge.service`):**
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
  Enable and start: `systemctl --user enable --now clipbridge`

#### 5. Windows Manual Setup
Compile `clipbridge.exe` and create a shortcut in `shell:startup`.

---

### Option B: Package Managers

#### Homebrew (macOS & Linux)
```sh
brew tap riccivr/tap
brew install clipbridge

# To run as background daemon:
brew services start clipbridge
```

#### Arch Linux (AUR)
```sh
yay -S clipbridge
# or with paru:
paru -S clipbridge
```

#### Debian / Ubuntu (.deb)
Download the `.deb` package from [Releases](https://github.com/riccivr/clipbridge/releases) or build locally:
```sh
sudo dpkg -i clipbridge_1.1.0_amd64.deb
```

#### Windows (Chocolatey)
```powershell
choco install clipbridge
```

---

### Option C: Pre-built Binary Packages (Releases)
Pre-compiled standalone binaries and installer images are available on the [GitHub Releases](https://github.com/riccivr/clipbridge/releases) page:
* **macOS**: `ClipBridge-1.1.0.dmg` (Drag-and-drop `ClipBridge.app` + one-click background daemon activator).
* **Linux (x86_64)**: `clipbridge-v1.1.0-linux-x86_64.tar.gz` (Pre-compiled standalone binary).
* **Windows (x64)**: `clipbridge-v1.1.0-windows-x64.zip` (Pre-compiled standalone `clipbridge.exe`).
* **Debian/Ubuntu**: `clipbridge_1.1.0_amd64.deb`.

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

License
-------
MIT License. See LICENSE file for details.
