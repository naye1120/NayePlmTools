#!/bin/bash
# Verify NayePlmTools_0.5.2.175 build artifacts (both tile variants).
#
# [CN-FORK] 本脚本**继承** pkg_173_verify.sh 的全部检查，再加 .174/.175 专属断言。
# 每轮出包都基于上一版加断言，不从零写 —— 老断言能守住的老问题别让它回流。
#
# ⚠ 版本号说明：这批改动**本打算出 .174**，但 .174 这个序号当时已被占用/作废，
#   所以实际出的是 **.175**（代码内容与 CHANGELOG 里写的 `.174` 完全相同）。
#
# .174/.175 是**四件事**（用户四条口径）：
#   A. 删掉「静默窗口」（GS_SNAP_QUIET_SEC）与 `.172` 的延迟重试队列 ——
#      本地快照只剩两个时机：① 游戏退出（与「往远程推送」同一时刻）② 手动「立即备份」；
#   B. 「快照活动」上屏 —— 新增 GET /game_save_snap_log + 界面「最近活动」子段
#      （负向结果：内容没变 / 没得拍 / 拍失败，以前只在日志里）；
#   C. 快照**成功**时发一条主机系统通知（pldmgr_notify「本地快照完成」）；
#   D. 「参考与致谢」补全（5 条 → 22 条，四组）。
# 所以 .170 的后端 SQL 指纹、.171 的前端指纹、.172 的两条下载路由、.173 的改签指纹
# 必须**继续全绿**。
#
# Checks performed:
#   1. Both ELFs exist, non-trivial size, report md5 + byte size.
#   2. Embedded title carries correct version AND real commit hash — not DEV.
#   3. No leftover [[TITLE_PLACEHOLDER]] and no 0.5.2.173 residual.
#   4. Byte-exact: embedded web blob == frontend/dist/index.html.
#   5. [.170 继承] 改名修复的 ELF 层 SQL 字面量指纹。
#   6. [.171 继承] 捐赠码内联 + 本 fork 仓库地址的产物层指纹。
#   7. [.172 继承] 两条下载路由的产物层指纹。
#   8. [.173 继承] 改签 / 布局 / 删路由的产物层指纹。
#   9. [.174 新增] 静默窗口+重试删干净 + 快照活动上屏 + 致谢补全：
#        ELF 必须有 `/game_save_snap_log`（新路由）、`游戏退出，检查本地快照`（事件串）、
#        `内容与最近一份相同，未新增`（负向事件）；
#        **必须没有** `本地快照已排队重试`（重试日志删干净）、`静默窗口还没过`（旧措辞删干净）；
#        dist 必须有「最近活动」+ `snap_events_title` + 「与推送远程同一时刻」；
#        **必须没有** `排队重试` / `存档刚被写过`；
#        致谢的六个新条目（ShadowMountPlus / offact / LibAtrac9 / stb / fan_target /
#        ps5-fan-control）必须在 dist 里。
#
# ⚠ 三条**踩过的坑**，别把断言写回去：
#   (a) 「存档转换」四个字**必然**还在 dist / ELF 各 1 处 —— `.173` 关于页更新日志
#       里故意写了「删掉「存档转换」：它想干的事…」。所以不能拿它当「没删干净」的判据，
#       要判的是**功能残留**（路由名 / 旧键名 / 旧 UI 文案）；反过来还应该**正向**断言
#       那句「删掉「存档转换」」在（否则用户看不出删了）。
#   (b) clang 会对共享前缀的字面量做**后缀合并**（SHF_MERGE / SHF_STRINGS）：
#       共享的前缀在 `.rodata` 里只存一份，于是整串可能 grep 不到、独有词在。
#       想认「这行日志编进去了」就认**完整格式串**。
#   (c) `.174` 起**别拿 `等下一轮` / `静默窗口` 当「删干净了」的判据** —— 用户口径要求
#       把「静默窗口」「等下一轮」这两个词写进关于页更新日志（说明删了什么），所以它们
#       在 dist/ELF 里**本来就该有**。要判的是**代码侧的串**：`本地快照已排队重试`、
#       `静默窗口还没过`、`排队重试`、`存档刚被写过`。
#
# Usage (WSL — needs grep/cmp from coreutils):
#   bash pkg_175_verify.sh
set -uo pipefail

