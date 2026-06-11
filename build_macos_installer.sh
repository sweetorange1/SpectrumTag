#!/usr/bin/env bash
set -euo pipefail

# Pupon macOS packaging script
# - Builds an installer .pkg containing VST3 + AU
# - Installs presets into the default runtime folder: ~/Documents/puponpresent
# - Wraps the .pkg into a distributable .dmg

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="${SCRIPT_DIR}"
BUILD_DIR="${PROJECT_ROOT}/cmake-build-release"
ARTEFACTS_DIR="${BUILD_DIR}/Puponvst_artefacts/Release"
DIST_DIR="${PROJECT_ROOT}/dist"
WORK_DIR="${DIST_DIR}/.mac_installer_work"
PKG_ROOT_DIR="${WORK_DIR}/pkg_root"
PKG_SCRIPTS_DIR="${WORK_DIR}/pkg_scripts"
DMG_STAGE_DIR="${WORK_DIR}/dmg_stage"

PRODUCT_NAME="Pupon"
PKG_IDENTIFIER="cn.iisaacbeats.pupon"
PRESET_FOLDER_NAME="puponpresent"
PRESET_SRC_DIR="${PROJECT_ROOT}/presents"

VST3_BUNDLE="${ARTEFACTS_DIR}/VST3/${PRODUCT_NAME}.vst3"
AU_BUNDLE="${ARTEFACTS_DIR}/AU/${PRODUCT_NAME}.component"

DO_SIGN=1
VERSION=""

log() { echo "[build_macos_installer] $*"; }

usage() {
  cat <<'EOF'
Usage:
  ./build_macos_installer.sh
  ./build_macos_installer.sh --no-sign
  ./build_macos_installer.sh --version 1.0.4

Options:
  --no-sign       Skip ad-hoc codesign for plugin bundles before packaging.
  --version VER   Override version (default: parsed from CMakeLists.txt).
  -h, --help      Show this help.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-sign)
      DO_SIGN=0
      shift
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

for cmd in hdiutil pkgbuild codesign; do
  if ! command -v "${cmd}" >/dev/null 2>&1; then
    echo "[ERROR] Missing required command: ${cmd}" >&2
    exit 1
  fi
done

if [[ -z "${VERSION}" ]]; then
  VERSION="$(awk '
    /juce_add_plugin/,/\)/ {
      if ($1 == "VERSION") { print $2; exit }
    }
  ' "${PROJECT_ROOT}/CMakeLists.txt" | tr -d '"')"
fi

if [[ -z "${VERSION}" ]]; then
  echo "[ERROR] Could not resolve VERSION from CMakeLists.txt. Use --version x.y.z" >&2
  exit 1
fi

PKG_NAME="${PRODUCT_NAME}-${VERSION}-macOS-installer.pkg"
PKG_PATH="${DIST_DIR}/${PKG_NAME}"
DMG_NAME="${PRODUCT_NAME}-${VERSION}-macOS.dmg"
DMG_PATH="${DIST_DIR}/${DMG_NAME}"

log "Step 1/6 Validate required build artifacts"
if [[ ! -d "${VST3_BUNDLE}" ]]; then
  echo "[ERROR] Missing VST3 bundle: ${VST3_BUNDLE}" >&2
  echo "[HINT] Build Release target Puponvst_VST3 first." >&2
  exit 1
fi
if [[ ! -d "${AU_BUNDLE}" ]]; then
  echo "[ERROR] Missing AU bundle: ${AU_BUNDLE}" >&2
  echo "[HINT] Build Release target Puponvst_AU first." >&2
  exit 1
fi
if [[ ! -d "${PRESET_SRC_DIR}" ]]; then
  echo "[ERROR] Missing preset source folder: ${PRESET_SRC_DIR}" >&2
  exit 1
fi

log "  - VST3: ${VST3_BUNDLE}"
log "  - AU  : ${AU_BUNDLE}"
log "  - Presets: ${PRESET_SRC_DIR}"

if [[ "${DO_SIGN}" -eq 1 ]]; then
  log "Step 2/6 Ad-hoc sign plugin bundles"
  sign_bundle() {
    local bundle="$1"
    codesign --force --deep --sign - --options runtime --timestamp=none "${bundle}"
    codesign --verify --deep --strict --verbose=2 "${bundle}" >/dev/null || true
  }
  sign_bundle "${VST3_BUNDLE}"
  sign_bundle "${AU_BUNDLE}"
