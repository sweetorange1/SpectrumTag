#!/usr/bin/env bash
set -euo pipefail

# SpectrumTag macOS packaging script
# ----------------------------------------------------------------------------
# 形态与 Windows 安装包（SpectrumTag_installer.iss）保持一致：
#   · Standalone 应用为主组件 —— 必装（对应 Inno Setup 的 Flags: fixed）
#   · VST3 + AU 插件为可选组件 —— 默认勾选，用户可在"自定义安装"里取消
# 产物：
#   dist/SpectrumTag-<version>-macOS.pkg   （Installer 安装包，含组件选择页）
#   dist/SpectrumTag-<version>-macOS.dmg   （分发用磁盘镜像，内含上面的 pkg）
#
# 依赖：command line tools 自带的 pkgbuild / productbuild / hdiutil / codesign / ditto
# ----------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="${SCRIPT_DIR}"
BUILD_DIR="${PROJECT_ROOT}/cmake-build-release"
ARTEFACTS_DIR="${BUILD_DIR}/SpectrumTag_artefacts/Release"
DIST_DIR="${PROJECT_ROOT}/dist"
WORK_DIR="${DIST_DIR}/.mac_installer_work"
STAGE_DIR="${WORK_DIR}/stage"
COMPONENTS_DIR="${WORK_DIR}/components"
SCRIPTS_DIR="${WORK_DIR}/scripts"
DMG_STAGE_DIR="${WORK_DIR}/dmg_stage"

PRODUCT_NAME="SpectrumTag"
PKG_ID_BASE="cn.iisaacbeats.spectrumtag"
STANDALONE_PKG_ID="${PKG_ID_BASE}.standalone"
PLUGINS_PKG_ID="${PKG_ID_BASE}.plugins"

STANDALONE_APP="${BUILD_DIR}/SpectrumTagStandalone_artefacts/Release/${PRODUCT_NAME}.app"
VST3_BUNDLE="${ARTEFACTS_DIR}/VST3/${PRODUCT_NAME}.vst3"
AU_BUNDLE="${ARTEFACTS_DIR}/AU/${PRODUCT_NAME}.component"

# 暂存区副本（签名只作用于这些副本，构建目录里的原始产物保持不动）
STAGED_APP="${STAGE_DIR}/standalone/${PRODUCT_NAME}.app"
STAGED_VST3="${STAGE_DIR}/plugins/Library/Audio/Plug-Ins/VST3/${PRODUCT_NAME}.vst3"
STAGED_AU="${STAGE_DIR}/plugins/Library/Audio/Plug-Ins/Components/${PRODUCT_NAME}.component"

HAVE_VST3=0
HAVE_AU=0

DO_SIGN=1
DO_PLUGINS=1
KEEP_WORK=0
SIGN_IDENTITY="-"     # 默认 ad-hoc 签名；用 --identity 指定 Developer ID
VERSION=""

log() { echo "[build_macos_installer] $*"; }

# ---- 架构探测：确认产物是 Intel / Apple Silicon 双架构 universal ------------
bundle_binary() {
  local bundle="$1"
  case "${bundle}" in
    *.app)       printf '%s' "${bundle}/Contents/MacOS/$(basename "${bundle}" .app)" ;;
    *.vst3)      printf '%s' "${bundle}/Contents/MacOS/$(basename "${bundle}" .vst3)" ;;
    *.component) printf '%s' "${bundle}/Contents/MacOS/$(basename "${bundle}" .component)" ;;
    *)           printf '%s' "" ;;
  esac
}

bundle_archs() {
  local bin
  bin="$(bundle_binary "$1")"
  if [[ -z "${bin}" || ! -f "${bin}" ]]; then
    printf '%s' "unknown"
    return 0
  fi
  /usr/bin/lipo -archs "${bin}" 2>/dev/null | tr '\n' ' ' | sed 's/[[:space:]]*$//'
}