VER=0.5.2.175
COMMIT=6a187dd
PREV=0.5.2.174
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

# --- [.174 新增] dist 层：快照活动上屏 + 时机文案 + 致谢补全 ----------
echo "=== dist 指纹（.174 快照活动 / 时机 / 致谢）==="
for S in '最近活动' 'snap_events_title' '与推送远程同一时刻' '主机系统通知'; do
  N=$(grep -o "$S" "$DIST" | wc -l | tr -d ' ')
  [ "$N" -ge 1 ] && ok "文案「${S}」在 dist（${N} 处）" || bad "文案「${S}」不在 dist"
done
# 删干净的判据（⚠ 见文件头 (c)：别用「等下一轮」「静默窗口」当判据）
for S in '排队重试' '存档刚被写过'; do
  N=$(grep -o "$S" "$DIST" | wc -l | tr -d ' ')
  [ "$N" -eq 0 ] && ok "已删干净的「${S}」在 dist 残留 0" \
                 || bad "「${S}」还在 dist（${N} 处）—— 没删干净"
done
# 致谢补全：新增的六条里挑最不可能被别处引用的项目名
for S in 'drakmor/ShadowMountPlus' 'ps5-payload-dev/offact' 'Thealexbarney/LibAtrac9' \
         'nothings/stb' 'drakmor/fan_target' 'owendswang/ps5-fan-control'; do
  N=$(grep -o "$S" "$DIST" | wc -l | tr -d ' ')
  [ "$N" -ge 1 ] && ok "致谢条目「${S}」在 dist（${N} 处）" || bad "致谢条目「${S}」不在 dist"
done

# --- [.173 继承] dist 层：改签文案在、删掉的文案不在 ----------
echo "=== dist 指纹（.173 兼容导入搬位 + 删「存档转换」+ 改签文案）==="
for S in '兼容导入（Garlic）' '都会落到你上面选定的用户名下' '已改签并导入到' '导入到哪个用户'; do
  N=$(grep -o "$S" "$DIST" | wc -l | tr -d ' ')
  [ "$N" -ge 1 ] && ok "文案「${S}」在 dist（${N} 处）" || bad "文案「${S}」不在 dist"
done
# ⚠ 「存档转换」四个字**不能**列进来（见文件头 ⚠(a)）。这里判的是**功能残留**。
for S in '从其它用户转入' '档案转换' 'convert_title' 'game_save_convert'; do
  N=$(grep -o "$S" "$DIST" | wc -l | tr -d ' ')
  [ "$N" -eq 0 ] && ok "已删干净的「${S}」在 dist 残留 0" \
                 || bad "「${S}」还在 dist（${N} 处）—— 没删干净"
done
N_CONVNOTE=$(grep -o '删掉「存档转换」' "$DIST" | wc -l | tr -d ' ')
[ "$N_CONVNOTE" -eq 1 ] && ok "关于页写了「删掉「存档转换」」（1 处，仅此一处）" \
                        || bad "「删掉「存档转换」」在 dist 出现 ${N_CONVNOTE} 处（期望 1）"

