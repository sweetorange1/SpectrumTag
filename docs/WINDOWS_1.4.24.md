# SpectrumTag 1.4.24 — standalone video and stopped deletion

Status update, 2026-11-07: subsequently designated **Stable** by the user with an explicit new Plan B request; see `STABLE_1.4.24.md`. The following describes the original development/delivery round and its verification boundaries.

This round delivers the Windows standalone only. The previously installed VST3/ARA remain 1.4.22. No Stable designation, Plan B, GitHub upload, or macOS/AU build is implied.

## Video output

- Fill the complete frame with the processed spectrum. Remove the title, Tag number, scale name, frequency/time rulers, elapsed/duration text, playhead line, and surrounding layout.
- New spectrum enters at the extreme right edge and scrolls left. Do not reserve a future-time strip. Retain smooth camera matching, Tag aspect targets, earlier-overlap priority and the unchanged processed soundtrack.
- Default all standalone export save dialogs to the user's actual Desktop special folder; the chooser still allows any destination. Load dialogs retain their existing defaults.
- Show a separate modal progress window with stage, percentage, destination, and Cancel Export. Closing the title bar requests cancellation, waits for the worker to stop, and then allows closing. Completion reaches 100%. A cancelled/failed export does not replace an existing target.

## Speed without reducing output settings

Cache frequency-row mapping and colourised immutable FFT columns while the vertical camera is unchanged. Invalidate on every frequency-camera transition; keep a bounded working set of current/previous visible columns, not the entire song. Horizontal motion still samples the original time positions; it does not shift a low-resolution screenshot.

Send the software RGB image directly to the encoder using JUCE's actual byte order (Windows BGR, Apple RGB), eliminating the former full-frame channel conversion/copy. Retain row-stride handling. CPU encoding uses half the logical CPUs, bounded to 2–8, rather than always four; H.264 fast/CRF 17 and VP9 quality settings, selected resolution and fps remain unchanged.

The test machine has an i9-10900 and RTX 3070. The bundled encoder's three-frame NVENC probe failed with `Cannot get the preset configuration: unsupported param (12)`. Hardware encoding is **not enabled**. A future codec upgrade needs its own driver, quality, licence and fallback verification; no network download or runtime replacement occurred here.

An independent scalar renderer verifies byte-identical frames through scale transitions and reverse seeks. The 1080p/90-frame fixture measured 1032.3 ms scalar versus 370.0 ms cached (2.79x). This is a frame-generation microbenchmark, not an end-to-end export speed guarantee. A separate short CPU encoding probe measured 2.18 s with four threads versus 1.92 s with eight. Export speed varies with content, format and camera transitions.

## Stopped deletion

The initial settled-deletion check passed, but immediate paint before another timer/playback tick still exposed stale coloured spectrum. Delete now invalidates outstanding raster requests immediately. Paint excludes obsolete time slices until a matching audio/raster snapshot arrives; the exclusion covers the entire frequency height because FFT leakage extends outside the image's band. Undo/redo deletions are observed too. The display must not enqueue a raster from a snapshot still containing deleted edit IDs.

`RecordingSnapshot::frameAt` rejects non-finite times and reads outside the current extent, preventing retained boundary-tile data from appearing beyond a shortened source-free timeline. Original audio and the existing no-FFT restore path remain unchanged.

## Verified on Windows

- Release standalone build completed (`build-pure-fast.log`).
- Actual offscreen UI: source-free Print, undo/redo, Clear/undo, immediate Delete before playback/timer and settled Delete. Deleted raster has zero residual coloured test pixels, no preview, and extent returns to one silent anchor sample.
- Progress content: percentage/stage, one-shot Cancel/title-close requests, completed 100%, cancelled-close; Desktop export default. Progress screenshot inspected. Native user interaction is still for user verification.
- Full-frame video, blank first frame without labels, right-edge content, exact scalar/cache pixel comparison, continuous axes, Tag aspect and overlap priority.
- MP4/MOV/MKV/WebM; 720p30, 1080p60, 1440p30 and 2160p60 probes. A 3.2-second MP4 decodes to 96 frames; soundtrack correlation remains above 0.94. Decoded colour check verifies RGB/BGR channel correctness. Cancellation preserves an existing target.
- Existing standalone suite: source-format audio/video exports, original video packets/frames/timing, delayed audio alignment, multiple audio tracks, cached session restoration and shared UI.
- Tail/prefix suite: Print outside source, actual sound/spectrum, unchanged source, extended WAV, Delete/undo/cache recall, negative seek, black video prefix/tail and preserved audio-picture alignment.

Evidence is in `D:\Codex\Temp\SpectrumTag-1.4.24`. Build and installation hash parity is recorded in that directory's delivery report. Existing frozen backups are untouched.
