# vocalexp_gst

Real-time vocal expressivity transformation as a GStreamer audio filter.

The plugin tracks the fundamental frequency of a voice, rescales the
variability of its pitch contour by a continuous `expressivity` factor, and
re-synthesises the voice through a pitch-shifting phase vocoder with optional
spectral-envelope (formant) preservation — flattening a voice toward monotone
(`expressivity < 1`) or exaggerating its intonation (`expressivity > 1`) with
~16 ms of algorithmic latency at 48 kHz.

## Status

| Phase | Content | State |
| --- | --- | --- |
| 1 | Core DSP (phase vocoder, YIN pitch tracker, envelope extractor, expressivity mapper) + unit tests | ✅ done |
| 2 | GStreamer `GstAudioFilter` element (`expressivity`, `envelope-preservation` properties) | ⏳ pending |
| 3 | Live pipeline testing (`autoaudiosrc` → plugin → `autoaudiosink`) | ⏳ pending |

## Architecture

All DSP lives in `src/dsp/`, has **no GStreamer or external dependency**
(including the FFT, which is implemented in-repo), performs **no allocation on
the audio path** after construction, and is wrapped by the GStreamer element
in Phase 2.

```
                       ┌──────────────┐   f0(t)   ┌────────────────────┐  ratio(t)
            ┌─────────►│ PitchTracker ├──────────►│ ExpressivityMapper ├─────────┐
            │          │    (YIN)     │           └────────────────────┘         │
            │          └──────────────┘                                          ▼
 audio in ──┤                                                            ┌──────────────┐
            │                                                            │ PhaseVocoder │──► audio out
            └───────────────────────────────────────────────────────────►│  (+envelope) │
                                                                         └──────────────┘
```

| Component | File | Algorithm |
| --- | --- | --- |
| `Fft` | `src/dsp/fft.cpp` | Radix-2 iterative Cooley-Tukey, precomputed tables |
| `PhaseVocoder` | `src/dsp/phase_vocoder.cpp` | STFT → per-bin phase unwrapping → spectral bin remapping → phase accumulation → ISTFT, Hann analysis+synthesis windows, 75 % overlap (Laroche/Dolson, Bernsee) |
| `EnvelopeExtractor` | `src/dsp/envelope_extractor.cpp` | Cepstral smoothing + True Envelope iteration (Röbel & Rodet 2005), dynamic-range clamped |
| `PitchTracker` | `src/dsp/pitch_tracker.cpp` | YIN (de Cheveigné & Kawahara 2002): CMNDF, absolute threshold, parabolic lag interpolation, RMS silence gate |
| `ExpressivityMapper` | `src/dsp/expressivity_mapper.cpp` | Derivative-scaling contour transform (below) |
| `VocalExpressivityProcessor` | `src/dsp/vocal_expressivity_processor.cpp` | Full chain, hop-synchronous ratio updates |

### The expressivity transform

Per voiced frame, with the pitch derivative `f0'(t) = f0(t) − f0(t−1)`:

```
f0_mod(t) = f0_mod(t−1) + E · f0'(t),     f0_mod(onset) = f0(onset)
ratio(t)  = f0_mod(t) / f0(t)             → phase-vocoder pitch-shift factor
```

so `E = 1` is bit-honest identity intent (ratio ≡ 1), `E = 0` produces a
monotone voice pinned at the onset pitch, and `E = 2` exactly doubles every
deviation of the contour from its onset value (the contour's standard
deviation scales by `E`).

> **Design note** — the original specification wrote the recursion with the
> *original* previous pitch as the base: `f0_mod(t) = f0(t−1) + E·f0'(t)`.
> That one-frame-memory form does not flatten the voice at `E = 0` (it
> degenerates to a one-frame-delayed copy of the original contour). The
> integrated form above is the one that actually scales the standard
> deviation of the pitch contour as required, while remaining identical for
> `E = 1`. Verified by `ExpressivityMapper.DeviationFromOnsetScalesExactlyByE`.

Unvoiced/silent frames map to ratio 1 (audio passes through untouched) and
re-anchor the contour at the next voiced onset. The ratio is clamped to
[0.25, 4] with post-clamp re-anchoring so saturation cannot wind up.

### Envelope preservation

When enabled, each analysis frame's spectral envelope is extracted by
cepstrally-smoothed True Envelope estimation, the spectrum is whitened by it
before bin remapping, and the *original* envelope is re-applied afterwards —
harmonics move, formants stay, so vocal identity is preserved and the
"chipmunk" effect is avoided.

### Latency

`latency = frameSize − hopSize`. Defaults: `frameSize = 1024`,
`overlapFactor = 4` → hop 256, i.e. **768 samples = 16 ms @ 48 kHz**. Both
are configurable (`frameSize` must be a power of two). Measured throughput of
the full chain (envelope preservation on): ~9× real-time on one core at
`-O2`; the dominant cost is YIN's O(W·τmax) difference function, which can be
moved to an FFT-based formulation if more headroom is needed.

## Building and testing

Requires CMake ≥ 3.16, a C++17 compiler and GoogleTest (only for tests).

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The suite (43 tests) covers: FFT correctness (impulse, bin-centred sine,
round-trip, Parseval) and Hann COLA; phase unwrapping recovering an off-bin
sine frequency to < 0.5 Hz; pitch shifting up/down landing on the target
frequency; formant retention under envelope preservation (spectral-centroid
comparison); YIN accuracy on sines/sawtooth, octave-error robustness, noise
and silence rejection, vibrato tracking; the exact expressivity recursion
including clamping, unvoiced resets and `E`-scaling; and end-to-end vibrato
widening/flattening through the full chain, including in-place processing.
