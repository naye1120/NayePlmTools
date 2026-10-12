#!/bin/bash
# Verify NayePlmTools_0.5.2.172 build artifacts (both tile variants).
#
# [CN-FORK] 本脚本**继承** pkg_171_verify.sh 的全部检查，再加 .172 专属断言。
# 每轮出包都基于上一版加断言，不从零写 —— 老断言能守住的老问题别让它回流。
#
# .172 是**存档模块**的改动（用户口径：「重点修复bug … 还有游戏关闭的时候并没有自动
# 存储本地快照」「所有存档列表删除按钮前边加一个下载按钮，并实现下载」），两条线：
#   A. 后端修「游戏退出不落本地快照」（`gs_snap_take` 的 20s 静默窗口 + 延迟重试）；
#   B. 新增两条 GET 下载路由（本地快照 / 远程存档）。
# 所以 .170 那组后端 SQL 指纹与 .171 的前端指纹必须**继续全绿**（证明没碰坏它们）。
#
# Checks performed:
#   1. Both ELFs exist, non-trivial size, report md5 + byte size.
#   2. Embedded title carries correct version AND real commit hash — not DEV.
#   3. No leftover [[TITLE_PLACEHOLDER]] and no 0.5.2.171 residual.
#   4. Byte-exact: embedded web blob == frontend/dist/index.html.
#   5. [.170 继承] 改名修复的 ELF 层 SQL 字面量指纹。
#   6. [.171 继承] 捐赠码内联 + 本 fork 仓库地址的产物层指纹。
#   7. [.172 新增] 两条下载路由与退出快照修复的产物层指纹：
#        · dist 里必须有 `/game_save_snap_download` 与 `/game_save_remote_download`
#          （前端 href 拼的就是这两个串，少一个那颗按钮就是 404）；
#        · dist 里必须有「下载快照」「下载远程存档」两句文案（i18n 兜底也走字面量）；
#        · 两个 ELF 里必须有这两条路由串（后端路由表在 ELF 里）；
#        · ELF 里必须有 `找不到这份快照`（快照打包的失败文案 = 新函数真的编进去了）；
#        · ELF 里必须有 `本地快照已排队重试`（`.172` 的退出快照重试真的在包里）。
#
# Usage (WSL — needs grep/cmp from coreutils):
#   bash pkg_172_verify.sh
set -uo pipefail

VER=0.5.2.172
COMMIT=89a8ccb
PREV=0.5.2.171
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

# --- [.172 新增] dist 层：两条下载路由 + 两句文案 ----------
echo "=== dist 指纹（.172 存档下载：两条路由 + 文案）==="
for S in '/game_save_snap_download' '/game_save_remote_download'; do
  N=$(grep -o "$S" "$DIST" | wc -l | tr -d ' ')
  [ "$N" -ge 1 ] && ok "下载路由「${S}」在 dist（${N} 处）" \
                 || bad "下载路由「${S}」不在 dist —— 那颗下载按钮会 404"
done
for S in '下载快照' '下载远程存档'; do
  N=$(grep -o "$S" "$DIST" | wc -l | tr -d ' ')
  [ "$N" -ge 1 ] && ok "文案「${S}」在 dist（${N} 处）" || bad "文案「${S}」不在 dist"
done

# --- [.171 继承] dist 层：捐赠码图片内联 + 仓库地址文案 ----------
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

  # --- [.171 继承] 捐赠码内联 + 仓库地址进了 ELF ---
  N_ELF_DATAURI=$(grep -ac -F 'data:image/png;base64' "$ELF")
  [ "$N_ELF_DATAURI" -ge 1 ] && ok "内联的捐赠码图片进了 ELF（data:image/png;base64）" \
                             || bad "ELF 里没有 base64 PNG —— 收款码图片没编进去"
  grep -aqF "$REPO_URL" "$ELF" && ok "本 fork 仓库地址进了 ELF（${REPO_URL}）" \
                               || bad "本 fork 仓库地址不在 ELF 里"
  N_ELF_OLD=$(grep -ac -F "$OLD_URL" "$ELF")
  [ "$N_ELF_OLD" -eq 0 ] && ok "旧的坏仓库地址不在 ELF 里" \
                         || bad "旧地址还在 ELF 里（${N_ELF_OLD} 处）"

  # --- [.172 新增] 两条下载路由 + 退出快照修复的 ELF 层指纹 ---
  for S in '/game_save_snap_download' '/game_save_remote_download'; do
    grep -aqF "$S" "$ELF" && ok "下载路由「${S}」进了 ELF" \
                          || bad "下载路由「${S}」不在 ELF 里 —— 后端路由表没编进去"
  done
  grep -aqF '找不到这份快照' "$ELF" && ok "快照打包的失败文案进了 ELF（新函数已编入）" \
                                   || bad "「找不到这份快照」不在 ELF 里 —— game_saves_snap_pack 没编进去"
  grep -aqF '本地快照已排队重试' "$ELF" && ok "退出快照重试的日志串进了 ELF（.172 修复已编入）" \
                                       || bad "「本地快照已排队重试」不在 ELF 里 —— .172 的重试修复丢了"

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
