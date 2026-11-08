# SpectrumTag 1.4.27 — Stable / Plan B

User authorisation on 2026-11-08: “先设为stable 执行plan B”.

## Status and inherited delivery

- The currently delivered 1.4.27 is user-designated Stable: Windows realtime VST3, ARA VST3 and standalone; macOS Universal realtime/ARA VST3, realtime/ARA AU and standalone, as included in the existing installers.
- No product input, numerical version or binary is changed by this designation. Plan A and the already completed native/installer checks are inherited from [INSTALLERS_1.4.27.md](INSTALLERS_1.4.27.md); they are not repeated for Plan B.
- Product source commit: `f7e4b1e17f2aeab73fe3a0f6b2c51d01246ec363`. Source workspace HEAD before this status update: `5ec715e051cae591e963195b1e71469f7392bf00`, branch `release-1.4.27-installers`. This snapshot also includes the local documentation-only Stable update.
- Active workspace: `D:\Codex\Workspaces\SpectrumTag-1.4.20-CrossPlatform`.
- Existing installers and bilingual TXT guides: `D:\Codex\Outputs\SpectrumTag-1.4.27-Installers`, with the previously verified real copy at `C:\Users\86189\Desktop\SpectrumTag-1.4.27-Installers`.
- Windows Setup recorded SHA-256: `C37A33DEF07CA274A48C91F3272D276944E7A1300F17C9D9DE012942B3D18BAF`.
- macOS PKG recorded SHA-256: `E390519CAD3AD2FA32ECDED18F02590381D0D4A61F21561C639433C98C00F233`.
- These are inherited delivery identities, not a new binary audit, build, installation or package upload. The installed products on the user's computer are not overwritten for this request.

## New formal source backup

Destination: `D:\备份文件\Vibe Coding\SpectrumTag\源代码\SpectrumTag-1.4.27-Stable`.

The snapshot contains current source, resources, documentation, tests, build scripts, dependency configuration, GitHub workflows and existing Git history, including the local Stable documentation update. Build/dependency caches, temporary test media, installed product copies and output installers are excluded. Required Windows codec inputs (`ffmpeg.exe`, `COPYING.GPLv3`, `FFmpeg-NOTICE.txt`) are included at `RequiredBuildInputs/WindowsCodec`, copied from `D:\Codex\Temp\SpectrumTag-EmbeddedRuntime` with their original notices.

Completion evidence (outside the backup): `D:\Codex\Temp\SpectrumTag-1.4.27\PLAN_B_COMPLETION.md`. It records actual file/byte totals and key-file comparison results after copying. This is the continuation reference for later Plans; do not return to the backup for revalidation.

The snapshot becomes frozen when the completion report is issued. No previous frozen backup is read, enumerated, hashed, refreshed, overwritten or removed. Later development and publishing must use the active workspace, recorded source commits and delivery records. Only another explicit user backup request can authorize another snapshot.

Authority: `D:\备份文件\Vibe Coding\Plan-ABCDEFG-开发与发布执行规范-完整版.md`, sections 0.3, 0.6, 0.7, 1.3 and 6. The former ABCDEF filename is absent; this is its current successor. The latest explicit user rules take precedence over older Plan summaries.

## Reproduction inputs

CMake pins JUCE 8.0.15 and ARA SDK commit `0005c532bb03eb210e7bb490329fc6d7ba1792c7` (SDK 2.3). Existing platform scripts, workflows, third-party notices and license are preserved unchanged. Windows uses MSVC 2022 x64 Release; macOS Universal delivery combines native arm64 and x86_64 builds. Original build and installer evidence is referenced in `INSTALLERS_1.4.27.md` rather than copied into the source snapshot.

For a future reproduction, copy the snapshot to a new writable workspace; never build inside the frozen backup. Restore the pinned dependencies or supply their external paths. `build-standalone-windows.ps1` accepts `-JuceDir`, a separate D-drive `-BuildDir`, and `-RuntimeDir <new-workspace>\RequiredBuildInputs\WindowsCodec`. Preserve all three codec files; substituting another codec is a changed build input. macOS runtime preparation and packaging remain specified in the platform workflows and scripts.

## Known issues, rollback and pending work

- The friend's Mac issue (Print appears in exported audio in RX but not the internal display) is still under investigation. The source is not assumed to have opposite polarity. Collect the manual diagnostic JSON before changing playback/view state. Synthetic normal-stereo tests are not proof that the actual host/project issue is fixed.
- Standalone installer destination selection changes are explicitly deferred by the user. This Plan B does not implement them.
- Upstream PR #2 remains closed. Do not create a new upstream PR until the user personally validates the Mac version and explicitly authorizes it. Stable designation alone is not that confirmation.
- Prior delivery: 1.4.26, retaining its original status. Prior explicitly designated Stable baselines: Windows standalone 1.4.24 and Windows plugins 1.4.18. Those are component-specific fallbacks, not a claim of prior validated Mac field behaviour. Use active records and existing output products for any requested rollback; do not inspect frozen backups as part of a later Plan.
- This request performs local status documentation and Plan B only. No automatic Plan C/D/E/F/G, remote push, Release rewrite, build, test rerun, installation or PR is performed.
