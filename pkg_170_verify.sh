#!/bin/bash
# Verify NayePlmTools_0.5.2.170 build artifacts (both tile variants).
#
# [CN-FORK] 本脚本**继承** pkg_169_verify.sh 的全部检查，再加 .170 专属断言。
# 每轮出包都基于上一版加断言，不从零写 —— 老断言能守住的老问题别让它回流。
#
# .170 是**纯后端 C 改动**（src/tile_db.c 的 appinfo 匹配式），前端产物不变 ——
# 所以 .169 那组前端文案指纹**原样保留**（它们仍必须全绿，证明前端没被这轮碰坏），
# 再补 .170 的**ELF 层**指纹（后端修复真的编进去了）。
#
# Checks performed:
#   1. Both ELFs exist, non-trivial size, report md5 + byte size.
#   2. Embedded title carries correct version AND real commit hash (bedea87) — not DEV.
#   3. No leftover [[TITLE_PLACEHOLDER]] and no 0.5.2.169 residual.
#   4. Byte-exact: embedded web blob == frontend/dist/index.html.
#   5. [.169 继承] 排序入口搬迁前端指纹（旧口径消失 / 新口径在 / icon-btn+arrow-up-down 在）。
#   6. [.170 新增] 改名修复的 ELF 层字符串指纹：
#        · `key GLOB 'TITLE_[0-9]*'` 必须在（出现 2 次 = UPDATE + 计数 SELECT）；
#        · `concept_name_[0-9]*` 必须在（concept 侧同修）；
#        · 旧写法 `key IN ('TITLE','TITLE_01')` **必须消失**（残留 0）。
#      这三条是「appinfo 漏写语言槽」在产物里唯一可读的形状 —— ELF 已 strip，
#      结构断言（比如「UPDATE 语句的 WHERE 子句」）做不了，只能靠 SQL 字面量指纹。
#      ⚠ 别拿函数名认代码（会被 gc-sections / strip 去掉），只能认字符串字面量。
#
# Usage (WSL — needs grep/cmp from coreutils):
#   bash pkg_170_verify.sh
set -uo pipefail

VER=0.5.2.170
COMMIT=bedea87
PREV=0.5.2.169
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

# --- [.169 继承] 前端文案指纹：先验 dist，dist 错了后面 ELF 的字节比对必然一起错 ----------
echo "=== dist 文案指纹（.169 排序入口搬迁，本轮继承守门）==="
N_OLD=$(grep -o '右上角还有' "$DIST" | wc -l | tr -d ' ')
[ "$N_OLD" -eq 0 ] && ok "旧口径「右上角还有」已消失（磁贴右上角不再说 ⇅ 排序）" \
                    || bad "旧口径「右上角还有」还在（${N_OLD} 处）"
for S in '排序按钮在板块标题栏右上角' '所有磁贴共用一个'; do
  N=$(grep -o "$S" "$DIST" | wc -l | tr -d ' ')
  [ "$N" -ge 1 ] && ok "新口径「${S}」在（${N} 处）" \
                 || bad "新口径「${S}」不在"
done
N_ICON=$(grep -o 'icon-btn' "$DIST" | wc -l | tr -d ' ')
[ "$N_ICON" -ge 1 ] && ok "icon-btn 在产物里（${N_ICON} 处）" \
                    || bad "icon-btn 不在产物里 —— 标题栏那颗排序按钮没编进去"
grep -q 'arrow-up-down' "$DIST" && ok 'ArrowUpDown 图标在产物里' \
                                || bad 'arrow-up-down 不在产物里'

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

  # --- [.170 新增] 改名修复的 ELF 层字符串指纹 ---
  N_T=$(grep -ac -F "key GLOB 'TITLE_[0-9]*'" "$ELF")
  [ "$N_T" -ge 1 ] && ok "改名修复指纹「key GLOB 'TITLE_[0-9]*'」在（${N_T} 处 = UPDATE+SELECT）" \
                   || bad "改名修复指纹不在 —— appinfo 匹配式还是老写法，修复没编进包"
  N_C=$(grep -ac -F "concept_name_[0-9]*" "$ELF")
  [ "$N_C" -ge 1 ] && ok "concept 侧修复指纹「concept_name_[0-9]*」在（${N_C} 处）" \
                   || bad "concept 侧修复指纹不在"
  N_OLDW=$(grep -ac -F "key IN ('TITLE','TITLE_01')" "$ELF")
  [ "$N_OLDW" -eq 0 ] && ok "旧写法 key IN ('TITLE','TITLE_01') 残留 0" \
                      || bad "旧写法还在（${N_OLDW} 处）—— 修复没生效"
  # TITLE_ID 保护：产物里必须能见到排除它的 GLOB 字符类（[0-9]），不是裸 TITLE_
  grep -aqF "TITLE_[0-9]*" "$ELF" && ok "GLOB 字符类 [0-9] 在（TITLE_ID 已排除）" \
                                  || bad "[0-9] 字符类不在 —— 有把 TITLE_ID 写坏的风险"

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