usage() {
  cat <<'EOF'
Usage:
  ./build_macos_installer.sh [options]

Options:
  --version VER     Override version (default: parsed from CMakeLists.txt).
  --no-sign         Skip codesign before packaging.
  --identity NAME   Codesign identity (default: "-" ad-hoc).
                    e.g. --identity "Developer ID Application: Your Name (TEAMID)"
  --skip-plugins    Package the standalone app only (no VST3 / AU component).
  --keep-work       Keep dist/.mac_installer_work for troubleshooting.
  -h, --help        Show this help.

Examples:
  # 本地自测：ad-hoc 签名，打完整包（Standalone + 插件）
  ./build_macos_installer.sh

  # 正式发布：Developer ID 签名（之后仍需 notarize）
  ./build_macos_installer.sh --identity "Developer ID Application: iisaacbeats (XXXXXXXXXX)"
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-sign)
      DO_SIGN=0
      shift
      ;;
    --skip-plugins)
      DO_PLUGINS=0
      shift
      ;;
    --keep-work)
      KEEP_WORK=1
      shift
      ;;
    --identity)
      SIGN_IDENTITY="${2:-}"
      if [[ -z "${SIGN_IDENTITY}" ]]; then
        echo "[ERROR] --identity requires a value" >&2
        exit 1
      fi
      shift 2
      ;;
    --version)
      VERSION="${2:-}"
      if [[ -z "${VERSION}" ]]; then
        echo "[ERROR] --version requires a value" >&2
        exit 1
      fi
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "[ERROR] Unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "[ERROR] This script must run on macOS." >&2
  exit 1
fi

for cmd in ditto pkgbuild productbuild hdiutil codesign; do
  if ! command -v "${cmd}" >/dev/null 2>&1; then
    echo "[ERROR] Missing required command: ${cmd}" >&2
    exit 1
  fi
done

# ----------------------------------------------------------------------------
# Step 1/6  解析版本号
# ----------------------------------------------------------------------------
log "Step 1/7 Resolve version"

parse_version() {
  local file="${PROJECT_ROOT}/CMakeLists.txt"
  local v=""

  # 优先：project(SpectrumTag VERSION x.y.z ...)
  v="$(awk '/^[[:space:]]*project[[:space:]]*\(/ {
              for (i = 1; i <= NF; i++)
                if ($i == "VERSION") { print $(i + 1); exit }
            }' "${file}" | tr -d '"')"

  # 兜底：juce_add_plugin( ... VERSION x.y.z ... )
  if [[ -z "${v}" ]]; then
    v="$(awk '/juce_add_(plugin|gui_app)/,/^[[:space:]]*\)/ {
                for (i = 1; i <= NF; i++)
                  if ($i == "VERSION") { print $(i + 1); exit }
              }' "${file}" | tr -d '"')"
  fi

  printf '%s' "${v}"
}

if [[ -z "${VERSION}" ]]; then
  VERSION="$(parse_version)"
fi

if [[ -z "${VERSION}" ]]; then
  echo "[ERROR] Could not resolve VERSION from CMakeLists.txt. Use --version x.y.z" >&2
  exit 1
fi

PKG_NAME="${PRODUCT_NAME}-${VERSION}-macOS.pkg"
PKG_PATH="${DIST_DIR}/${PKG_NAME}"
DMG_NAME="${PRODUCT_NAME}-${VERSION}-macOS.dmg"
DMG_PATH="${DIST_DIR}/${DMG_NAME}"

log "  - Version: ${VERSION}"

# ----------------------------------------------------------------------------
# Step 2/6  校验构建产物
#   Standalone 是主组件，缺失直接失败（与 Windows 侧 standalone 组件 fixed 一致）
#   插件是可选组件，缺失只告警
# ----------------------------------------------------------------------------
log "Step 2/7 Validate build artifacts"

if [[ ! -d "${STANDALONE_APP}" ]]; then
  echo "[ERROR] Missing standalone app: ${STANDALONE_APP}" >&2
  echo "[HINT] Build first:" >&2
  echo "       cmake --build cmake-build-release --config Release --target SpectrumTagStandalone" >&2
  exit 1
fi
log "  - App : ${STANDALONE_APP} [$(bundle_archs "${STANDALONE_APP}")]"

