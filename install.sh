#!/bin/bash
# install.sh - Install obs-ltc-timecode plugin for OBS Studio (Linux)
#
# Usage:
#   ./install.sh                     # Install from build output (build_x86_64)
#   ./install.sh --build-dir "path"  # Install from custom build directory
#   ./install.sh --uninstall         # Remove the plugin
#
# Installs to ~/.config/obs-studio/plugins/obs-ltc-timecode/

set -e

PLUGIN_NAME="obs-ltc-timecode"
OBS_CONFIG="${HOME}/.config/obs-studio"
PLUGIN_DIR="${OBS_CONFIG}/plugins/${PLUGIN_NAME}"
SO_NAME="${PLUGIN_NAME}.so"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

print_header() {
    echo ""
    echo -e "${CYAN}=== obs-ltc-timecode Installer ===${NC}"
    echo ""
}

find_build_dir() {
    local candidates=(
        "build_x86_64"
        "build"
    )
    for dir in "${candidates[@]}"; do
        local so_path="${SCRIPT_DIR}/${dir}/${SO_NAME}"
        if [ -f "$so_path" ]; then
            echo "${SCRIPT_DIR}/${dir}"
            return 0
        fi
    done
    return 1
}

# Parse arguments
BUILD_DIR=""
UNINSTALL=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --uninstall)
            UNINSTALL=true
            shift
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Usage: $0 [--build-dir <path>] [--uninstall]"
            exit 1
            ;;
    esac
done

# --- Uninstall ---
if [ "$UNINSTALL" = true ]; then
    print_header
    if [ -d "$PLUGIN_DIR" ]; then
        rm -rf "$PLUGIN_DIR"
        echo -e "${GREEN}[OK] Plugin removed from: ${PLUGIN_DIR}${NC}"
    else
        echo -e "${YELLOW}[INFO] Plugin not installed at: ${PLUGIN_DIR}${NC}"
    fi
    exit 0
fi

# --- Install ---
print_header

# Step 1: Check OBS is installed
if command -v obs &> /dev/null; then
    echo -e "${GREEN}[OK] OBS Studio found: $(which obs)${NC}"
elif [ -f "/usr/bin/obs" ]; then
    echo -e "${GREEN}[OK] OBS Studio found: /usr/bin/obs${NC}"
else
    echo -e "${YELLOW}[WARN] OBS Studio not found in PATH.${NC}"
    echo -e "${YELLOW}       Make sure OBS Studio is installed.${NC}"
fi

# Step 2: Find the plugin .so
if [ -z "$BUILD_DIR" ]; then
    BUILD_DIR=$(find_build_dir) || true
fi

if [ -z "$BUILD_DIR" ] || [ ! -f "${BUILD_DIR}/${SO_NAME}" ]; then
    echo -e "${RED}[ERROR] Plugin .so not found.${NC}"
    echo ""
    echo -e "${RED}  Expected: <build-dir>/${SO_NAME}${NC}"
    echo ""
    echo -e "${YELLOW}  Build the plugin first:${NC}"
    echo "    cmake --preset ubuntu-x86_64"
    echo "    cmake --build --preset ubuntu-x86_64"
    echo ""
    echo -e "${CYAN}  Or specify the build directory:${NC}"
    echo "    ./install.sh --build-dir /path/to/build"
    exit 1
fi

echo -e "${GREEN}[OK] Plugin .so found: ${BUILD_DIR}/${SO_NAME}${NC}"

# Step 3: Find locale data
DATA_DIR="${SCRIPT_DIR}/data"
if [ ! -f "${DATA_DIR}/locale/en-US.ini" ]; then
    echo -e "${RED}[ERROR] Locale data not found at: ${DATA_DIR}/locale/en-US.ini${NC}"
    echo -e "${RED}        Make sure you run this from the project root.${NC}"
    exit 1
fi
echo -e "${GREEN}[OK] Locale data found: ${DATA_DIR}${NC}"

