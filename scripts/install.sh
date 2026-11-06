#!/bin/bash
# Builds and installs AirPods Bridge: the virtual audio device + the forwarder that starts at login.
set -e
cd "$(dirname "$0")/.."
APPDIR="$HOME/Applications"

echo "==> Building"
make clean all >/dev/null

echo "==> Installing the virtual device (asks for your password)"
sudo rm -rf /Library/Audio/Plug-Ins/HAL/AirPodsBridge.driver
sudo cp -R build/AirPodsBridge.driver /Library/Audio/Plug-Ins/HAL/
sudo chown -R root:wheel /Library/Audio/Plug-Ins/HAL/AirPodsBridge.driver
sudo killall coreaudiod
sleep 3

echo "==> Installing the forwarder app to $APPDIR"
mkdir -p "$APPDIR" "$HOME/Library/Logs"
pkill -x apbfwd 2>/dev/null || true
rm -rf "$APPDIR/AirPods Bridge.app"
cp -R "build/AirPods Bridge.app" "$APPDIR/"

echo "==> Asking for audio-input permission"
echo "    macOS treats every audio input as a microphone. Click ALLOW on the popup."
echo "    (No popup? System Settings > Privacy & Security > Microphone > enable AirPods Bridge.)"
open -n "$APPDIR/AirPods Bridge.app" --args permit
read -r -p "    Press Enter once you've allowed it... "

echo "==> Starting AirPods Bridge at login (Login Items)"
osascript -e 'tell application "System Events" to delete (every login item whose name is "AirPods Bridge")' >/dev/null 2>&1 || true
osascript -e "tell application \"System Events\" to make login item at end with properties {path:\"$APPDIR/AirPods Bridge.app\", hidden:true}" >/dev/null
pkill -x apbfwd 2>/dev/null || true; sleep 1
open -a "$APPDIR/AirPods Bridge.app"

echo
echo "Done. Look for the headphones icon in your menu bar. In your DAW, set the audio output to 'AirPods Bridge' and pick any buffer size."
echo "Tip: also set System Settings > Sound > Input back to your usual microphone."