if [[ "${DO_PLUGINS}" -eq 1 ]]; then
  if [[ -d "${VST3_BUNDLE}" ]]; then HAVE_VST3=1; log "  - VST3: ${VST3_BUNDLE} [$(bundle_archs "${VST3_BUNDLE}")]";
  else log "  - VST3: NOT FOUND (skipped)"; fi

  if [[ -d "${AU_BUNDLE}" ]]; then HAVE_AU=1; log "  - AU  : ${AU_BUNDLE} [$(bundle_archs "${AU_BUNDLE}")]";
  else log "  - AU  : NOT FOUND (skipped)"; fi

  if [[ "${HAVE_VST3}" -eq 0 && "${HAVE_AU}" -eq 0 ]]; then
    log "[WARN] No plug-in bundles found, packaging standalone only."
    log "[HINT] Build Release target SpectrumTag_VST3 / SpectrumTag_AU to include them."
    DO_PLUGINS=0
  fi
else
  log "  - Plug-ins: skipped (--skip-plugins)"
fi

# ---- 双架构校验：任一产物缺少 arm64 或 x86_64 就告警 ------------------------
ARCH_TEXT=""
UNIVERSAL_OK=1
verify_universal() {
  local archs
  archs="$(bundle_archs "$1")"
  [[ -n "${ARCH_TEXT}" ]] || ARCH_TEXT="${archs}"
  if [[ "${archs}" != *arm64* || "${archs}" != *x86_64* ]]; then
    log "[WARN] Not a universal binary: $1 -> ${archs}"
    log "[HINT] Configure with -DCMAKE_OSX_ARCHITECTURES=\"arm64;x86_64\" and rebuild."
    UNIVERSAL_OK=0
  fi
}
verify_universal "${STANDALONE_APP}"
if [[ "${HAVE_VST3}" -eq 1 ]]; then verify_universal "${VST3_BUNDLE}"; fi
if [[ "${HAVE_AU}"   -eq 1 ]]; then verify_universal "${AU_BUNDLE}";   fi
if [[ "${UNIVERSAL_OK}" -eq 1 ]]; then
  log "  - Universal binary OK (arm64 + x86_64)"
fi

# ----------------------------------------------------------------------------
# Step 3/7  复制产物到暂存区
#   注意：签名一律只作用于这里的副本，绝不改动 cmake 构建目录里的原始产物。
#   否则会顺手给本地调试用的 app 加上 hardened runtime（且没有 get-task-allow），
#   导致 CLion / lldb 无法附加进程："Not allowed to attach to process"。
# ----------------------------------------------------------------------------
log "Step 3/7 Stage bundles"

rm -rf "${WORK_DIR}"
mkdir -p "${DIST_DIR}" "${COMPONENTS_DIR}" "${SCRIPTS_DIR}"

mkdir -p "${STAGE_DIR}/standalone"
/usr/bin/ditto "${STANDALONE_APP}" "${STAGED_APP}"
log "  - ${PRODUCT_NAME}.app"

if [[ "${DO_PLUGINS}" -eq 1 ]]; then
  mkdir -p "${STAGE_DIR}/plugins/Library/Audio/Plug-Ins/VST3"
  mkdir -p "${STAGE_DIR}/plugins/Library/Audio/Plug-Ins/Components"

  if [[ "${HAVE_VST3}" -eq 1 ]]; then
    /usr/bin/ditto "${VST3_BUNDLE}" "${STAGED_VST3}"
    log "  - ${PRODUCT_NAME}.vst3"
  fi
  if [[ "${HAVE_AU}" -eq 1 ]]; then
    /usr/bin/ditto "${AU_BUNDLE}" "${STAGED_AU}"
    log "  - ${PRODUCT_NAME}.component"
  fi
fi

# ----------------------------------------------------------------------------
# Step 4/7  签名（只签暂存区副本）
# ----------------------------------------------------------------------------
if [[ "${DO_SIGN}" -eq 1 ]]; then
  log "Step 4/7 Codesign staged bundles (identity: ${SIGN_IDENTITY})"

  sign_bundle() {
    local bundle="$1"
    local ts_args=()
    if [[ "${SIGN_IDENTITY}" == "-" ]]; then
      ts_args=(--timestamp=none)
    else
      ts_args=(--timestamp)
    fi

    codesign --force --deep --sign "${SIGN_IDENTITY}" --options runtime \
             "${ts_args[@]}" "${bundle}"
    codesign --verify --deep --strict --verbose=2 "${bundle}" >/dev/null || true
  }

  sign_bundle "${STAGED_APP}"
  if [[ "${HAVE_VST3}" -eq 1 ]]; then sign_bundle "${STAGED_VST3}"; fi
  if [[ "${HAVE_AU}"   -eq 1 ]]; then sign_bundle "${STAGED_AU}";   fi
