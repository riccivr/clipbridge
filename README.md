<p align="center">
  <img src="assets/logo.png" alt="ClipBridge Logo" width="220" />
</p>

<h1 align="center">ClipBridge</h1>
<p align="center"><strong>Universal Clipboard Formatting Bridge &amp; Background Daemon</strong></p>

`clipbridge` runs as a background daemon or one-shot command that formats clipboard HTML using the [`unipaste`](https://github.com/riccivr/unipaste) engine.

The Problem
-----------
When you copy rich text or tables from Slack, Microsoft Teams, Discord, Google Chrome, or other web apps, the clipboard receives rich HTML and a plain-text fallback. Most apps generate flat plain text with missing table columns, lost code block indentation, broken lists, and missing URLs.

When you paste into plain-text targets (Notepad, terminals, vim, code editors), the target app reads the plain-text slot and outputs unaligned text.

How clipbridge Works
--------------------
`clipbridge` listens for clipboard updates with native OS event hooks. When an application puts HTML on the clipboard, `clipbridge` runs the HTML through the `unipaste` engine and writes formatted plain text (tables, code blocks, lists, links) into the plain-text clipboard slot. It leaves the original HTML slot unchanged.

Features
--------
* **Self-contained engine**: In-tree vendored `unipaste` C99 parsing and formatting engine (`vendor/unipaste/`).
* **Format preservation**: Leaves the rich HTML slot untouched while offering formatted plain text (`CF_UNICODETEXT` on Windows, `NSPasteboardTypeString` on macOS, `text/plain` on Linux).
* **Privacy protection**: Automatically skips clipboard updates marked private by password managers (`Clipboard Viewer Ignore`, `CanIncludeInClipboardHistory`, `CanUploadToCloudStore`, `x-kde-passwordManagerHint`, and `org.nspasteboard.ConcealedType` from 1Password, Bitwarden, KeePassXC, and Apple Keychain).
* **Platform backends**:
  * **Windows**: Native pure Win32 API (`AddClipboardFormatListener`, tray icon, registry persistence, hotkeys, 0 external dependencies).
  * **macOS**: Native Cocoa/AppKit `NSPasteboard` changeCount tracking.
  * **Linux (Wayland)**: Uses `wl-clipboard` (`wl-paste` and `wl-copy`).
  * **Linux (X11)**: Uses `xclip` or `xsel`.
* **Modes**:
  * Background daemon (`clipbridge -w`).
  * One-shot hotkey sync (`clipbridge -1`).
  * Direct terminal paste or stream filter (`clipbridge -p`).
  * Active window paste simulation (`clipbridge -k`).

Build and Install
-----------------

### Build from Source
```sh
git clone https://github.com/riccivr/clipbridge.git
cd clipbridge
make
sudo make install
```
On macOS, `make` links against `-framework AppKit -framework Foundation` using clang.

---

### Package Managers

#### Homebrew (macOS and Linux)
```sh
brew tap riccivr/tap
brew install clipbridge

# To start as a background service:
brew services start clipbridge
```

#### Arch Linux (AUR)
```sh
yay -S clipbridge
```

#### Debian and Ubuntu (.deb)
```sh
sudo dpkg -i clipbridge_1.3.0_amd64.deb
```

#### Windows (Chocolatey)
```powershell
choco install clipbridge
```

---

### Pre-built Installers and Binaries
Download pre-built packages from [GitHub Releases](https://github.com/riccivr/clipbridge/releases):
* **macOS (Universal)**: `ClipBridge-1.3.0.dmg` (Universal binary for **Apple Silicon M1/M2/M3/M4 + Intel**).
* **Windows (x64 Installer)**: `clipbridge-setup.exe` (installs to `%LOCALAPPDATA%\ClipBridge` and creates Start Menu launcher).
* **Windows (x64 Portable)**: `clipbridge-portable.exe` and `clipbridge-v1.3.0-windows-x64.zip` (standalone executable).
* **Debian / Ubuntu (x86_64)**: `clipbridge_1.3.0_amd64.deb`.
* **Debian / Ubuntu (ARM64)**: `clipbridge_1.3.0_arm64.deb` (Raspberry Pi 64-bit / ARM servers).
* **Linux (x86_64 Tarball)**: `clipbridge-v1.3.0-linux-x86_64.tar.gz`.
* **Linux (ARM64 Tarball)**: `clipbridge-v1.3.0-linux-arm64.tar.gz`.

Setup and Hotkeys
-----------------

### macOS Background Daemon (Launchd)
To run `clipbridge` automatically on login without third-party app wrappers:
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

### macOS Global Hotkey
To format on demand (`clipbridge -1`) using a keyboard shortcut:
* With `skhd` (`~/.config/skhd/skhdrc`):
  ```
  cmd + alt - v : /usr/local/bin/clipbridge -1
  ```
* With Shortcuts.app: Create a Quick Action that runs `/usr/local/bin/clipbridge -1` and assign a shortcut.

### Linux Hotkeys and Systemd
* i3 / Sway (`~/.config/sway/config`):
  ```
  bindsym $mod+Alt+v exec clipbridge -1
  ```
* Hyprland (`~/.config/hypr/hyprland.conf`):
  ```
  bind = $mainMod ALT, V, exec, clipbridge -1
  ```
* Systemd user service (`~/.config/systemd/user/clipbridge.service`):
  ```ini
  [Unit]
  Description=Clipboard formatting daemon
  After=graphical-session.target

  [Service]
  ExecStart=/usr/local/bin/clipbridge -w
  Restart=always

  [Install]
  WantedBy=graphical-session.target
  ```
  Enable and start: `systemctl --user enable --now clipbridge`

### Windows System Tray & Hotkey
When running `clipbridge.exe` on Windows, it runs silently in the notification tray with zero terminal windows:
* **Normal copy/paste unchanged**: `Ctrl+C` and `Ctrl+V` keep their standard OS behavior by default.
* **Paste with ClipBridge (`Ctrl+Alt+V` / `Win+Alt+V`)**: Formats the clipboard and pastes directly into whatever window is currently active.
* **Right click tray menu**:
  * **Paste with ClipBridge**: Instantly formats and pastes.
  * **Auto-Format Default Paste (Ctrl+V)**: Optional toggle to make standard `Ctrl+V` always auto-format on every copy.
  * **Pause Formatting (15 Minutes)**: Temporarily pause automatic clipboard rewriting.
  * **Strip URL Tracking Parameters**: Strip telemetry & tracking (`utm_*`, `fbclid`, `gclid`, etc.).
  * **Output Mode**: Plain Text, GitHub Markdown, Slack/Discord mrkdwn, Jira/Confluence, or Terminal ANSI.
  * **Table Style**: ASCII Box, Unicode Grid, Markdown Pipe, TSV.
  * **Language**: Auto (System Default), English, Español.
  * **Start with Windows**: Toggle automatic startup at login.
  * **Exit ClipBridge**: Stop and remove tray icon.

Usage
-----
```
clipbridge [-w1kpruvKh] [-m mode] [-t table] [-l link] [--uninstall] [file ...]
```

### Actions
* `-w`: Watch clipboard in background and listen for hotkeys (default).
* `-k`: Format clipboard and paste directly into active focused window.
* `-1`: Format current clipboard once and update clipboard text.
* `-p`: Print formatted clipboard content directly to stdout (or filter piped input).
* `--uninstall`: Remove desktop shortcuts, startup registrations, and configuration.

### Formatting Options
* `-m mode`: Output mode: `plain` (default), `markdown`, `slack`, `jira`, `terminal`.
* `-t table`: Table format: `grid` (default), `markdown`, `tsv`, `simple`.
* `-l link`: Link format: `bracket` (default), `inline`, `text`, `footnote`.
* `-K`: Keep URL tracking & telemetry parameters (stripped by default).
* `-u`: Use Unicode box-drawing characters for tables (`┌─┬─┐`).
* `-r`: Emit Windows CRLF (`\r\n`) line endings.
* `-v`: Print version.
* `-h`: Display help message.

Acknowledgments & Credits
-------------------------
* **Brand & Icon Design**: Created with care by **Estefani Medina** ([@estephmediseno](https://github.com/estephmediseno)). Special thanks to Estefani for designing the ClipBridge brand emblem.
* **Core Conversion Engine**: Powered by **[unipaste](https://github.com/riccivr/unipaste)**.

