# SAF Beetle

**SAF Beetle** (BTLE: Bluetooth Loss Emulator) is a real-time audio effect that models a
failing packetised headphone link. It is intended to create the frozen fragments,
bursty dropouts, temporal jumps, stereo smearing, and flange-like comb filtering
heard when a Bluetooth receiver repeatedly loses and reacquires its stream.

This is a perceptual effect, not a bit-exact Bluetooth stack or RF simulator.

## Current milestone

The repository contains:

- an allocation-free C++20 DSP core, independent of any plugin framework;
- a burst-loss state model rather than uniformly random dropouts;
- packet repetition, muting, jitter, forward/backward frame swaps, and stutters;
- clock drift with abrupt receive-buffer resynchronisation;
- independent right-channel lag for phasey stereo failures;
- a constant 50 ms delayed dry path and host-reported plugin latency;
- deterministic pattern seeds and host-block-size-independent rendering;
- a JUCE VST3/standalone wrapper using the generic parameter editor;
- focused DSP tests and a ten-second WAV demo renderer.

The clean path is deliberately delayed by the same amount as the damaged path.
Consequently, `Mix` creates comb filtering only when the simulated receiver moves
away from its expected packet position.

## Controls

| Control | Behaviour |
| --- | --- |
| Signal Quality | Master health of the simulated link. Lower values increase all failures. |
| Packet Size | Duration of each independently damaged audio frame. |
| Burstiness | How likely a loss continues after the first missing packet. |
| Jitter | Reads nearby packets early or late. |
| Temporal Swap | Emits a future frame and then the displaced earlier frame. |
| Stutter | Freezes one packet and repeats it several times. |
| Stereo Desync | Lets the right channel lag for short phasey/flanged failures. |
| Clock Drift | Slowly changes the receive position, then performs a hard resync. |
| Mix | Blends the latency-aligned clean and damaged receive paths. |
| Output | Output trim in dB. |
| Pattern Seed | Reproduces the same fault sequence for the same automation. |

## Build on Windows

JUCE 9.0.0 is fetched only by the plugin preset. Review JUCE's licence and select
the appropriate terms for your use before distributing a binary.

From an x64 Visual Studio developer terminal (the NMake generator needs the
compiler environment to be initialised):

```powershell
cmake --preset windows-core
cmake --build --preset core-release
ctest --preset core-release
```

To build the VST3 and standalone application:

```powershell
cmake --preset windows-plugin
cmake --build --preset plugin-release
ctest --preset plugin-release
```

The plugin artifact is generated below:

```text
build/plugin-release/SAF_BEETLE_artefacts/Release/VST3/SAF Beetle.vst3
```

Copy that bundle to the standard per-user or system VST3 directory and rescan
plugins in FL Studio. `COPY_PLUGIN_AFTER_BUILD` is intentionally disabled so a
normal build never writes into system plugin directories.

## Render the diagnostic demo

After the core build:

```powershell
build/core-release/saf_beetle_render.exe
```

This writes `saf_beetle_demo.wav`: approximately 2.5 seconds of near-clean audio,
followed by three increasingly damaged sections. The WAV is ignored by Git.

## DSP constraints

- The audio callback performs no heap allocation, file access, logging, or locks.
- All event decisions occur at internal packet boundaries, independently of the
  host's buffer size.
- Latency remains fixed at 50 ms even when packet size changes, avoiding dynamic
  plugin-delay-compensation changes in the DAW.
- The current UI is JUCE's generic editor. A packet timeline and SAF visual design
  belong to the next milestone after the sound model is tuned.
