#!/bin/sh
# Cross-compile risergamepad for AmigaOS m68k using a local vbcc install.
# Adjust VBCC_ROOT / TARGET_ROOT if you keep the toolchain elsewhere.
set -e

VBCC_ROOT=${VBCC_ROOT:-/home/tore/opt/amitools/vbcc}
TARGET_ROOT=${TARGET_ROOT:-/home/tore/opt/amitools/target/vbcc_target_m68k-amigaos/targets/m68k-amigaos}
VASM_DIR=${VASM_DIR:-/home/tore/opt/amitools/vasm}
VLINK_DIR=${VLINK_DIR:-/home/tore/opt/amitools/vlink}

if [ ! -x "$VBCC_ROOT/bin/vbccm68k" ] || [ ! -d "$TARGET_ROOT/include" ]; then
    echo "vbcc not found. Build it first (vasm, vlink, vbcc + m68k-amigaos target)."
    exit 1
fi

export PATH="$VBCC_ROOT/bin:$VASM_DIR:$VLINK_DIR:$PATH"
export VBCC="$VBCC_ROOT"

cd "$(dirname "$0")"
vc +aos68k -O2 risergamepad.c risergamepad-gui.c cd32_bitmap.c -o risergamepad
echo "Built: $(pwd)/risergamepad ($(wc -c < risergamepad) bytes)"
file risergamepad
