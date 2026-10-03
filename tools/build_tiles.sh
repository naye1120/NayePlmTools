#!/bin/bash
# Build the two home-screen tile variants of NayePlmTools in one run:
#   NayePlmTools_<ver>-me.elf  -> media tab   (NAYE00001, applicationCategoryType 65536)
#   NayePlmTools_<ver>-ga.elf  -> game tab    (NAYE00004, applicationCategoryType 0)
# Both carry the same embedded tile artwork (assets/icon0.png + assets/pic0.png
# + assets/pic0.dds) and deep-link to http://127.0.0.1:8911/, so either tile
# opens the running panel.
#
# Usage (WSL, SDK expected at /opt/ps5-payload-sdk):
#   cd /mnt/d/pldmanager/ps5-payload-manager && bash tools/build_tiles.sh
set -euo pipefail

cd "$(dirname "$0")/.."

SDK_BIN=/opt/ps5-payload-sdk/bin
if [ ! -x "$SDK_BIN/prospero-clang" ]; then
    echo "ERROR: $SDK_BIN/prospero-clang not found - run this inside WSL/Docker with the PS5 SDK." >&2
    exit 1
fi

VERSION=$(grep '#define MENU_VERSION' include/pldmgr.h | awk '{print $3}' | tr -d '"' | tr -d '\r')
ELF_ME="NayePlmTools_${VERSION}-me.elf"
ELF_GA="NayePlmTools_${VERSION}-ga.elf"

for asset in assets/param.json assets/param.game.json assets/icon0.png assets/pic0.png assets/pic0.dds; do
    [ -f "$asset" ] || { echo "ERROR: missing tile asset $asset" >&2; exit 1; }
done

if [ ! -f frontend/dist/index.html ]; then
    echo "[0/3] frontend/dist missing -> building frontend"
    make frontend-build
else
    echo "[0/3] reusing existing frontend/dist (delete it to force an npm rebuild)"
fi

echo "[1/3] media-tab tile  -> $ELF_ME"
make clean ELF="$ELF_ME" >/dev/null
make all ELF="$ELF_ME"

# Only app_installer.c depends on the variant macro; dropping its object forces a
# recompile without disturbing the ELF built in the previous step.
echo "[2/3] game-tab tile   -> $ELF_GA"
rm -f src/app_installer.o include/assets_*.h
make all ELF="$ELF_GA" VARIANT_CFLAGS=-DTILE_VARIANT_GAME

echo "[3/3] results"
ls -l "$ELF_ME" "$ELF_GA"

for pair in "$ELF_ME NAYE00001" "$ELF_GA NAYE00004"; do
    set -- $pair
    if grep -aq "$2" "$1"; then
        echo "  OK   $1 contains $2"
    else
        echo "  FAIL $1 does not contain $2" >&2
        exit 1
    fi
done