else
  log "Step 4/7 Skip signing (--no-sign)"
fi

# ----------------------------------------------------------------------------
# Step 5/7  打 component 包
# ----------------------------------------------------------------------------
log "Step 5/7 Build component packages"

# ---- 5a. Standalone 组件的 preinstall / postinstall ----
mkdir -p "${SCRIPTS_DIR}/standalone"
cat > "${SCRIPTS_DIR}/standalone/preinstall" <<'EOF'
#!/bin/sh
# 安装前关闭正在运行的旧版本，避免旧文件被占用（对应 Windows 侧的 CloseApplications=force）
/usr/bin/osascript -e 'tell application "SpectrumTag" to quit' >/dev/null 2>&1 || true
/usr/bin/pkill -x "SpectrumTag" >/dev/null 2>&1 || true
exit 0
EOF
cat > "${SCRIPTS_DIR}/standalone/postinstall" <<'EOF'
#!/bin/sh
APP_PATH="/Applications/SpectrumTag.app"
# 清掉隔离属性：ad-hoc 签名的 app 若带 com.apple.quarantine，首次启动会被 Gatekeeper 拦截
/usr/bin/xattr -dr com.apple.quarantine "$APP_PATH" >/dev/null 2>&1 || true
/bin/chmod -R a+rX "$APP_PATH" >/dev/null 2>&1 || true
exit 0
EOF
/bin/chmod +x "${SCRIPTS_DIR}/standalone/preinstall" "${SCRIPTS_DIR}/standalone/postinstall"

pkgbuild \
  --root "${STAGE_DIR}/standalone" \
  --identifier "${STANDALONE_PKG_ID}" \
  --version "${VERSION}" \
  --install-location "/Applications" \
  --scripts "${SCRIPTS_DIR}/standalone" \
  "${COMPONENTS_DIR}/standalone.pkg"
log "  - standalone.pkg -> /Applications/${PRODUCT_NAME}.app"

# ---- 5b. 插件组件（可选） ----
if [[ "${DO_PLUGINS}" -eq 1 ]]; then
  mkdir -p "${SCRIPTS_DIR}/plugins"
  {
    echo '#!/bin/sh'
    echo '# 清掉插件 bundle 的隔离属性，避免 Logic / 宿主把插件判定为不可用'
    if [[ "${HAVE_VST3}" -eq 1 ]]; then
      echo '/usr/bin/xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/SpectrumTag.vst3" >/dev/null 2>&1 || true'
    fi
    if [[ "${HAVE_AU}" -eq 1 ]]; then
      echo '/usr/bin/xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/Components/SpectrumTag.component" >/dev/null 2>&1 || true'
    fi
    echo 'exit 0'
  } > "${SCRIPTS_DIR}/plugins/postinstall"
  /bin/chmod +x "${SCRIPTS_DIR}/plugins/postinstall"

  pkgbuild \
    --root "${STAGE_DIR}/plugins" \
    --identifier "${PLUGINS_PKG_ID}" \
    --version "${VERSION}" \
    --install-location "/" \
    --scripts "${SCRIPTS_DIR}/plugins" \
    "${COMPONENTS_DIR}/plugins.pkg"
  log "  - plugins.pkg"
fi

# ----------------------------------------------------------------------------
# Step 6/7  合成带组件选择页的 product 包
#   standalone  : selected="true" enabled="false"  —— 必装，勾选框置灰
#   plugins     : selected="true" enabled="true"   —— 默认勾选，可取消
# ----------------------------------------------------------------------------
log "Step 6/7 Build product package"

if [[ "${DO_PLUGINS}" -eq 1 ]]; then
  CUSTOMIZE_ATTR="always"   # 有可选组件时才显示"自定义安装"
else
  CUSTOMIZE_ATTR="never"
fi