else
  log "Step 2/6 Skip signing (--no-sign)"
fi

log "Step 3/6 Prepare pkg payload and scripts"
rm -rf "${WORK_DIR}"
mkdir -p "${PKG_ROOT_DIR}/Library/Audio/Plug-Ins/VST3"
mkdir -p "${PKG_ROOT_DIR}/Library/Audio/Plug-Ins/Components"
mkdir -p "${PKG_SCRIPTS_DIR}/presets"
mkdir -p "${DIST_DIR}"

# Payload: system plugin locations
/usr/bin/ditto "${VST3_BUNDLE}" "${PKG_ROOT_DIR}/Library/Audio/Plug-Ins/VST3/${PRODUCT_NAME}.vst3"
/usr/bin/ditto "${AU_BUNDLE}" "${PKG_ROOT_DIR}/Library/Audio/Plug-Ins/Components/${PRODUCT_NAME}.component"

# Script resource: user preset files copied by postinstall
/usr/bin/ditto "${PRESET_SRC_DIR}" "${PKG_SCRIPTS_DIR}/presets"

cat > "${PKG_SCRIPTS_DIR}/postinstall" <<'POSTINSTALL'
#!/bin/bash
set -euo pipefail

PRESET_FOLDER_NAME="puponpresent"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PRESET_SRC_DIR="${SCRIPT_DIR}/presets"

# Resolve the active console user (the user running Installer.app)
CONSOLE_USER="$(/usr/bin/stat -f %Su /dev/console)"
if [[ -z "${CONSOLE_USER}" || "${CONSOLE_USER}" == "root" ]]; then
  echo "[Pupon postinstall] No valid console user; skip preset install." >&2
  exit 0
fi

USER_HOME="$(/usr/bin/dscl . -read "/Users/${CONSOLE_USER}" NFSHomeDirectory 2>/dev/null | awk '{print $2}')"
if [[ -z "${USER_HOME}" ]]; then
  USER_HOME="/Users/${CONSOLE_USER}"
fi

TARGET_DIR="${USER_HOME}/Documents/${PRESET_FOLDER_NAME}"
/bin/mkdir -p "${TARGET_DIR}"
/usr/bin/ditto "${PRESET_SRC_DIR}" "${TARGET_DIR}"
/usr/sbin/chown -R "${CONSOLE_USER}:staff" "${TARGET_DIR}" || true

echo "[Pupon postinstall] Presets installed to ${TARGET_DIR}"
exit 0
POSTINSTALL
chmod +x "${PKG_SCRIPTS_DIR}/postinstall"

log "Step 4/6 Build .pkg"
rm -f "${PKG_PATH}"
pkgbuild \
  --root "${PKG_ROOT_DIR}" \
  --scripts "${PKG_SCRIPTS_DIR}" \
  --identifier "${PKG_IDENTIFIER}" \
  --version "${VERSION}" \
  --install-location "/" \
  "${PKG_PATH}"

log "Step 5/6 Build .dmg"
rm -rf "${DMG_STAGE_DIR}"
mkdir -p "${DMG_STAGE_DIR}"
/usr/bin/ditto "${PKG_PATH}" "${DMG_STAGE_DIR}/${PKG_NAME}"

cat > "${DMG_STAGE_DIR}/README.txt" <<EOF
${PRODUCT_NAME} ${VERSION} macOS installer
========================================

This package installs:
  - ${PRODUCT_NAME}.vst3      -> /Library/Audio/Plug-Ins/VST3/
  - ${PRODUCT_NAME}.component -> /Library/Audio/Plug-Ins/Components/

Presets are installed to:
  - ~/Documents/${PRESET_FOLDER_NAME}

After installation, reopen your DAW and rescan plugins if needed.
EOF

rm -f "${DMG_PATH}"
hdiutil create \
  -volname "${PRODUCT_NAME} ${VERSION}" \
  -srcfolder "${DMG_STAGE_DIR}" \
  -ov \
  -format UDZO \
  -imagekey zlib-level=9 \
  "${DMG_PATH}" >/dev/null

log "Step 6/6 Cleanup"
rm -rf "${WORK_DIR}"

log "Done"
log "  - PKG: ${PKG_PATH}"
log "  - DMG: ${DMG_PATH}"

