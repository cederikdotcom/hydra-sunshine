# Hydra process audio fork (#844)

Based on upstream Sunshine `v2025.924.154138` (`86188d47a7463b0f73b35de18a628353adeaa20e`).
The default remains `audio_capture_mode = device`. The optional Windows
`audio_capture_mode = process` captures only the process tree launched by this
Sunshine instance. Missing or ambiguous targets fail without system-audio fallback.
See [configuration](configuration.md#audio_capture_mode).

The activation callback must expose `IAgileObject`; aggregating the free-threaded
marshaler alone caused `E_ILLEGAL_METHOD_CALL` on the tested machines. Process
loopback also returns `E_NOTIMPL` for `GetDevicePeriod`, so capture uses a bounded
20 ms fallback. Application targets come from a retained handle to the launch's
Windows job, including when an Unreal bootstrap exits and its Shipping child
remains. Capture never attaches to an independently launched process by name.

## Build and test

Use the upstream Windows/MSYS2 UCRT64 dependencies and initialize the submodules.
The tested compiler was GCC 16.2.0. This fork detects whether the MinGW SDK already
provides `HSYNTHETICPOINTERDEVICE` to avoid duplicate compatibility declarations.

```bash
export BUILD_VERSION=2025.924.154138-hydra.844
export BRANCH=fork/process-loopback-audio
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_DOCS=OFF -DBUILD_TESTS=OFF -DBUILD_WERROR=ON \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DSUNSHINE_ASSETS_DIR=assets
cmake --build build --target sunshine web-ui --parallel 4

g++ -std=c++20 -O2 -static -Isrc/platform/windows \
  tools/process_audio_probe.cpp -o process_audio_probe.exe \
  -lole32 -loleaut32 -luuid -lwinmm
```

In an interactive Windows session with a functioning audio output:

```powershell
powershell -File tools/test-process-audio.ps1 -Probe ./process_audio_probe.exe
```

The harness starts simultaneous 440, 880 and 1320 Hz sources, captures each process
and an unrelated silent process, and checks wanted amplitude > 0.01 and each other
tone < 0.1% of the wanted tone. It stops only its own sources. The production COM
regression tests live in `tests/unit/platform/test_process_audio.cpp`.

## Recorded checks (2026-10-19)

Windows build and web UI build passed on chunky. Two COM regression tests passed.
Measured three-source amplitudes using the production activation helper:

| Captured target | 440 Hz | 880 Hz | 1320 Hz |
|---|---:|---:|---:|
| 440 Hz process | 0.08562072 | 0.00001870 | 0.00000292 |
| 880 Hz process | 0.00000885 | 0.08562726 | 0.00000852 |
| 1320 Hz process | 0.00000952 | 0.00000917 | 0.08562782 |
| Unrelated process | 0 | 0 | 0 |

A separate real Mercator Shipping process on cosmic produced 95 nonzero buffers,
peak RMS 0.025766, after the Start button was clicked. This verifies application
audio capture; it does not verify microphone input or a complete spoken dialogue.

An isolated fork server on chunky (port 49289) launched its own tone application,
selected that application's PID, initialized 48 kHz stereo Opus, and streamed audio
to a Mac Mini Moonlight client. The client received audio packets. The stock
Sunshine instances and cosmic's active streams were left running.

## Rollout

This is an opt-in fork, not a fleet deployment. Install it on every Sunshine slot
before enabling process mode. HydraBody's #844 change inherits the mode and enables
extra-slot audio only when the base configuration explicitly selects `process`.
Stock Sunshine ignores unknown config options: never enable this option with a
stock executable. Remove the option and regenerate disabled extra-slot audio
configs before rolling back to stock. Perform changes when all slots are idle.

Three simultaneous real game streams, reconnection/relaunch and the separate
microphone/voice route still need a venue acceptance check before declaring the
three-iPad experience complete.
