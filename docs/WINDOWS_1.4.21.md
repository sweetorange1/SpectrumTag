# Windows 1.4.21 — 2026-11-07

Status: delivered for user verification; not designated Stable. macOS/AU packaging remains paused. No Plan B was created or refreshed.

## Product changes

- New Noise Fill uses a target magnitude spectrogram followed by three phase-reconstruction refinements. Bright pixels have fewer random holes. Synthesis is bounded to four-second chunks, cancellable, and gated to covered samples inside the image time range. The frequency boundary tapers inward. Existing Print edits retain the legacy algorithm until edited; new and edited Prints use version 1. Deletion restores the original signal, including removal of synthesized fill.
- A derived render cache stores completed audio, raw/wet spectrum tiles and waveform peaks. The key includes source PCM, coverage, sample coordinates, FFT settings, edit masks, algorithm version and audio parameters. Stable content keys survive ARA object recreation. Checksums and atomic replacement reject incomplete/corrupt files; original audio plus edits remain authoritative. Disk restores happen at document/import/ARA recovery boundaries, not during ordinary parameter editing or Delete. Cache writes happen on restoration and explicit flush/save, avoiding a full new disk snapshot for every gesture.
- SPT5 retains float masks losslessly. SPT1–4 and older ARA archives remain readable with their original fill algorithm.
- First-load analysis pre-mixes each tile once and uses at most four analysis threads for long sources. ARA/native file reads use larger blocks. Unedited ARA compositions share the raw result rather than building a redundant wet copy. Native PCM/FLAC metadata does not require launching FFmpeg.
- Standalone normal shutdown saves a separate last-session document: cached source, Print edits, rendered result, source format and processor/UI state. The next launch restores it without decoding the original file again. Clear remains empty after restart. Failed restoration does not overwrite the prior session. This global standalone behavior is not shared with VST3/ARA project state.

## Verification

All tests below passed on the delivered Windows binaries:

- Full realtime/shared regression: recording alignment and gaps, freeze lifecycle, legacy state migration, six frequency scales, UI gestures and previews, deletion without FFT recomputation, cancellation/latest-edit behavior, Noise Fill and project recall.
- ARA model-host suite: 44 sources / 22 renderers, rates/offsets/overlaps, source replacement, read failure and retry, filtered archive restoration, versioned Print disk-cache hit, host seek, selection, source/view incremental page and spectrum reuse.
- Standalone suite: source-format audio/video import/export, preservation of video packets and timing/additional audio tracks, source/Print/bit-exact audio session recall, no DSP on reopen and Clear persistence.
- Both VST3s: pluginval 1.0.4, strictness 5, seed 20261107, GUI tests skipped.
- Installed VST3 bundle files and installed EXE match the staged binaries by SHA-256; PE file versions are 1.4.21.

Measured fixtures (not a guarantee for all source media or DAWs):

| Measurement | Result |
| --- | --- |
| Bright region dB standard deviation, legacy vs target synthesis | 5.61 dB → 3.08 dB |
| 2.4-second image on silent audio, target render including flush | 694 ms |
| Saved 3-second session restore | 80 ms; 0 Print passes, 0 FFT analysis passes |
| 30-second stereo first-load spectrum analysis | 99 ms vs 420 ms scalar reference |
| Optimized vs scalar spectrum | All tested quantized bins equal |
| Corrupt derived-cache recovery | Rebuilds deterministically from original audio and edits |

The ARA suite is a programmatic host harness. Actual Cubase project reopen and subjective print quality remain for the user's listening/viewing verification. Legacy projects without a derived cache must compute it once. Legacy Prints intentionally do not change sound automatically; edit Noise Fill or Print again to use the new synthesis.

## Local persistence

Windows uses the existing SpectrumTag data folder on D when available. `Recordings/` contains authoritative captured audio and Print state; `RenderCache/` contains disposable, content-validated derived data; `Standalone/last-session.xml` is the standalone-only pointer and UI/format state. macOS continues using the platform application-data location. The standalone EXE retains its embedded codec and requires no adjacent runtime folder. Removing the original video prevents video remux export; the cached audio can still be inspected.

QQ DeBreath standalone's `save_session` / `restore_last_session` implementation informed the separation between global standalone recall and per-project plugin/ARA state.
