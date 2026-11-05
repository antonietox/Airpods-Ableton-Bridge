#!/bin/bash
# Removes AirPods Bridge completely.
LABEL=com.rapsodica.airpodsbridge
launchctl bootout "gui/$(id -u)/$LABEL" 2>/dev/null
rm -f "$HOME/Library/LaunchAgents/$LABEL.plist"
rm -rf "$HOME/Applications/AirPods Bridge.app"
echo "Removing the virtual device (asks for your password)"
sudo rm -rf /Library/Audio/Plug-Ins/HAL/AirPodsBridge.driver
sudo killall coreaudiod
echo "AirPods Bridge removed."
