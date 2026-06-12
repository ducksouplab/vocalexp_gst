# vocalexp_gst

Real-time vocal expressivity transformation as a GStreamer audio filter.

The `vocalexp` element tracks the fundamental frequency of a voice, rescales
the variability of its pitch contour by a continuous `expressivity` factor,
and re-synthesises the voice through a pitch-shifting phase vocoder with
optional spectral-envelope (formant) preservation — flattening a voice toward
monotone (`expressivity < 1`) or exaggerating its intonation
(`expressivity > 1`) with ~16 ms of algorithmic latency at 48 kHz.

## Status

| Phase | Content | State |
| --- | --- | --- |
| 1 | Core DSP (phase vocoder, YIN pitch tracker, envelope extractor, expressivity mapper) + unit tests | ✅ done |
| 2 | GStreamer `GstAudioFilter` element (`expressivity`, `envelope-preservation` properties) + harness tests | ✅ done |
| 3 | Live pipelines (`autoaudiosrc` → plugin → `autoaudiosink`) + interactive demo tool | ✅ done |

## Quick start (Docker)

The project includes a `Dockerfile` that packages all dependencies (GStreamer, ONNX Runtime, RubberBand) and builds the plugin.

```sh
docker build -t vocalexp-gst .

# Process a file:
docker run --rm -v $(pwd):/workspace vocalexp-gst \
  gst-launch-1.0 filesrc location=media/in/F21a1.wav ! wavparse ! \
  audioconvert ! audioresample ! \
  'audio/x-raw,format=F32LE,channels=1,rate=48000' ! \
  vocalexp expressivity=2.0 ! \
  audioconvert ! wavenc ! filesink location=media/out/F21a1_expressive.wav
```

## Quick start (Local)

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure   # 51 tests

# Run the element without installing:
export GST_PLUGIN_PATH="$PWD/build"
gst-inspect-1.0 vocalexp
```

Prefer notebooks? [`notebooks/vocalexp_tutorial.ipynb`](notebooks/vocalexp_tutorial.ipynb)
builds the plugin, processes a WAV from `media/in/`,
lets you A/B the audio, and plots pitch contours and spectrograms for any
parameter combination (needs `numpy` + `matplotlib`).

Requires CMake ≥ 3.16, a C++17 compiler, GStreamer ≥ 1.20 development files
(`gstreamer-1.0`, `gstreamer-base-1.0`, `gstreamer-audio-1.0`; on
Debian/Ubuntu: `libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev`) and
GoogleTest for the test suite. Without GStreamer dev files, the build falls
back to the DSP library and its tests only.

## The element

```
$ gst-inspect-1.0 vocalexp
  Factory Details: Vocal expressivity transformer (Filter/Effect/Audio)
  Pad caps: audio/x-raw, format=F32LE, rate=[8000,192000], channels=1
```

| Property | Type | Default | Runtime-changeable | Description |
| --- | --- | --- | --- | --- |
| `expressivity` | float [0, 4] | 1.0 | yes (controllable) | Pitch contour scaling: 0 = monotone, 1 = unchanged, >1 = exaggerated |
| `envelope-preservation` | bool | true | yes (controllable) | Keep formants (vocal identity) in place while shifting pitch |
| `frame-size` | uint [128, 8192] | 1024 | at next negotiation | STFT window in samples (power of two) |
| `overlap-factor` | uint [2, 16] | 4 | at next negotiation | Overlapping windows; hop = frame-size / overlap-factor |
| `min-frequency` | float [20, 2000] | 60 | at next negotiation | Pitch search lower bound (Hz) |
| `max-frequency` | float [50, 4000] | 1000 | at next negotiation | Pitch search upper bound (Hz) |

The element processes mono float32 in-place, resets its state on `DISCONT`
buffers, and answers latency queries with the STFT latency
(`frame-size − hop`, 16 ms at the defaults @ 48 kHz) so live pipelines
compensate automatically.

## Live pipelines (Phase 3)

Microphone → speakers, neutral (verify transparent pass-through first —
**use headphones**, the loop feeds back through open speakers):

```sh
GST_PLUGIN_PATH=$PWD/build gst-launch-1.0 \
  autoaudiosrc ! queue max-size-time=20000000 leaky=downstream ! \
  audioconvert ! audioresample ! \
  'audio/x-raw,format=F32LE,channels=1,rate=48000' ! \
  vocalexp expressivity=1.0 ! \
  audioconvert ! autoaudiosink
