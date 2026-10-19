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
to a Mac Mini Moonlight client. SDL's disk audio backend recorded the decoded
48 kHz stereo float samples while separate 880 and 1320 Hz processes were running.
Median amplitudes over 25 ms windows were 0.08584651 (440 Hz), 0.00002128
(880 Hz), and 0.00000642 (1320 Hz). Short windows avoid cancellation across packet
loss/resynchronization discontinuities; a long-window single-bin measurement was
unstable and was not used as a pass criterion. These figures characterize tone
isolation, not network quality or audible glitching.

Disconnect/reconnect selected the same application PID (8612). Before reconnect,
median amplitudes were 0.08589802 / 0.00002255 / 0.00000651; after reconnect,
0.08587300 / 0.00002293 / 0.00000631. The stock Sunshine instances and cosmic's
active streams were left running.

A GUI bootstrap which exited immediately after starting its child also passed:
Sunshine selected the surviving child's PID (29996), and the decoded stream
contained its 440 Hz tone. A `cmd /c start` wrapper produced multiple independent
job roots; Sunshine logged the ambiguity and sent no audio. This is an intentional
unsupported case, not a fallback to device capture. Launch one tracked application
tree per slot, and keep `wait-all` enabled for bootstrapped applications.

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
