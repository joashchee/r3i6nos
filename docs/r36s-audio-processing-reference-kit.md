# Audio Processing: Pitch Shifting, Time Stretching & Stem Separation

This document compiles open-source code, libraries, and reference material for implementing audio pitch-shifting, time-stretching, and stem-separation features.

## Recommended starting stack

| Task | Best first choice | Alternatives / references |
|---|---|---|
| Time-stretch without changing pitch | Rubber Band Library | SoundTouch, librosa, TarsosDSP |
| Pitch-shift without changing duration | Rubber Band Library | SoundTouch, librosa, smbPitchShift |
| Fast Python prototyping | librosa | scipy, torchaudio |
| Stem separation | Demucs | Spleeter, Open-Unmix, Asteroid |
| Educational DSP implementation | Phase vocoder + HPSS | marcfores/DSP-Audio-Stem-Separation |

A robust production-oriented default is **Demucs for stems + Rubber Band for pitch/tempo + librosa for analysis and prototyping**.

## Pitch shifting

### Libraries

- **Rubber Band Library** — High-quality open-source C++/C library for pitch shifting and time stretching. Best choice for vocals, full mixes, DAW-like editing, and real-time playback.
  - Repository: https://github.com/breakfastquay/rubberband
- **SoundTouch** — Open-source WSOLA-based tempo, pitch, and playback-rate processing. A lightweight alternative for real-time or embedded use.
  - Repository: https://gitlab.com/soundtouch/soundtouch
- **librosa** — Python library suitable for fast prototypes and offline processing.
  - Documentation: https://librosa.org/doc/latest/
- **TarsosDSP** — Pure-Java audio-processing library with pitch detection, pitch shifting, and WSOLA time stretching.
  - Site: https://0110.be/releases/TarsosDSP/TarsosDSP-1.7/TarsosDSP-1.7-Readme.html

### Python example

```python
import librosa

y, sr = librosa.load("input.wav", sr=None, mono=False)

# Shift up by 3 semitones while preserving duration
y_pitch = librosa.effects.pitch_shift(y, sr=sr, n_steps=3)
```

`librosa.effects.pitch_shift` treats `n_steps` as semitones when `bins_per_octave=12`; it supports multichannel NumPy arrays.

### Implementation references

- **smbPitchShift** — Classic C implementation of frequency-domain pitch shifting; useful as a phase-vocoder learning reference.
- **Phase vocoder** — Analyze audio with an STFT, modify frequency-bin phase/magnitude data, then reconstruct using overlap-add.
- **TD-PSOLA** — Time-domain pitch-synchronous overlap-add approach, particularly useful for monophonic voice and pitched instruments.

## Time stretching

### Libraries

- **Rubber Band Library** — Strong general-purpose option for high-quality music time stretching.
- **SoundTouch** — WSOLA-based library for tempo, pitch, and playback-rate changes.
- **librosa** — Convenient Python baseline for offline time stretching.
- **pytimestretch** — Python interfaces for Rubber Band and Signalsmith Stretch, including time warping, scrubbing, and extreme spectral stretching.
  - Repository: https://github.com/openmirlab/pytimestretch
- **audiojs/stretch** — Modular JavaScript collection of time-stretch algorithms, including WSOLA, PSOLA, phase-locked vocoders, transient-aware vocoders, HPSS hybrids, and PaulStretch.
  - Repository: https://github.com/audiojs/stretch

### Python example

```python
import librosa

y, sr = librosa.load("input.wav", sr=None, mono=False)

# 1.5x faster, same pitch
y_fast = librosa.effects.time_stretch(y, rate=1.5)

# 0.75x slower, same pitch
y_slow = librosa.effects.time_stretch(y, rate=0.75)
```

A `rate > 1` speeds audio up; `rate < 1` slows it down while preserving pitch.

### Algorithm selection

| Algorithm | Best for | Tradeoff |
|---|---|---|
| WSOLA | Speech, solo instruments, lightweight real-time use | Can smear complex polyphonic music |
| PSOLA / TD-PSOLA | Monophonic voice and pitched instruments | Requires reliable pitch detection |
| Phase vocoder | General music and educational implementations | Can cause phasing artifacts without phase locking |
| Phase-locked / transient-aware vocoder | Drums, percussion, full mixes | More complex to implement |
| PaulStretch | Extreme slowdowns and ambient textures | Not suitable for normal musical timing |

## Stem separation

### Demucs

**Demucs** is a strong starting point for neural music source separation. It separates a mixture into drums, bass, vocals, and other stems using a hybrid spectrogram/waveform model.

- Repository: https://github.com/facebookresearch/demucs
- License: check the repository before commercial use
- Best fit: Python/PyTorch services, offline batch processing, and high-quality vocal/instrument isolation
- Caveat: the original Meta repository was archived on January 1, 2025 and is no longer maintained, but it remains an important reference implementation.

```bash
pip install demucs
demucs --two-stems vocals input.wav
```

### Spleeter

**Spleeter** is Deezer’s Python/TensorFlow source-separation library with pretrained models. It supports 2-stem separation (vocals/accompaniment) and 4-stem separation (vocals/drums/bass/other).

- Repository: https://github.com/deezer/spleeter
- License: MIT
- Best fit: straightforward Python deployments and pretrained-model experiments

### Research-grade options

- **Open-Unmix** — Minimal PyTorch reference implementation for music source separation, using bidirectional LSTMs on magnitude spectrograms with optional multichannel Wiener filtering.
- **Asteroid** — General-purpose PyTorch source-separation toolkit with composable filterbanks, encoders, maskers, decoders, losses, and training/evaluation recipes.
- **DSP-only separation** — Harmonic-percussive source separation (HPSS) and non-negative matrix factorization (NMF) can produce coarse stems without neural models.

### Educational DSP repository

`marcfores/DSP-Audio-Stem-Separation` is a compact Python/librosa reference combining HPSS, NMF, phase-vocoder time stretching, and pitch shifting.

- Repository: https://github.com/marcfores/DSP-Audio-Stem-Separation

## Suggested pipeline

1. **Ingest and normalize** — Decode audio to PCM, establish a consistent sample rate, and preserve channel layout and metadata.
2. **Separate stems** — Run Demucs once; cache outputs by content hash, model version, and separation settings.
3. **Analyze stems** — Estimate tempo, key, onset strength, and loudness before editing.
4. **Edit** — Apply time-stretch and pitch-shift operations to selected stems, preferably with Rubber Band.
5. **Mix and render** — Sum stems with gain/pan automation, then apply limiting and loudness normalization.
6. **Evaluate** — Compare separated stems against the original mix; listen for vocal bleed, drum transients, phase artifacts, and pitch artifacts.

## Practical recommendations

- Use **Rubber Band** when output quality is the priority.
- Use **librosa** for rapid Python experimentation, analysis, and internal tooling.
- Use **Demucs** for modern neural stem separation.
- Use **Spleeter** when you need a simpler pretrained-model workflow.
- Use **SoundTouch** or **TarsosDSP** for lightweight real-time or Java-based implementations.
- Avoid applying aggressive pitch shifting or extreme stretching directly to a full mix when stems are available; process individual stems instead.
- Preserve original audio and generate processed versions as separate assets.
- Test with vocals, drums, bass, dense mixes, and extreme tempo/pitch changes before shipping.
