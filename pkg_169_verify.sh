#!/bin/bash
# Verify NayePlmTools_0.5.2.169 build artifacts (both tile variants).
#
# [CN-FORK] 本脚本**继承** pkg_168_verify.sh 的全部检查，再加两条 .169 专属断言。
# 每轮出包都基于上一版加断言，不从零写 —— 老断言能守住的老问题别让它回流。
#
# Checks performed (per CN-FORK packaging gate):
#   1. Both ELFs exist, are non-trivial size, and report md5 + byte size.
#   2. Embedded title carries the correct version AND the real commit hash
#      (1f41eba) — i.e. NOT stamped "DEV".
#   3. No leftover [[TITLE_PLACEHOLDER]] (injection ran) and no 0.5.2.168 residual.
#   4. Byte-exact: the embedded web blob is extracted from each ELF and cmp'd
#      against frontend/dist/index.html.
#   5. [.169] 排序入口搬迁的**产物层**指纹：
#        · 旧口径「右上角还有」必须**消失**（原来是 tile_hint 里说磁贴右上角有 ⇅）；
#        · 新口径「排序按钮在板块标题栏右上角」+「所有磁贴共用一个」必须**在**。
#      这两条是「磁贴上的 ⇅ 撤走、改成标题栏一颗」在压缩产物里唯一可读的形状 ——
#      产物是 minified 的，结构断言（比如「磁贴右上角只剩一颗按钮」）做不了，
#      只能靠文案指纹。真正的结构断言在 game112 第 12.3 节（真页面 DOM 实测）。
#
# Usage (WSL — needs grep/cmp from coreutils):
#   bash pkg_169_verify.sh
set -uo pipefail

VER=0.5.2.169
COMMIT=1f41eba
PREV=0.5.2.168
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

# --- [.169] 文案指纹：先验 dist，dist 错了后面 ELF 的字节比对必然一起错 ----------
echo "=== dist 文案指纹（v0.5.2.169 排序入口搬迁）==="
N_OLD=$(grep -o '右上角还有' "$DIST" | wc -l | tr -d ' ')
[ "$N_OLD" -eq 0 ] && ok "旧口径「右上角还有」已消失（磁贴右上角不再说 ⇅ 排序）" \
                    || bad "旧口径「右上角还有」还在（${N_OLD} 处，tile_hint 没改干净）"
for S in '排序按钮在板块标题栏右上角' '所有磁贴共用一个'; do
  N=$(grep -o "$S" "$DIST" | wc -l | tr -d ' ')
  [ "$N" -ge 1 ] && ok "新口径「${S}」在（${N} 处 = JSX 兜底 + 语言包）" \
                 || bad "新口径「${S}」不在（tile_hint / pref_hint 没落进产物）"
done
# icon-btn 是新按钮的 className，排序档位那一组走 segmented-item —— 数量不该是 1
# （页面别处也用 icon-btn），但**必须 > 0**，否则标题栏那颗根本没编进去。
N_ICON=$(grep -o 'icon-btn' "$DIST" | wc -l | tr -d ' ')
[ "$N_ICON" -ge 1 ] && ok "icon-btn 在产物里（${N_ICON} 处，含标题栏那颗）" \
                    || bad "icon-btn 不在产物里 —— 标题栏那颗排序按钮没编进去"
grep -q 'arrow-up-down' "$DIST" && ok 'ArrowUpDown 图标在产物里（标题栏那颗用的就是它）' \
                                    || bad 'arrow-up-down 不在产物里 —— 排序按钮图标丢了'

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
