# Ghost Delay

A spectral delay audio plugin with Blender-rendered UI. Splits your signal into individual frequency bands via FFT and delays each one independently -- chords unfold in time like a prism splitting light.

![Ghost Delay](Resources/led_on.png)

## Knobs

| Knob | What it does |
|------|-------------|
| **TIME** | Spectral delay length. Quadratic curve, up to ~3 seconds. |
| **FDBK** | Feedback with self-oscillation. Goes above unity with tanh saturation. Cross-bin bleed at high settings. |
| **MIX** | Dry/wet crossfade. |
| **FREEZE** | Continuous spectral freeze. Full lock with slow spectral drift. |
| **TILT** | Spectral dispersion. 4x range -- lows and highs arrive at completely different times. |
| **SPREAD** | Stereo phase scattering. 6π rotation with random per-bin offsets. |
| **DIR** | Forward/reverse spectral playback. Below 0.2: frequency mirroring (swaps highs/lows). |
| **ENV** | Spectral gate. At 0: only loudest frequencies survive. Per-bin envelope followers. |

## DSP Engine

- 1024-point FFT, 75% overlap (256 hop)
- 513 spectral bins with independent delay lines (512 frames deep)
- Per-bin feedback with soft-clip saturation
- Cross-bin feedback bleed for spectral smearing
- Per-bin envelope follower for dynamic gating
- Output soft-clipping per bin

## Build

Requires CMake 3.22+ and a C++17 compiler. JUCE is fetched automatically.

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

Installs AU to `~/Library/Audio/Plug-Ins/Components/` and VST3 to `~/Library/Audio/Plug-Ins/VST3/`.

## Visuals

All UI elements are pre-rendered in Blender 5.0 -- the pedal body, 8 knobs (128-frame filmstrips), ghost animation (192-frame spritesheet), LED states, and spectral analyzer display. JUCE composites them at runtime.

## License

MIT
