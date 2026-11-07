# SpectrumTag 1.4.27 — Mac standalone field report

Later update on 2026-11-08: the user explicitly authorized a new upstream pull request after this confirmation. The earlier PR hold below is superseded by that instruction; the technical evidence still covers this standalone case only. See [the current merge handoff](UPSTREAM_HANDOFF_1.4.27.md).

Reviewed on 2026-11-08. User-supplied report: `SpectrumTag-1.4.27-Diagnostics-20261108-041103.json`. The raw report remains outside the repository; this document contains only a technical summary. No source music, image or project was supplied or requested for this review.

## User confirmation

Asked what was visible with the original image hidden when the report was exported, the user answered: **“已经能看到打印图形”** (the printed image is already visible).

This confirms normal Print visibility for the reported **Mac standalone 1.4.27** scenario. It is not evidence of a currently failing display and does not identify the cause of the earlier failure. VST3, AU and ARA field acceptance remains separate.

## Report evidence

| Item | Reported result |
| --- | --- |
| Product / mode | 1.4.27, standalone true, ARA build/binding false |
| Process | arm64, Rosetta false; OS string reported as Mac OSX 27.2 |
| Audio | 48 kHz, stereo, 512-sample block; loaded range 0–10 seconds |
| Print | One edit, 5.700–8.833 seconds, approximately 671–9328 Hz |
| Parameters | Mel (stored mode 4), 20 kHz range, Speed about 1x, Noise Fill 45%, ratio 0%, B/W 0.5 |
| Render | Busy false, progress 1, error code 0, one desired and one published Print; matching edit |
| Audio probe | Change RMS 0.003364908769066, confirming sampled output differs from original |
| Frequency data | Rendered mean 96.9370 vs original 3.6296; mean absolute change 95.5027 (internal byte-level units, not dB) |
| Snapshot | Engine, view and waterfall revision all 3; source matches true |
| Raster | Requested, applied, result and last-painted generation all 9; geometry and style match true |
| Display | Original image hidden, no active preview, valid 883 × 468 RGB waterfall; Print intersects viewport |
| Raster probe | 1024 pixels sampled: brightness mean 0.3751, peak 0.8706, variance 0.04148 |

The recent-state history shows the final revision and raster being adopted and painted after the new Print, with no pending render at capture. These observations agree with the user's visible result. Raster brightness statistics alone would not prove that an arbitrary image is recognisable; the user's confirmation supplies that missing visual evidence.

The source image bounds are local to the content clip: x=342 plus the content's x=70 gives x=412, equal to the printed footprint's x=412. Both widths are 188 pixels; heights are 275 and about 274.935 pixels. The reported source aspect is about 0.68421. The tiny differences are consistent with pixel rounding, so this snapshot does not exhibit the previously reported selection-size mismatch.

## Scope and next step

- Record this specific standalone result as working; do not label the raw report as a failure or attribute it to phase cancellation, a GPU bug or a stale cache.
- The earlier Mac failure cannot be assigned a unique root cause from a successful report. In particular, there is no evidence that the user's music had opposite polarity.
- VST3, AU and ARA still require their own user confirmation. If one still fails, collect its report while the failure is visible before playback, scale changes or reopening; the standalone report must not stand in for those formats.
- No product change, version increment, rebuild, installation or new upstream PR is warranted by this successful report alone.
- Current Stable remains 1.4.27. Its completed Plan B is frozen and was not accessed, compared or updated. This review only updates the active development record, with no remote publication.
