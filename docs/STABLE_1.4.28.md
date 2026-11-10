# SpectrumTag 1.4.28 — Stable / Plan B

User authorization, 2026-11-09: designate the current version Stable, execute Plan B first, then continue the requested personal-Fork sync, Windows/macOS installers and upstream merge request.

## Inherited results

Windows standalone 1.4.28 was already built, checked and delivered; its subsequent replacement at `D:\Program Files\iisaacbeats.cn\SpectrumTag\SpectrumTag.exe` was verified. See [the Windows record](WINDOWS_1.4.28.md). SHA-256: `D1A9D74C3A1D7A86F820C8AE45543A79938196BC56E8114B5A3109216012FA1F`. Plan B does not rebuild or revalidate this product. Stable designation does not change the numerical version or imply completed Mac validation.

The previous complete cross-platform Stable is 1.4.27, whose existing installation packages remain the rollback baseline. Upstream PR #3 was merged on 2026-11-08; the user authorizes a new incremental PR after this delivery.

## New snapshot

Source: `D:\Codex\Workspaces\SpectrumTag-1.4.20-CrossPlatform`.

Destination: `D:\备份文件\Vibe Coding\SpectrumTag\源代码\SpectrumTag-1.4.28-Stable`.

The complete source, assets, documents/history, tests, build/dependency configuration, workflows and current Git history are copied. Required Windows codec inputs (`ffmpeg.exe`, `COPYING.GPLv3`, `FFmpeg-NOTICE.txt`) are included under `RequiredBuildInputs/WindowsCodec`. Regenerable dependency/build caches, user media, test artifacts and installers are excluded.

Completion evidence is written outside the snapshot: `D:\Codex\Temp\SpectrumTag-1.4.28\PLAN_B_COMPLETION.md`. It records the source commit, actual file/byte counts and key hash comparisons. At completion the backup freezes. Later work must not read, enumerate, hash, refresh, modify or remove it. Old snapshots are not accessed. Subsequent guide/packaging/release-document changes use the active workspace and do not refresh this snapshot.

Authority: `D:\备份文件\Vibe Coding\Plan-ABCDEFG-开发与发布执行规范-完整版.md`, sections 0.3, 0.6, 0.7, 1.3 and 6; the former ABCDEF filename is absent. The user's latest instructions take precedence over older memory summaries.

## Reproduction and remaining work

Use a new writable workspace to reproduce; never build in the frozen snapshot. CMake pins JUCE 8.0.15 and ARA SDK 2.3 commit `0005c532bb03eb210e7bb490329fc6d7ba1792c7`. Windows build scripts accept external build/JUCE/runtime paths. Preserve all three required codec inputs. The existing Mac workflows build arm64/x86_64 natively, bundle matching media dependencies, validate AU/VST3 and merge/install a Universal PKG.

At designation, 1.4.28 Mac products, version-matched Windows plug-ins and both installation packages are still to be generated under the user's separate delivery request. All UI/audio feature changes this version are in standalone spectrum-video export; the shared audio Print implementation is unchanged. Raw user diagnostics/media are not uploaded. Existing AGPL-3.0 and third-party notices are retained. Actual DAW field acceptance remains separate from automated checks.

## Subsequent delivery completion

The separately requested cross-platform packages, native and actual-install checks, public Release and Desktop handoff subsequently completed on 2026-11-09; see [the installation record](INSTALLERS_1.4.28.md). This addendum does not modify the original designation or access/refresh the frozen backup.
