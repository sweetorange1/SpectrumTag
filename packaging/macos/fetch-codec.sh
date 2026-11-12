#!/usr/bin/env bash
set -euo pipefail

# SpectrumTag macOS codec fetcher
# ----------------------------------------------------------------------------
# CMakeLists.txt 在构建 SpectrumTagStandalone 时把
#   packaging/macos/runtime/<arch>/ffmpeg
# 连同依赖闭包、许可与构建信息一起放进
#   SpectrumTag.app/Contents/Helpers/<arch>/
# 本脚本负责准备这个输入，作用等同 Windows 的 fetch-codec.ps1：
#   · 下载官方 GPL 静态 FFmpeg（macOS amd64 / arm64，单文件、无可重定位依赖）
#   · 用发布方提供的 SHA-256 校验压缩包，再校验 Mach-O 架构
#   · 保存 COPYING.GPLv3 与 FFmpeg-NOTICE.txt，记录来源、版本、构建参数和哈希
#   · 确认导出所需的编码器齐全
#
# 用法：
#   ./packaging/macos/fetch-codec.sh                  # 下载当前机器架构
#   ./packaging/macos/fetch-codec.sh --arch arm64     # 指定架构
#   ./packaging/macos/fetch-codec.sh --arch all       # Universal app 需要两个架构
#   ./packaging/macos/fetch-codec.sh --force          # 忽略已有文件重新下载
#
# 已有的 Homebrew ffmpeg-full 也可以直接使用，无需本脚本：
#   cmake -B build -DSPECTRUMTAG_MACOS_CODEC="$(brew --prefix ffmpeg-full)/bin/ffmpeg"
# ----------------------------------------------------------------------------

BASE_URL="${SPECTRUMTAG_CODEC_BASE_URL:-https://ffmpeg.martin-riedl.de}"
LICENSE_URL="${SPECTRUMTAG_CODEC_LICENSE_URL:-https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/COPYING.GPLv3}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
RUNTIME_ROOT="${PROJECT_ROOT}/packaging/macos/runtime"

# macOS 自带导出所需：AAC/LAME/Vorbis/Opus/FLAC/PCM、H.264/H.265/VP9 与 ProRes
REQUIRED_ENCODERS=(libmp3lame libvorbis libopus libx264 libx265 libvpx-vp9 prores_ks flac pcm_s24le)

ARCH_ARGS=()
FORCE=0

log() { echo "[fetch-codec] $*"; }
die() { echo "[fetch-codec] ERROR: $*" >&2; exit 1; }

usage() {
  cat <<'EOF'
Usage: ./packaging/macos/fetch-codec.sh [--arch x86_64|arm64|all] [--force]

  --arch ARCH   Target architecture (default: this machine).
                Use "all" to prepare both slices of a Universal app.
  --force       Download again even if runtime/<arch>/ffmpeg already exists.
  -h, --help    Show this help.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --arch)
      [[ $# -ge 2 ]] || die "--arch requires a value"
      ARCH_ARGS+=("$2")
      shift 2
      ;;
    --force)
      FORCE=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "Unknown argument: $1"
      ;;
  esac
done

[[ "$(uname -s)" == "Darwin" ]] || die "This script must run on macOS."

for cmd in curl unzip shasum lipo file; do
  command -v "${cmd}" >/dev/null 2>&1 || die "Missing required command: ${cmd}"
done

host_arch() {
  if [[ "$(sysctl -n hw.optional.arm64 2>/dev/null || echo 0)" == "1" ]]; then
    echo "arm64"
  elif [[ "$(uname -m)" == "arm64" ]]; then
    echo "arm64"
  else
    echo "x86_64"
  fi
}

if [[ ${#ARCH_ARGS[@]} -eq 0 ]]; then
  ARCH_ARGS=("$(host_arch)")
fi

normalise_arch() {
  case "$1" in
    all|universal) echo "x86_64 arm64" ;;
    x86_64|intel|amd64) echo "x86_64" ;;
    arm64|aarch64) echo "arm64" ;;
    *) die "Unsupported architecture: $1" ;;
  esac
}

HOST_ARCH="$(host_arch)"
TARGETS=()
for requested in "${ARCH_ARGS[@]}"; do
  for arch in $(normalise_arch "${requested}"); do
    TARGETS+=("${arch}")
  done
done

latest_zip_path() {
  local remote="$1" index path
  index="$(curl -fsSL --retry 3 --max-time 120 "${BASE_URL}/")"
  path="$(printf '%s' "${index}" \
    | grep -oE "/download/macos/${remote}/[0-9]+_[^/\"]+/ffmpeg\.zip" \
    | head -n 1)"
  [[ -n "${path}" ]] || die "Could not locate a ${remote} ffmpeg.zip on ${BASE_URL}"
  echo "${path}"
}

