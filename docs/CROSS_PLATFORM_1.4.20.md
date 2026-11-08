# SpectrumTag 1.4.20 cross-platform delivery

This is the continuation requested after the Windows 1.4.18/1.4.19 Stable
handoff. All new product targets use 1.4.20. This does not promote 1.4.20 to
Stable or modify the frozen Plan B source backup.

## Components and build inputs

| Platform | Components | Minimum system |
| --- | --- | --- |
| Windows x64 | Realtime VST3, ARA VST3, standalone EXE | Windows 10 |
| macOS Universal | Realtime VST3, ARA VST3, realtime AU, ARA AU, standalone App | macOS 15 |

JUCE is pinned to 8.0.15. The ARA target has its own plug-in and document
identities. Network update/telemetry functionality is disabled in these builds.
Windows uses MSVC 2022 Release; Mac uses separate Apple Silicon and Intel
Release builds followed by `lipo` and ad-hoc signing. The installer baseline is
macOS 15 because its bundled codec dependencies use that system baseline.

The Windows EXE includes the compressed codec and notices. The Mac App includes
both architecture-specific codec dependency trees under `Contents/Helpers`;
the running application slice chooses the matching helper, including Rosetta.
Users do not need Homebrew or a separate FFmpeg installation.

## Fixes found during this port

- The Mac native image implementation returned ARGB storage for an RGB request.
  Frequency rasters and Print previews wrote packed `PixelRGB` values. Explicit
  software RGB images now preserve the expected stride. The mapping regression
  checks all columns, RGB channels and opacity rather than relaxing tolerances.
- CoreFoundation must precede the JUCE headers in the ARA test entry point to
  avoid the global `Point` type collision.
- Preview retirement is checked on subsequent UI ticks after a matching raster
  arrives. A worker can publish the final snapshot before clearing its busy flag;
  checking only on raster adoption could otherwise leave the simulation visible.
- Standalone display checks wait for the loaded recording's raster revision,
  not the initial empty raster. Fill gesture tests settle the preceding
  selection/display-FFT job before testing that dragging does not render audio.
- Mac file dialogs use the user's Music folder. Windows machines without D:
  use a per-user application data fallback; Mac never uses a Windows drive path
  or writes into an installed application bundle.
- Codec relocation uses a recent `install_name_tool`, edits each Mach-O image
  in one invocation, and replaces signatures after relocation. Source formulae,
  versions, notices and helper manifests are included with the Mac App.

## Windows verification

The realtime processor regression suite, ARA model/host harness and both VST3
`pluginval` strictness-5 validations passed. The standalone suite passed audio
and video import/drop, Print and export, delayed audio alignment, original audio
codec/rate/channel checks, unchanged video packets/frames/timestamps, preservation
of the extra audio track in its fixture, and rejection of video without audio.
The final loaded-view snapshot contains the expected spectrum and version text.

The Inno Setup installer includes selectable components and the signed Microsoft
x64 runtime. Every staged VST3 file and the standalone EXE were hash-compared
with the tested build. It has not been installed over the user's running system
as part of this packaging task.

## macOS verification

Native architecture and installed-PKG results are recorded here when the
cross-platform workflow completes. A successful compile alone does not establish
AU registration, host support or complete installer validation.

## Reproduction and packaging

- `.github/workflows/cross-platform.yml`: native builds, processor/ARA/media
  regressions, AU/VST3 validators, Universal assembly and actual installation
  on a disposable runner.
- `packaging/macos/bundle_codec.py`: relocate and audit the complete codec
  dependency closure and retain third-party notices.
- `packaging/macos/validate_au.py`: verify generated AU identities and signatures,
  refresh the disposable runner's component registry, then run `auval`.
  Native AU bundles are explicitly signed before discovery as well as after
  Universal assembly; the compiler's incidental Mach-O signature is insufficient.
- `packaging/macos/package.py`: merge native product slices, check bundle
  versions, sign and verify bundles, and generate the five-component PKG.
- `.github/workflows/package-macos.yml`: reuse successful native archives for
  packaging-only changes after proving product inputs are identical.
- `packaging/windows/build-installer.ps1` and `installer.iss`: stage verified
  x64 files and compile the three-component Windows installer.

## Handoff limits

Windows installers are unsigned. Mac bundles are ad-hoc signed; the PKG has no
Developer ID Installer certificate or Apple notarization. Refer to the bilingual
installation guides for narrowly scoped troubleshooting.

Automated validators and the ARA harness do not replace real Cubase/Logic
projects, Retina/multiple-display checks, DAW save/reopen and host-specific ARA
entry testing. Logic's Apple Silicon ARA/Rosetta requirements are explained in
the guides. No upstream PR has been automatically merged by this delivery.
