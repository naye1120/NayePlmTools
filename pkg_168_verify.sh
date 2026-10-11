#!/bin/bash
# Verify NayePlmTools_0.5.2.168 build artifacts (both tile variants).
#
# Checks performed (per CN-FORK packaging gate):
#   1. Both ELFs exist, are non-trivial size, and report md5 + byte size.
#   2. Embedded title carries the correct version AND the real commit hash
#      (296d162) — i.e. NOT stamped "DEV".
#   3. No leftover [[TITLE_PLACEHOLDER]] (injection ran) and no 0.5.2.167 residual.
#   4. Byte-exact: the embedded web blob (symbol assets_index_html) is extracted
#      from each ELF and cmp'd against frontend/dist/index.html.
#
# Usage (WSL — needs readelf/objcopy family from the SDK binutils):
#   bash pkg_168_verify.sh
set -uo pipefail

VER=0.5.2.168
COMMIT=296d162
PREV=0.5.2.167
DIR="$(cd "$(dirname "$0")" && pwd)"
ELF_ME="$DIR/NayePlmTools_${VER}-me.elf"
ELF_GA="$DIR/NayePlmTools_${VER}-ga.elf"
DIST="$DIR/frontend/dist/index.html"

fail=0
ok(){ echo "  OK   $1"; }
bad(){ echo "  FAIL $1"; fail=1; }

[ -f "$DIST" ] || { echo "DIST MISSING: $DIST (run make frontend-build first)"; exit 1; }
DISTSZ=$(wc -c < "$DIST")
echo "dist/index.html bytes: $DISTSZ"

# unique prefix of the embedded HTML to locate its start inside the ELF
PREFIX=$(head -c 32 "$DIST")

for ELF in "$ELF_ME" "$ELF_GA"; do
  echo "=== $ELF ==="
  [ -f "$ELF" ] || { bad "ELF missing"; continue; }
  sz=$(wc -c < "$ELF"); echo "  size: $sz"
  echo "  md5:  $(md5sum "$ELF" | awk '{print $1}')"

  grep -aqF "$VER" "$ELF"    && ok "contains version $VER"    || bad "missing version $VER"
  grep -aqF "$COMMIT" "$ELF" && ok "contains commit $COMMIT"   || bad "missing commit $COMMIT"
  grep -aqF '[[TITLE_PLACEHOLDER]]' "$ELF" && bad "title placeholder not injected" || ok "title placeholder consumed"
  grep -aqF "by naye (DEV" "$ELF"        && bad "title stamped DEV"                 || ok "title not DEV"
  grep -aqF "$PREV" "$ELF"               && bad "residual $PREV present"            || ok "no $PREV residual"

  # --- byte-exact embed check ---
  # The web blob is embedded verbatim as a C array, so the first bytes of dist
  # appear once inside the ELF at the array's start. The array length equals the
  # dist file size (gen_assets embeds the whole file). The ELF is stripped, so we
  # can't read the symbol length from the symbol table — use dist size directly.
  START=$(grep -abo -m1 -F -f <(printf '%s' "$PREFIX") "$ELF" | head -1 | cut -d: -f1)
  LEN="$DISTSZ"
  if [ -n "${START:-}" ] && [ "${LEN:-0}" -gt 0 ]; then
    TMP=$(mktemp /tmp/nayeplm_blob.XXXXXX)
    tail -c "+$((START+1))" "$ELF" | head -c "$LEN" > "$TMP"
    if cmp -s "$TMP" "$DIST"; then ok "embedded web blob byte-exact (len=$LEN, start=+$START)";
    else bad "embedded web blob differs from dist (len=$LEN, start=+$START)"; fi
    rm -f "$TMP"
  else
    bad "could not locate embedded blob (START=$START LEN=$LEN)"
  fi
done

echo "===================="
if [ "$fail" -eq 0 ]; then echo "ALL CHECKS PASSED"; else echo "SOME CHECKS FAILED"; fi
exit $fail