write_notice() {
  local arch="$1" dest="$2" zip_url="$3" zip_sha="$4" bin_sha="$5" version="$6" configuration="$7"
  cat > "${dest}/FFmpeg-NOTICE.txt" <<EOF
FFmpeg — third-party codec bundled with SpectrumTag (macOS ${arch} helper)

Component : Contents/Helpers/${arch}/ffmpeg
Version   : ${version}
Build     : static macOS ${arch} build from ${BASE_URL}, linked only against macOS system libraries
Download  : ${zip_url}
SHA-256   : ${bin_sha} (helper), ${zip_sha} (downloaded archive)
License   : GNU General Public License version 3 (see COPYING.GPLv3)
Source    : https://github.com/FFmpeg/FFmpeg and the build configuration listed below
Buildopts : ${configuration}

FFmpeg is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version. FFmpeg is distributed WITHOUT ANY WARRANTY; without even the implied
warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

This binary is stored inside SpectrumTag.app and is launched as a separate
child process for media import and export. It is not linked into SpectrumTag.
The complete corresponding source code is available from the URLs above.
EOF
}

for arch in "${TARGETS[@]}"; do
  case "${arch}" in
    x86_64) remote="amd64" ;;
    arm64)  remote="arm64" ;;
    *) die "Unsupported architecture: ${arch}" ;;
  esac

  dest="${RUNTIME_ROOT}/${arch}"
  if [[ -x "${dest}/ffmpeg" && "${FORCE}" -eq 0 ]]; then
    log "Reusing ${dest}/ffmpeg (use --force to download again)"
    continue
  fi

  zip_path="$(latest_zip_path "${remote}")"
  zip_url="${BASE_URL}${zip_path}"
  work="$(mktemp -d "${TMPDIR:-/tmp}/spectrumtag-codec-XXXXXX")"

  log "[1/5] Downloading ${zip_url}"
  curl -fsSL --retry 3 --max-time 600 "${zip_url}" -o "${work}/ffmpeg.zip"
  curl -fsSL --retry 3 --max-time 120 "${zip_url}.sha256" -o "${work}/ffmpeg.zip.sha256"

  log "[2/5] Verifying archive checksum"
  ( cd "${work}" && shasum -a 256 -c ffmpeg.zip.sha256 >/dev/null ) \
    || die "SHA-256 mismatch for ${zip_url}"

  log "[3/5] Extracting"
  mkdir -p "${work}/unpack"
  unzip -o -q "${work}/ffmpeg.zip" -d "${work}/unpack"
  binary="${work}/unpack/ffmpeg"
  [[ -f "${binary}" ]] || die "ffmpeg was not found inside ${zip_url}"
  file "${binary}" | grep -q "Mach-O" || die "Downloaded ffmpeg is not a Mach-O executable"
  lipo -archs "${binary}" | tr ' ' '\n' | grep -qx "${arch}" \
    || die "Downloaded ffmpeg does not contain the ${arch} slice"

  log "[4/5] Installing into ${dest}"
  mkdir -p "${dest}"
  install -m 755 "${binary}" "${dest}/ffmpeg"
  /usr/bin/xattr -dr com.apple.quarantine "${dest}/ffmpeg" >/dev/null 2>&1 || true
  curl -fsSL --retry 3 --max-time 120 "${LICENSE_URL}" -o "${dest}/COPYING.GPLv3" \
    || log "WARNING: could not download COPYING.GPLv3; add it manually"

  log "[5/5] Verifying codec"
  binary_sha="$(shasum -a 256 "${dest}/ffmpeg" | awk '{print $1}')"
  zip_sha="$(shasum -a 256 "${work}/ffmpeg.zip" | awk '{print $1}')"
  version="unknown (cannot execute this slice on $(uname -m))"
  configuration="unavailable"
  runnable=0
  if lipo -archs "${dest}/ffmpeg" | tr ' ' '\n' | grep -qx "${HOST_ARCH}"; then
    runnable=1
  elif [[ "${HOST_ARCH}" == "arm64" ]] && lipo -archs "${dest}/ffmpeg" | tr ' ' '\n' | grep -qx "x86_64"; then
    runnable=1   # Apple Silicon 上可通过 Rosetta 运行 Intel 切片
  fi

  if [[ "${runnable}" -eq 1 ]]; then
    version_line="$("${dest}/ffmpeg" -hide_banner -version 2>/dev/null | head -n 1 || true)"
    [[ -n "${version_line}" ]] && version="${version_line}"
    configuration="$("${dest}/ffmpeg" -hide_banner -version 2>/dev/null \
      | grep -m1 '^ *configuration:' | sed 's/^ *configuration: *//' || true)"
    [[ -n "${configuration}" ]] || configuration="unavailable"

    encoders="$("${dest}/ffmpeg" -hide_banner -encoders 2>&1 || true)"
    missing=()
    for name in "${REQUIRED_ENCODERS[@]}"; do
      if ! printf '%s' "${encoders}" | grep -qE "^[[:space:]]*[VAS][^[:space:]]*[[:space:]]+${name}[[:space:]]"; then
        missing+=("${name}")
      fi
    done
    if [[ ${#missing[@]} -gt 0 ]]; then
      rm -rf "${work}"
      die "This build lacks required encoders: ${missing[*]}"
    fi
  else
    log "Skipping execution checks: the ${arch} slice cannot run on this machine."
  fi

  write_notice "${arch}" "${dest}" "${zip_url}" "${zip_sha}" "${binary_sha}" "${version}" "${configuration}"
  rm -rf "${work}"
  log "Done: ${dest}/ffmpeg"
done

log "Rebuild SpectrumTagStandalone; CMake relocates each helper into"
log "SpectrumTag.app/Contents/Helpers/<arch>/ with its licence and build notice."
