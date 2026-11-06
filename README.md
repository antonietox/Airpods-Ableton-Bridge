# AirPods Bridge

Run Ableton Live (or any DAW) at **1024 / 2048 samples** while listening on AirPods.

AirPods plus a room-emulation plugin is a surprisingly effective travel setup, until a heavy session
starts popping because the buffer can't go above 512.

Recent macOS versions cap the AirPods output buffer at **960 frames**, so DAWs stop at 512.

AirPods Bridge fixes it natively:

```
DAW (any buffer, 64–2048+) → "AirPods Bridge" virtual device → forwarder → AirPods (≤ 960)
```

- **Virtual device**: a tiny 2-channel, 48 kHz loopback CoreAudio driver (AudioServerPlugIn).
- **Forwarder**: a small background app that feeds the AirPods, with clock-drift correction.
  It only holds the AirPods while audio is flowing and releases them after 60 s of silence,
  so they stay free for your phone and calls.
- **Bonus**: an incoming call no longer crashes your DAW's audio.

## Install

Needs the Xcode Command Line Tools (`xcode-select --install`).

```sh
git clone https://github.com/antonietox/Airpods-Ableton-Bridge.git
cd Airpods-Ableton-Bridge
./scripts/install.sh
```

The installer asks for your password once, then shows a microphone popup: click Allow.
macOS treats any audio input as a mic; your real microphone is never touched.

Then in your DAW: **Output → AirPods Bridge**, pick your buffer, play.

## Good to know

- AirPods only, for now: the forwarder looks for an output device with "AirPods" in its name.
- It runs at 48 kHz, so your DAW will too. Same rate the AirPods use anyway.
- The orange mic dot only shows while your DAW is playing into the Bridge with AirPods connected.
- The Bridge never becomes your Mac's default input or output on its own. You pick it in your DAW.
- Bluetooth still adds its own ~150–250 ms delay: great for playback and mixing, not for tracking.
- Tested on an Intel Mac, macOS 15, AirPods Pro. Builds universal; Apple Silicon reports welcome.
- Log: `~/Library/Logs/AirPodsBridge.log`

## Alternatives

- [Proxy Audio Device](https://github.com/briankendall/proxy-audio-device): a general-purpose virtual
  device that forwards all output to another device, with a configurable buffer. Same core idea,
  done inside the driver. AirPods Bridge is narrower: built for AirPods + DAW, it frees the AirPods
  when idle, steps aside during calls and corrects clock drift.
- BlackHole / Soundflower + a routing app: works for some, more moving parts.

## Uninstall

```sh
./scripts/uninstall.sh
```

## License

MIT
