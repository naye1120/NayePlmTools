#!/bin/bash
# Verify NayePlmTools_0.5.2.171 build artifacts (both tile variants).
#
# [CN-FORK] 本脚本**继承** pkg_170_verify.sh 的全部检查，再加 .171 专属断言。
# 每轮出包都基于上一版加断言，不从零写 —— 老断言能守住的老问题别让它回流。
#
# .171 是**前端改动**（「关于」页：捐赠码换图片 + 补本 fork 仓库地址），
# 所以 .170 那组后端 SQL 指纹必须**继续全绿**（证明没碰坏改名链），
# 再加 .171 的前端/产物层指纹。
#
# Checks performed:
#   1. Both ELFs exist, non-trivial size, report md5 + byte size.
#   2. Embedded title carries correct version AND real commit hash — not DEV.
#   3. No leftover [[TITLE_PLACEHOLDER]] and no 0.5.2.170 residual.
#   4. Byte-exact: embedded web blob == frontend/dist/index.html.
#   5. [.170 继承] 改名修复的 ELF 层 SQL 字面量指纹（key GLOB 'TITLE_[0-9]*' 在、
#      旧写法 key IN ('TITLE','TITLE_01') 残留 0）。
#   6. [.171 新增] 捐赠码图片 + 仓库地址的产物层指纹：
#        · dist 里 `data:image/png;base64` 必须出现（图片**内联**了，否则 ELF 里必 404）；
#        · dist 里**不能**有外链 `donate-qr` 引用（外链 = 没内联）；
#        · `github.com/naye1120/NayePlmTools` 必须在 dist 与两个 ELF 里；
#        · 旧的 `github.com/naye/ps5-payload-manager`（画在二维码里的那个坏地址）**必须消失**；
#        · 新增文案「本 Fork 地址」/「本 Fork 仓库」必须在。
#
# Usage (WSL — needs grep/cmp from coreutils):
#   bash pkg_171_verify.sh
set -uo pipefail

VER=0.5.2.171
COMMIT=8b897fd
PREV=0.5.2.170
REPO_URL='github.com/naye1120/NayePlmTools'
OLD_URL='github.com/naye/ps5-payload-manager'
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

# --- [.171 新增] dist 层：捐赠码图片内联 + 仓库地址文案 ----------
echo "=== dist 文案指纹（.171 关于页：捐赠码图片 + 仓库地址）==="
N_DATAURI=$(grep -o 'data:image/png;base64' "$DIST" | wc -l | tr -d ' ')
[ "$N_DATAURI" -ge 1 ] && ok "捐赠码图片已内联（data:image/png;base64 ×${N_DATAURI}）" \
                       || bad "dist 里没有 base64 PNG —— 图片没内联，嵌进 ELF 后会 404"
N_EXT=$(grep -o 'donate-qr[^"'"'"']*\.png' "$DIST" | grep -v '^data:' | wc -l | tr -d ' ')
[ "$N_EXT" -eq 0 ] && ok "无外链 donate-qr 引用（图片走 data: URI）" \
                   || bad "dist 里还有 ${N_EXT} 处外链 donate-qr 引用"
for S in "$REPO_URL" '本 Fork 地址' '本 Fork 仓库'; do
  N=$(grep -o "$S" "$DIST" | wc -l | tr -d ' ')
  [ "$N" -ge 1 ] && ok "「${S}」在 dist（${N} 处）" || bad "「${S}」不在 dist"
done
N_OLDURL=$(grep -o "$OLD_URL" "$DIST" | wc -l | tr -d ' ')
[ "$N_OLDURL" -eq 0 ] && ok "旧的坏仓库地址 ${OLD_URL} 已消失" \
                      || bad "旧地址 ${OLD_URL} 还在（${N_OLDURL} 处）—— 二维码没换干净"

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

  # --- [.170 继承] 改名修复的 ELF 层字符串指纹 ---
  N_T=$(grep -ac -F "key GLOB 'TITLE_[0-9]*'" "$ELF")
  [ "$N_T" -ge 1 ] && ok "改名修复指纹「key GLOB 'TITLE_[0-9]*'」在（${N_T} 处）" \
                   || bad "改名修复指纹不在 —— .170 的修复丢了"
  N_OLDW=$(grep -ac -F "key IN ('TITLE','TITLE_01')" "$ELF")
  [ "$N_OLDW" -eq 0 ] && ok "旧写法 key IN ('TITLE','TITLE_01') 残留 0" \
                      || bad "旧写法还在（${N_OLDW} 处）"

  # --- [.171 新增] 捐赠码内联 + 仓库地址进了 ELF ---
  N_ELF_DATAURI=$(grep -ac -F 'data:image/png;base64' "$ELF")
  [ "$N_ELF_DATAURI" -ge 1 ] && ok "内联的捐赠码图片进了 ELF（data:image/png;base64）" \
                             || bad "ELF 里没有 base64 PNG —— 收款码图片没编进去"
  grep -aqF "$REPO_URL" "$ELF" && ok "本 fork 仓库地址进了 ELF（${REPO_URL}）" \
                               || bad "本 fork 仓库地址不在 ELF 里"
  N_ELF_OLD=$(grep -ac -F "$OLD_URL" "$ELF")
  [ "$N_ELF_OLD" -eq 0 ] && ok "旧的坏仓库地址不在 ELF 里" \
                         || bad "旧地址还在 ELF 里（${N_ELF_OLD} 处）"

  # --- byte-exact embed check ---
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
