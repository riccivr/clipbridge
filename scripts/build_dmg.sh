#!/bin/sh
# build_dmg.sh - Build macOS ClipBridge.app bundle and DMG installer disk image
set -e

VERSION="1.1.0"
APP_NAME="ClipBridge"
DMG_NAME="ClipBridge-${VERSION}.dmg"
BUILD_DIR="build"
STAGING_DIR="${BUILD_DIR}/dmg_root"

echo "Building macOS application bundle and DMG for ${APP_NAME} v${VERSION}..."

# Clean previous build artifacts
rm -rf "${BUILD_DIR}" "${DMG_NAME}"
mkdir -p "${STAGING_DIR}/${APP_NAME}.app/Contents/MacOS"
mkdir -p "${STAGING_DIR}/${APP_NAME}.app/Contents/Resources"

# Compile clipbridge if binary doesn't exist
if [ ! -f "clipbridge" ]; then
    make
fi

# Assemble .app bundle
cp clipbridge "${STAGING_DIR}/${APP_NAME}.app/Contents/MacOS/clipbridge"
chmod 755 "${STAGING_DIR}/${APP_NAME}.app/Contents/MacOS/clipbridge"
cp Info.plist "${STAGING_DIR}/${APP_NAME}.app/Contents/Info.plist"
if [ -f "assets/AppIcon.icns" ]; then
    cp assets/AppIcon.icns "${STAGING_DIR}/${APP_NAME}.app/Contents/Resources/AppIcon.icns"
    cp assets/AppIcon.icns "${STAGING_DIR}/.VolumeIcon.icns"
    cp assets/tray_*.png "${STAGING_DIR}/${APP_NAME}.app/Contents/Resources/" 2>/dev/null || true
    if command -v SetFile >/dev/null 2>&1; then
        SetFile -a C "${STAGING_DIR}"
    fi
fi

# Add /Applications symlink for standard drag-and-drop installation
ln -s /Applications "${STAGING_DIR}/Applications"

# Add LaunchAgent helper installer command
cat << 'EOF' > "${STAGING_DIR}/Enable_Background_Daemon.command"
#!/bin/bash
PLIST_DIR="$HOME/Library/LaunchAgents"
PLIST_FILE="${PLIST_DIR}/com.riccivr.clipbridge.plist"

mkdir -p "$PLIST_DIR"
cat << 'PLIST' > "$PLIST_FILE"
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.riccivr.clipbridge</string>
    <key>ProgramArguments</key>
    <array>
        <string>/Applications/ClipBridge.app/Contents/MacOS/clipbridge</string>
        <string>-w</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
</dict>
</plist>
PLIST

launchctl unload "$PLIST_FILE" 2>/dev/null || true
launchctl load "$PLIST_FILE"
echo "ClipBridge background daemon successfully enabled and loaded!"
read -p "Press Enter to finish..."
EOF
chmod 755 "${STAGING_DIR}/Enable_Background_Daemon.command"

# Copy documentation
cp README.md "${STAGING_DIR}/"
cp LICENSE "${STAGING_DIR}/"

# Build DMG using hdiutil if available (on macOS / CI)
if command -v hdiutil >/dev/null 2>&1; then
    echo "Creating DMG disk image with hdiutil..."
    hdiutil create \
        -volname "${APP_NAME}" \
        -srcfolder "${STAGING_DIR}" \
        -ov \
        -format UDZO \
        "${DMG_NAME}"
    echo "Successfully created ${DMG_NAME}!"
else
    echo "Notice: hdiutil is only available on macOS. Staging directory prepared at ${STAGING_DIR}."
fi