# Step 4: Check OBS config directory
if [ ! -d "$OBS_CONFIG" ]; then
    echo -e "${RED}[ERROR] OBS config directory not found: ${OBS_CONFIG}${NC}"
    echo -e "${RED}        Run OBS Studio at least once before installing plugins.${NC}"
    exit 1
fi
echo -e "${GREEN}[OK] OBS config directory exists: ${OBS_CONFIG}${NC}"

# Step 5: Create plugin directory structure
BIN_DIR="${PLUGIN_DIR}/bin/64bit"
DATA_DEST_DIR="${PLUGIN_DIR}/data"

if [ ! -d "${OBS_CONFIG}/plugins" ]; then
    echo -e "${YELLOW}[INFO] Creating plugins directory (does not exist by default, this is normal)${NC}"
fi

mkdir -p "$BIN_DIR"
mkdir -p "$DATA_DEST_DIR"
echo -e "${GREEN}[OK] Plugin directory created: ${PLUGIN_DIR}${NC}"

# Step 6: Copy files
cp "${BUILD_DIR}/${SO_NAME}" "${BIN_DIR}/"
echo -e "${GREEN}[OK] Copied: ${SO_NAME} -> ${BIN_DIR}/${NC}"

# libltc is dynamically linked (LGPLv3, SHARED). The plugin .so has an
# RPATH of $ORIGIN, so libltc.so must sit next to it in bin/64bit or the
# plugin fails to load. This is the Linux equivalent of install.ps1's
# libltc.dll copy — without it, OBS silently skips the plugin.
LIBLTC_FOUND=false
for libltc in "${BUILD_DIR}"/libltc.so*; do
    if [ -f "$libltc" ]; then
        cp -P "$libltc" "${BIN_DIR}/"
        echo -e "${GREEN}[OK] Copied: $(basename "$libltc") -> ${BIN_DIR}/${NC}"
        LIBLTC_FOUND=true
    fi
done
if [ "$LIBLTC_FOUND" = false ]; then
    echo -e "${YELLOW}[WARN] libltc.so not found in ${BUILD_DIR}.${NC}"
    echo -e "${YELLOW}       The plugin will FAIL to load without it. Rebuild the project.${NC}"
fi

cp -r "${DATA_DIR}/"* "${DATA_DEST_DIR}/"
echo -e "${GREEN}[OK] Copied: data/ -> ${DATA_DEST_DIR}/${NC}"

# Step 7: Verify installation
echo ""
echo -e "${CYAN}--- Verification ---${NC}"

ALL_OK=true

check_file() {
    local path="$1"
    local desc="$2"
    if [ -f "$path" ]; then
        local size=$(stat -c%s "$path" 2>/dev/null || stat -f%z "$path" 2>/dev/null || echo "?")
        echo -e "  ${GREEN}[OK] ${desc}: ${path} (${size} bytes)${NC}"
    else
        echo -e "  ${RED}[FAIL] ${desc}: ${path}${NC}"
        ALL_OK=false
    fi
}

check_file "${BIN_DIR}/${SO_NAME}" "Plugin .so"
check_file "${DATA_DEST_DIR}/locale/en-US.ini" "Locale file (en-US)"

# Verify libltc landed (glob, since the filename carries a version suffix)
if compgen -G "${BIN_DIR}/libltc.so*" > /dev/null; then
    echo -e "  ${GREEN}[OK] libltc shared library present in ${BIN_DIR}${NC}"
else
    echo -e "  ${RED}[FAIL] libltc shared library missing — plugin will not load${NC}"
    ALL_OK=false
fi

echo ""
if [ "$ALL_OK" = true ]; then
    echo -e "${GREEN}Installation successful!${NC}"
    echo ""
    echo -e "${CYAN}Next steps:${NC}"
    echo "  1. Restart OBS Studio (close and reopen)"
    echo "  2. Go to Sources -> + -> 'LTC Timecode Generator'"
    echo "  3. Check Help -> Log Files if the source doesn't appear"
    echo ""
    echo -e "Installed to: ${PLUGIN_DIR}"
else
    echo -e "${RED}Installation completed with errors. Check the messages above.${NC}"
    exit 1
fi
