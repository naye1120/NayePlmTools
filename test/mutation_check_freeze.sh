#!/bin/bash
# [CN-FORK] v0.5.2.150 变异对照：把「拉取前挂起游戏」挪到拉取**之后**，
# test/game_auto_test.c 里那两条**顺序**断言必须变红。
#
# 为什么要单独一条（本工程第四次为「测试有效性」写对照）：
#   这个修复的全部价值就在**顺序**上 —— 用户口径是
#   「游戏启动 → 监测到启动 → 暂停游戏 → 拉取存档 → 拉取成功 → 恢复游戏」，
#   而「先拉后冻」等于什么都没修（游戏照样在冻结之前把旧存档读进内存）。
#
#   顺序恰恰是「测试照样绿、行为已经错」的重灾区：桩被调到了、日志里那几行都在、
#   调用次数也对，一个只断言「挂起过一次 / 恢复过一次」的用例在**错误顺序**下
#   照样全绿。所以变异必须做在**位置**上（把 freeze 挪到 ga_do_inner 之后），
#   验证顺序断言真的会咬人 —— 不做这一步，就没法排除那两条断言是虚的。
#
# 跑法：bash test/mutation_check_freeze.sh        （EXIT 0 = 变异被抓住）
# 不接进 run_host_tests.sh：它要重编一整遍（与 mutation_check_media.sh 同一理由）。
set -uo pipefail
cd "$(dirname "$0")/.."

W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT

python3 - "$W/game_auto.c" <<'PY'
import sys, pathlib
src = pathlib.Path('src/game_auto.c').read_text(encoding='utf-8')

FREEZE = '    int frozen = (freeze_game && act == GA_ACT_PULL) ? ga_freeze_for_pull(tid) : 0;'
INNER = '    ga_do_inner(tid, act, only_uid, have ? &cfg : NULL, res);'

assert src.count(FREEZE) == 1, '变异锚点（freeze 行）没找到或不止一处'
assert src.count(INNER) == 1, '变异锚点（ga_do_inner 行）没找到或不止一处'

# 先拉后冻 —— 最容易写错、也最不容易被测出来的那种顺序错误。
out = src.replace(FREEZE + '\n', '', 1)
out = out.replace(INNER, INNER + '\n' + FREEZE, 1)
assert out != src and out.count(FREEZE) == 1, 'mutation failed'
pathlib.Path(sys.argv[1]).write_text(out, encoding='utf-8')
print('  变异已注入：freeze 挪到 ga_do_inner **之后**（先拉后冻）')
PY

# 宿主版 libsmb2（与 run_host_tests.sh 同一个缓存位置；别放 /tmp，tmpfs 会被清）
SR_SMB_DIR=/root/libsmb2-host/libsmb2-6.2
SR_SMB_A=""
[ -f "$SR_SMB_DIR/build/lib/libsmb2.a" ] && SR_SMB_A="$SR_SMB_DIR/build/lib/libsmb2.a"
if [ -z "$SR_SMB_A" ]; then
    echo "  宿主版 libsmb2 不在（$SR_SMB_DIR）—— 先跑一次完整 test/run_host_tests.sh"
    exit 1
fi

echo
echo "=== 用变异后的 game_auto.c 编 test/game_auto_test.c ==="
if ! clang -Wall -Wextra -O0 -Iinclude -Isrc/psaccounts/third_party -Itest \
    -I/usr/include -Ithird_party/libsmb2/include \
    -DGA_NO_LNCUTIL \
    -DROOT_DIR='"/tmp/pldmgr_ga_mut"' \
    -DAUTO_PATH='"/tmp/pldmgr_ga_mut/auto.json"' \
    -DGS_HOME='"/tmp/pldmgr_ga_mut/home"' \
    -DGAME_SAVES_AUTO_PATH='"/tmp/pldmgr_ga_mut/auto.json"' \
    -DGAME_SAVES_PACK_DIR='"/tmp/pldmgr_ga_mut/packs"' \
    -DGAME_SAVES_SNAP_DIR='"/tmp/pldmgr_ga_mut/snap"' \
    -DGS_SNAP_QUIET_SEC=0 \
    "$W/game_auto.c" \
    src/save_remote.c src/curl_init.c src/game_saves.c src/game_job.c \
    src/json_helpers.c src/utils.c src/inflate.c src/zip_io.c \
    src/psaccounts/third_party/cJSON.c \
    test/stub_servers.c test/stub_archive_global.c test/game_auto_test.c \
    -o "$W/t" "$SR_SMB_A" -lpthread -lm -lcurl; then
    echo "  变异体编译失败（那也算红，但先看看是不是脚本坏了）"; exit 1
fi

rm -rf /tmp/pldmgr_ga_mut
OUT=$("$W/t" 2>&1)
echo "$OUT" | grep -aE "FAIL|checks," | head -20

# 只认那两条顺序断言：①「已挂起」该是第一行 ② 挂起必须在「拉取 开始」之前。
# 别的 FAIL 一律当脚本坏了 —— 变异只挪了一行，不该有连带。
# -a：输出里是中文，不加这个 grep 会把流当二进制、-c 只数到 1 次。
n_ord=$(echo "$OUT" | grep -ac "挂起必须在\|第一行" || true)
n_all=$(echo "$OUT" | grep -ac "FAIL" || true)
echo
echo "  红的总数 = $n_all，其中顺序类 = $n_ord"
if [ "${n_ord:-0}" -ge 2 ] && [ "${n_all:-0}" -le 3 ]; then
    echo "MUTATION OK：「先拉后冻」被两条顺序断言抓住，且没有别的连带红 ——"
    echo "             顺序断言真的咬得住（不是虚的）"
    exit 0
fi
echo "MUTATION BAD：期望恰好那两条红（实得 顺序=$n_ord / 总 $n_all）—— 守卫不可信"
exit 1
