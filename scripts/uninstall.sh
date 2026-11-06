#!/bin/bash
# Removes AirPods Bridge completely.
osascript -e 'tell application "System Events" to delete (every login item whose name is "AirPods Bridge")' >/dev/null 2>&1
pkill -x apbfwd 2>/dev/null
rm -rf "$HOME/Applications/AirPods Bridge.app"
echo "Removing the virtual device (asks for your password)"
sudo rm -rf /Library/Audio/Plug-Ins/HAL/AirPodsBridge.driver
sudo killall coreaudiod
echo "AirPods Bridge removed."
