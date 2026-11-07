# Windows 1.4.22 — 2026-11-07

Status: verified for Windows delivery; not designated Stable. Existing frozen backups, macOS/AU builds and GitHub publication remain unchanged.

## Time view

The standalone implicitly fitted the complete file on first snapshot arrival. This multiplied the normal Speed clock by a hidden zoom. A 60-second fixture showed 60 seconds at 1x instead of 13.833333 seconds in the standard 830-pixel plot. Selecting Insight cleared that multiplier, explaining the reported workaround.

- First Load and replacement Load now use the current Speed. A pending invalid file view resets old Fit zoom even when audio and editor initialization finish in either order.
- Explicit Fit still shows the whole recording and has its own displayed effective multiplier and duration. It does not change the normal Speed parameter range or the common 60 logical pixels/second clock. Changing Speed exits Fit using that common clock.
- Valid saved views, including intentional Fit, are restored. ARA's existing first-load overview and realtime recording navigation remain separate from standalone file initialization.
- Zoom retains audio when browsing audio, while an intentionally panned empty standalone area remains available for Print placement.

## Standalone Print before/after a file

- Print may lie wholly or partly before original time zero or after the original end. Rendered audio, spectrum, overview and export cover the original file plus all Prints. Noise Fill supplies sound in empty areas; with zero Noise Fill, silence remains silence.
- Source pages remain immutable. Synthetic coverage exists only within Print intervals. Intervening gaps stay silent. Original zero remains the original song's zero in the editor.
- Deleting/moving/resizing Prints derives the extent again; deleting the last extension restores original length. The derived negative-time origin is removed from SPT5 source storage, which continues storing original audio plus edit instructions. Undo/Redo, session recall and content-keyed render caching retain the extension.
- Playback seek uses a NaN sentinel instead of -1 so negative times are valid. Ordinary realtime VST3 and ARA do not gain standalone coverage outside recorded/host audio.
- Existing capacity remains 15 minutes or 64 million samples for the complete edited extent, whichever is smaller.

## Video export

An audio-only export starts at the earliest retained audio/Print sample. Video export pads the picture with black frames before/after the original picture when needed. Leading padding is rounded up to a video frame; the fractional-frame remainder is added as leading silence to the processed audio. Original picture and sound therefore shift by the same amount. Additional audio/subtitle streams receive the same offset.

No-padding video exports retain the existing stream-copy path. Black-frame padding requires video encoding in the original codec/container: H.264, HEVC and VP9 use lossless settings; VP8, MPEG-4, MJPEG and ProRes use the supported high-quality settings. Unsupported padding codecs fail without replacing the source file. The original file is never overwritten. Do not describe every source codec as pixel-lossless.

The bundled Windows encoder predates `-fps_mode`; use supported `-vsync 0`. The padded H.264 test verifies every original decoded picture frame by MD5, constant black head/tail frames, audio-picture alignment and audio-only export.

## Missing session media

When restoration cannot find the saved audio cache, leave the standalone empty without an error/status prompt. A missing cached image leaves its source preview empty. The old session is not overwritten by a failed empty restore; a deliberate new Load permits normal session saving again. Existing audio caches still restore when only the original external audio file has disappeared. This is the explicitly stated working assumption while the user's original-file-versus-cache clarification remains unanswered.

## Verification

Passed: startup view ordering/recall and explicit Fit; silent missing-media recovery and new Load; head/tail Print sound/spectrum, immutable source, deletion without FFT for a tail, Undo/Redo, negative seek, cached restart; extended WAV/audio-only exports; MP4 black padding, original-frame MD5 and sync; the realtime/shared, ARA model-host and full standalone format suites; both VST3 pluginval strictness 5 runs. Detailed logs and installed hashes are recorded in the local delivery report. Offscreen tests do not substitute for the user's DAW/window review.

Build note: changing a shared struct during a multi-target MSBuild can leave earlier objects compiled against its previous layout even after an incremental pass. All affected project translation units were subsequently forced to rebuild together; the ARA 44-source sample mapping then passed. Test reads of timer-published viewport properties must wait for the requested value/load revision, rather than assuming a 10 ms message pump refreshes a 60 Hz UI.