```

Hyper-expressive voice (intonation exaggerated 2×, formants preserved):

```sh
GST_PLUGIN_PATH=$PWD/build gst-launch-1.0 \
  autoaudiosrc ! queue max-size-time=20000000 leaky=downstream ! \
  audioconvert ! audioresample ! \
  'audio/x-raw,format=F32LE,channels=1,rate=48000' ! \
  vocalexp expressivity=2.0 envelope-preservation=true ! \
  audioconvert ! autoaudiosink
```

Monotone voice, lower latency (frame 512 → 8 ms STFT latency, pitch floor
raised to keep YIN reliable in the shorter window):

```sh
GST_PLUGIN_PATH=$PWD/build gst-launch-1.0 \
  autoaudiosrc buffer-time=20000 ! queue leaky=downstream ! \
  audioconvert ! audioresample ! \
  'audio/x-raw,format=F32LE,channels=1,rate=48000' ! \
  vocalexp expressivity=0.0 frame-size=512 min-frequency=120 ! \
  audioconvert ! autoaudiosink buffer-time=20000
```

Offline file processing:

```sh
GST_PLUGIN_PATH=$PWD/build gst-launch-1.0 \
  uridecodebin uri=file:///path/to/voice.wav ! audioconvert ! audioresample ! \
  'audio/x-raw,format=F32LE,channels=1,rate=48000' ! \
  vocalexp expressivity=1.8 ! audioconvert ! \
  wavenc ! filesink location=voice_expressive.wav
```

### Changing properties dynamically

`gst-launch-1.0` cannot modify a running pipeline, so the repo ships an
interactive demo (`build/vocalexp-demo`) that builds the live pipeline and
binds keys:

```sh
GST_PLUGIN_PATH=$PWD/build ./build/vocalexp-demo
#   +/-, arrows : expressivity ±0.1
#   0 1 2 3 4   : set expressivity directly
#   e           : toggle envelope-preservation
#   q           : quit
```

Both `expressivity` and `envelope-preservation` are flagged
`GST_PARAM_CONTROLLABLE`, so they can also be automated sample-accurately
with `GstController` from application code.

## Architecture

All DSP lives in `src/dsp/`, has **no GStreamer or external dependency**
(including the FFT, which is implemented in-repo), and performs **no
allocation on the audio path** after construction. The GStreamer element
(`src/gst/gstvocalexp.cpp`) is a thin `GstAudioFilter` wrapper around
`VocalExpressivityProcessor`.

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
| `GstVocalexp` | `src/gst/gstvocalexp.cpp` | `GstAudioFilter` element: properties, in-place transform, latency query, discontinuity reset |

### The expressivity transform

Per voiced frame, with the pitch derivative `f0'(t) = f0(t) − f0(t−1)`:

```
f0_mod(t) = f0_mod(t−1) + E · f0'(t),     f0_mod(onset) = f0(onset)
ratio(t)  = f0_mod(t) / f0(t)             → phase-vocoder pitch-shift factor
```

so `E = 1` is identity (ratio ≡ 1), `E = 0` produces a monotone voice pinned
at the onset pitch, and `E = 2` exactly doubles every deviation of the
contour from its onset value (the contour's standard deviation scales by
`E`).

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

## Tests

`ctest --test-dir build` runs two suites:

- **`dsp_tests`** (43 cases) — FFT correctness (impulse, bin-centred sine,
  round-trip, Parseval) and Hann COLA; phase unwrapping recovering an off-bin
  sine frequency to < 0.5 Hz; pitch shifting up/down landing on the target
  frequency; formant retention under envelope preservation
  (spectral-centroid comparison); YIN accuracy on sines/sawtooth,
  octave-error robustness, noise and silence rejection, vibrato tracking;
  the exact expressivity recursion including clamping, unvoiced resets and
  `E`-scaling; end-to-end vibrato widening/flattening through the full
  chain, including in-place processing.
- **`gst_element_tests`** (7 cases, `GstHarness`) — element registration and
  property defaults/round-trips; sample-for-sample buffer flow; end-to-end
  vibrato flattening through the element; mid-stream property changes;
  latency query reporting 16 ms; `DISCONT` handling; clean error on invalid
  `frame-size`.
