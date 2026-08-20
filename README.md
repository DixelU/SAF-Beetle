# SAF Beetle

**SAF Beetle** (BTLE: Bluetooth Loss Emulator) is a real-time audio effect that models a
failing packetised headphone link. It is intended to create the frozen fragments,
bursty glitches, hard packet dropouts, temporal jumps, stereo smearing, and flange-like
comb filtering heard when a Bluetooth receiver repeatedly loses and reacquires its stream.

This is a perceptual effect, not a bit-exact Bluetooth stack or RF simulator.

## Current milestone

The repository contains:

- an allocation-free C++20 DSP core, independent of any plugin framework;
- burst-shaped corruption events rather than uniformly random glitches;
- isolated packet dropout, repetition, jitter, forward/backward frame swaps, and stereo lag;
- clock drift with abrupt receive-buffer resynchronisation;
- independent right-channel lag for phasey stereo failures;
- a constant 50 ms delayed dry path and host-reported plugin latency;
- deterministic pattern seeds and host-block-size-independent rendering;
- a JUCE VST3/standalone wrapper with a compact custom dark editor;
- focused DSP tests and a ten-second WAV demo renderer.

The clean path is deliberately delayed by the same amount as the damaged path.
Consequently, `Mix` creates comb filtering only when the simulated receiver moves
away from its expected packet position.

## Controls

| Control | Behaviour |
| --- | --- |
| Signal Quality | Master intensity. Lower values increase the selected corruption modes. |
| Packet Size | Duration of each independently damaged audio frame. |
| Boundary Smooth | Crossfades packet transitions over up to half a packet. `0%` preserves hard boundaries; higher values suppress clicks, including dropout edges. |
| Burstiness | How often a new selected corruption burst is introduced. |
| Burst Length | Duration of a corruption event in packets, from 1 to 1000. |
| Burst Variance | Randomises each event length by up to one octave shorter or longer. |
| Jitter | Reads nearby packets early or late. |
| Temporal Swap | Exchanges equally long future and displaced packet ranges. |
| Stutter | Freezes one packet for the chosen burst duration. |
| Packet Dropout | Replaces complete packet bursts with digital silence. Defaults to `0%` for compatibility with existing projects. |
| Stereo Desync | Lets the right channel lag for the chosen burst duration. |
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
- Packet-boundary smoothing uses an allocation-free raised-cosine crossfade and
  remains exactly transparent when consecutive packets already form one stream.
  Dropout entry and exit use the same crossfade when smoothing is enabled; `0%`
  retains the abrupt packet cut.
- Corruption modes are isolated: a zeroed knob cannot inject or continue its
  effect, and the link controls only shape the modes that are enabled.
- Latency remains fixed at 50 ms even when packet size changes, avoiding dynamic
  plugin-delay-compensation changes in the DAW.
- The custom editor keeps all controls on one compact surface and preserves the
  same host-automatable parameter IDs used by existing projects.
