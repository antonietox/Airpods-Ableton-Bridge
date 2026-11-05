# AirPods Bridge

Run Ableton Live (or any DAW) at **1024 / 2048 samples** while listening on AirPods.

Made for mixing on the road: AirPods plus a headphone room-emulation plugin (e.g. Acustica Sienna)
is a surprisingly effective travel setup — until a heavy session on an older Mac starts popping
because the buffer can't go above 512.

Recent macOS versions cap the AirPods output buffer at **960 frames**, so DAWs stop at 512 —
not enough for heavy projects. Aggregate devices inherit the same cap.

AirPods Bridge fixes it natively, without BlackHole or Soundflower:

```
DAW (any buffer, 64–2048+) → "AirPods Bridge" virtual device → forwarder → AirPods (≤ 960)
```

- **Virtual device**: a tiny 2-channel, 48 kHz loopback CoreAudio driver (AudioServerPlugIn).
- **Forwarder**: a small background app that feeds the AirPods, with clock-drift correction.
  It only holds the AirPods while audio is flowing and releases them after 60 s of silence,
  so they stay free for your phone and calls.
- **Bonus**: an incoming call no longer crashes your DAW's audio — its device never disappears.

## Install

Requires the Xcode Command Line Tools (`xcode-select --install`).

```sh
git clone https://github.com/antonietox/Airpods-Ableton-Bridge.git
cd Airpods-Ableton-Bridge
./scripts/install.sh
```

The installer asks for your password (to install the audio device) and shows a
**microphone permission** popup — click Allow. macOS treats every audio input as a mic;
nothing records your actual microphone.

Then in your DAW: **Output → AirPods Bridge**, **Input → No device** (or your usual mic),
and choose any buffer size.

## Notes

- Works with any AirPods (the forwarder picks the first output device with "AirPods" in its name).
- Bluetooth adds its own ~150–250 ms delay — great for playback and mixing, not for tracking.
- During a call the AirPods switch to headset mode; the forwarder steps aside and resumes afterwards.
- Tested on an Intel Mac with macOS 15 (Sequoia) and AirPods Pro. Builds universal; Apple Silicon reports welcome.
- Log: `~/Library/Logs/AirPodsBridge.log`

## Alternatives

- [Proxy Audio Device](https://github.com/briankendall/proxy-audio-device) — a general-purpose
  virtual device that forwards all output to another device, with a configurable buffer.
  Same core idea, done inside the driver (no separate app or mic permission), and available
  via Homebrew. AirPods Bridge is narrower: built for AirPods + DAW, it frees the AirPods when
  idle, steps aside during calls and corrects clock drift between the two devices.
- BlackHole / Soundflower + a routing app — works for some, but more moving parts.

## Uninstall

```sh
./scripts/uninstall.sh
```

## License

MIT