# --- [.172 继承] dist 层：两条下载路由 + 两句文案 ----------
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

  # --- [.172 继承] 两条下载路由的 ELF 层指纹 ---
  for S in '/game_save_snap_download' '/game_save_remote_download'; do
    grep -aqF "$S" "$ELF" && ok "下载路由「${S}」进了 ELF" \
                          || bad "下载路由「${S}」不在 ELF 里 —— 后端路由表没编进去"
  done
  grep -aqF '找不到这份快照' "$ELF" && ok "快照打包的失败文案进了 ELF（新函数已编入）" \
                                   || bad "「找不到这份快照」不在 ELF 里 —— game_saves_snap_pack 没编进去"

  # --- [.173 继承] 改签 + 记账不落地 + 删路由 的 ELF 层指纹 ---
  grep -aqF '改签 uid=' "$ELF" && ok "导入改签的日志串进了 ELF（.173 的改签已编入）" \
                              || bad "「改签 uid=」不在 ELF 里 —— 改签的日志没编进去"
  grep -aqF 'manifest.json' "$ELF" && ok "跳过 garlic 记账的判据（manifest.json）在 ELF 里" \
                                   || bad "「manifest.json」不在 ELF 里 —— 记账不落地那条没了"
  # 快照结果那行：认**完整格式串**（不受字面量后缀合并影响，见文件头 ⚠(b)），
  # 再补一个独有词 `没拍` 证明新措辞真的编进去了。
  grep -aqF '已退出 · 本地快照：%s（%s）' "$ELF" && ok "快照结果那行的格式串进了 ELF" \
                                                || bad "「已退出 · 本地快照：%s（%s）」不在 ELF 里 —— 日志没编进去"
  grep -aqF '没拍' "$ELF" && ok "快照结果的新措辞进了 ELF（没拍）" \
                          || bad "「没拍」不在 ELF 里 —— 日志措辞没编进去"
  N_CONV=$(grep -ac -F '/game_save_convert' "$ELF")
  [ "$N_CONV" -eq 0 ] && ok "已删的「/game_save_convert」在 ELF 残留 0" \
                      || bad "「/game_save_convert」还在 ELF 里（${N_CONV} 处）—— 后端没删干净"
  N_CN=$(grep -ac -F '存档转换' "$ELF")
  if [ "$N_CN" -eq 1 ] && grep -aqF '删掉「存档转换」' "$ELF"; then
    ok "ELF 里「存档转换」只剩关于页那句「删掉「存档转换」」（1 处）"
  else
    bad "「存档转换」在 ELF 出现 ${N_CN} 处（期望 1，且是关于页那句）—— 有功能残留"
  fi

  # --- [.174 新增] 快照活动上屏 + 静默窗口/重试删干净 的 ELF 层指纹 ---
  grep -aqF '/game_save_snap_log' "$ELF" && ok "快照活动路由「/game_save_snap_log」进了 ELF" \
                                         || bad "「/game_save_snap_log」不在 ELF 里 —— 后端没编进去"
  grep -aqF '游戏退出，检查本地快照' "$ELF" && ok "退出快照的 INFO 事件串进了 ELF" \
                                           || bad "「游戏退出，检查本地快照」不在 ELF 里 —— 事件环没接上"
  grep -aqF '内容与最近一份相同，未新增' "$ELF" && ok "快照的负向事件串进了 ELF（内容没变）" \
                                              || bad "「内容与最近一份相同，未新增」不在 ELF 里 —— 负向结果上不了屏"
  grep -aqF '本地快照完成' "$ELF" && ok "快照成功的主机通知文案进了 ELF（pldmgr_notify）" \
                                 || bad "「本地快照完成」不在 ELF 里 —— 通知没编进去"
  # 删干净：重试日志与静默窗口措辞都不许再出现（⚠ 别用「静默窗口」当判据，见文件头 (c)）
  N_RETRY=$(grep -ac -F '本地快照已排队重试' "$ELF")
  [ "$N_RETRY" -eq 0 ] && ok "重试日志「本地快照已排队重试」在 ELF 残留 0" \
                       || bad "「本地快照已排队重试」还在 ELF 里（${N_RETRY} 处）—— 重试队列没删干净"
  N_QUIET=$(grep -ac -F '静默窗口还没过' "$ELF")
  [ "$N_QUIET" -eq 0 ] && ok "旧措辞「静默窗口还没过」在 ELF 残留 0" \
                       || bad "「静默窗口还没过」还在 ELF 里（${N_QUIET} 处）—— 静默窗口没删干净"
  # 致谢补全：ELF 里也得有（网页 blob 就是 ELF 的一部分，但单独断言更直白）
  for S in 'drakmor/ShadowMountPlus' 'Thealexbarney/LibAtrac9'; do
    grep -aqF "$S" "$ELF" && ok "致谢条目「${S}」进了 ELF" \
                          || bad "致谢条目「${S}」不在 ELF 里"
  done

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
