#!/bin/bash
# Builds and installs AirPods Bridge: the virtual audio device + the forwarder that starts at login.
set -e
cd "$(dirname "$0")/.."
LABEL=com.minkiwinki.airpodsbridge
APPDIR="$HOME/Applications"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"

echo "==> Building"
make clean all >/dev/null

echo "==> Installing the virtual device (asks for your password)"
sudo rm -rf /Library/Audio/Plug-Ins/HAL/AirPodsBridge.driver
sudo cp -R build/AirPodsBridge.driver /Library/Audio/Plug-Ins/HAL/
sudo chown -R root:wheel /Library/Audio/Plug-Ins/HAL/AirPodsBridge.driver
sudo killall coreaudiod
sleep 3

echo "==> Installing the forwarder app to $APPDIR"
mkdir -p "$APPDIR" "$HOME/Library/LaunchAgents" "$HOME/Library/Logs"
launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null || true
rm -rf "$APPDIR/AirPods Bridge.app"
cp -R "build/AirPods Bridge.app" "$APPDIR/"

echo "==> Asking for audio-input permission"
echo "    macOS treats every audio input as a microphone. Click ALLOW on the popup."
echo "    (No popup? System Settings > Privacy & Security > Microphone > enable AirPods Bridge.)"
open -n "$APPDIR/AirPods Bridge.app" --args permit
read -r -p "    Press Enter once you've allowed it... "

echo "==> Starting the forwarder at login"
cat > "$PLIST" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>Label</key><string>$LABEL</string>
<key>ProgramArguments</key><array><string>$APPDIR/AirPods Bridge.app/Contents/MacOS/apbfwd</string></array>
<key>RunAtLoad</key><true/>
<key>KeepAlive</key><true/>
<key>ThrottleInterval</key><integer>10</integer>
<key>ProcessType</key><string>Interactive</string>
<key>StandardOutPath</key><string>$HOME/Library/Logs/AirPodsBridge.log</string>
<key>StandardErrorPath</key><string>$HOME/Library/Logs/AirPodsBridge.log</string>
</dict></plist>
PL
pkill -x apbfwd 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$PLIST"

echo
echo "Done. In your DAW, set the audio output to 'AirPods Bridge' and pick any buffer size."
echo "Tip: also set System Settings > Sound > Input back to your usual microphone."