{
  echo '<?xml version="1.0" encoding="utf-8"?>'
  echo '<installer-gui-script minSpecVersion="1">'
  echo "    <title>${PRODUCT_NAME} ${VERSION}</title>"
  echo "    <options customize=\"${CUSTOMIZE_ATTR}\"/>"
  echo '    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>'
  echo '    <choices-outline>'
  echo "        <line choice=\"${STANDALONE_PKG_ID}\"/>"
  if [[ "${DO_PLUGINS}" -eq 1 ]]; then
    echo "        <line choice=\"${PLUGINS_PKG_ID}\"/>"
  fi
  echo '    </choices-outline>'
  echo "    <choice id=\"${STANDALONE_PKG_ID}\""
  echo "            title=\"${PRODUCT_NAME} (Standalone Application)\""
  echo "            description=\"The standalone application — offline audio stamping tool.\""
  echo '            enabled="false" selected="true">'
  echo "        <pkg-ref id=\"${STANDALONE_PKG_ID}\"/>"
  echo '    </choice>'
  if [[ "${DO_PLUGINS}" -eq 1 ]]; then
    echo "    <choice id=\"${PLUGINS_PKG_ID}\""
    echo "            title=\"${PRODUCT_NAME} Plug-ins (VST3 + AU)\""
    echo "            description=\"Audio plug-ins for your DAW.\""
    echo '            enabled="true" selected="true">'
    echo "        <pkg-ref id=\"${PLUGINS_PKG_ID}\"/>"
    echo '    </choice>'
  fi
  echo "    <pkg-ref id=\"${STANDALONE_PKG_ID}\" version=\"${VERSION}\" onConclusion=\"none\">standalone.pkg</pkg-ref>"
  if [[ "${DO_PLUGINS}" -eq 1 ]]; then
    echo "    <pkg-ref id=\"${PLUGINS_PKG_ID}\" version=\"${VERSION}\" onConclusion=\"none\">plugins.pkg</pkg-ref>"
  fi
  echo '</installer-gui-script>'
} > "${WORK_DIR}/Distribution.xml"

rm -f "${PKG_PATH}"
productbuild \
  --distribution "${WORK_DIR}/Distribution.xml" \
  --package-path "${COMPONENTS_DIR}" \
  "${PKG_PATH}"

# ----------------------------------------------------------------------------
# Step 7/7  打 dmg
# ----------------------------------------------------------------------------
log "Step 7/7 Build .dmg"

rm -rf "${DMG_STAGE_DIR}"
mkdir -p "${DMG_STAGE_DIR}"
/usr/bin/ditto "${PKG_PATH}" "${DMG_STAGE_DIR}/${PKG_NAME}"

{
  echo "${PRODUCT_NAME} ${VERSION} macOS installer"
  echo "========================================"
  echo
  echo "Architectures: ${ARCH_TEXT:-unknown}"
  echo
  echo "Open \"${PKG_NAME}\" to install:"
  echo
  echo "  1) ${PRODUCT_NAME} (Standalone Application)  [required]"
  echo "       -> /Applications/${PRODUCT_NAME}.app"
  if [[ "${DO_PLUGINS}" -eq 1 ]]; then
    echo "  2) ${PRODUCT_NAME} Plug-ins (VST3 + AU)      [optional]"
    if [[ "${HAVE_VST3}" -eq 1 ]]; then
      echo "       -> /Library/Audio/Plug-Ins/VST3/${PRODUCT_NAME}.vst3"
    fi
    if [[ "${HAVE_AU}" -eq 1 ]]; then
      echo "       -> /Library/Audio/Plug-Ins/Components/${PRODUCT_NAME}.component"
    fi
    echo
    echo "Click \"Customize\" on the Installation Type step to skip the plug-ins."
  else
    echo
    echo "(This build contains the standalone application only.)"
  fi
  echo
  echo "After installation, reopen your DAW and rescan plug-ins if needed."
} > "${DMG_STAGE_DIR}/README.txt"

rm -f "${DMG_PATH}"
hdiutil create \
  -volname "${PRODUCT_NAME} ${VERSION}" \
  -srcfolder "${DMG_STAGE_DIR}" \
  -ov \
  -format UDZO \
  -imagekey zlib-level=9 \
  "${DMG_PATH}" >/dev/null

if [[ "${KEEP_WORK}" -eq 1 ]]; then
  log "Keep work dir: ${WORK_DIR}"
else
  log "Cleanup"
  rm -rf "${WORK_DIR}"
fi

log "Done"
log "  - PKG: ${PKG_PATH}"
log "  - DMG: ${DMG_PATH}"
