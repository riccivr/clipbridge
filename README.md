clipbridge
==========
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
* Uses the `unipaste` C99 parsing and formatting engine.
* Zero external runtime dependencies.
* Leaves the rich HTML slot untouched while updating the plain-text slot.
* Skips clipboard updates marked private by password managers (`Clipboard Viewer Ignore`, `CanIncludeInClipboardHistory`, and `org.nspasteboard.ConcealedType` from 1Password, Bitwarden, KeePassXC, and Apple Keychain).
* Platform support:
  * macOS: Native Cocoa/AppKit `NSPasteboard` changeCount tracking.
  * Windows: Native Win32 `AddClipboardFormatListener` event loop.
  * Linux (Wayland): Uses `wl-clipboard` (`wl-paste` and `wl-copy`).
  * Linux (X11): Uses `xclip` or `xsel`.
* Modes:
  * Background daemon (`clipbridge -w`).
  * One-shot hotkey sync (`clipbridge -1`).
  * Direct terminal paste (`clipbridge -p`).

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
sudo dpkg -i clipbridge_1.1.0_amd64.deb
```

#### Windows (Chocolatey)
```powershell
choco install clipbridge
```

---

### Pre-built Binaries and DMG
Download pre-built packages from [GitHub Releases](https://github.com/riccivr/clipbridge/releases):
* macOS: `ClipBridge-1.1.0.dmg` (`ClipBridge.app` plus a daemon installer script).
* Windows: `clipbridge-v1.1.0-windows-x64.zip` (`clipbridge.exe`).
* Linux: `clipbridge-v1.1.0-linux-x86_64.tar.gz`.
* Debian / Ubuntu: `clipbridge_1.1.0_amd64.deb`.

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

Usage
-----
```
clipbridge [-w1pruv] [-m mode] [-t table] [-l link]
```

### Actions
* `-w`: Watch clipboard continuously in background and format updates (default).
* `-1`: Format current clipboard once and exit (for hotkey bindings).
* `-p`: Print formatted clipboard content to stdout.

### Formatting Options
* `-m mode`: Output mode: `plain` (default), `markdown`, `terminal`.
* `-t table`: Table format: `grid` (default), `markdown`, `tsv`, `simple`.
* `-l link`: Link format: `bracket` (default), `inline`, `text`, `footnote`.
* `-u`: Use Unicode box-drawing characters for tables (`┌─┬─┐`).
* `-r`: Emit Windows CRLF (`\r\n`) line endings.
* `-v`: Print version.
* `-h`: Display help message.

License
-------
MIT License. See LICENSE file for details.
