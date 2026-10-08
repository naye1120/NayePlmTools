#!/bin/bash
# [CN-FORK] v0.5.2.150 变异对照（两个变异，都必须被抓住）。
#
# 跑法：bash test/mutation_check_freeze.sh            （EXIT 0 = 两个都被抓住）
#       bash test/mutation_check_freeze.sh order      （只跑顺序那个）
#       bash test/mutation_check_freeze.sh appid      （只跑 app_id 那个）
# 不接进 run_host_tests.sh：它要重编两遍（与 mutation_check_media.sh 同一理由）。
#
# ── 变异 ①「order」：把 freeze 挪到 ga_do_inner **之后**（先拉后冻）──────────
# 这个修复的全部价值就在**顺序**上（用户口径：暂停游戏 → 拉取存档）。而顺序恰恰是
# 「测试照样绿、行为已经错」的重灾区：桩被调到了、日志里那几行都在、调用次数也对，
# 一个只断言「挂起过一次 / 恢复过一次」的用例在**错误顺序**下照样全绿。
# 所以变异必须做在**位置**上。期望：恰好 2 条红（顺序那两条）。
#
# ── 变异 ②「appid」：挂起时不把 app_id 交出去（= 真机首轮那个 bug 的等价形态）
# `.150` 真机首轮：「恢复失败」的下一秒就是「已退出」。真根因是恢复代码**又解析了
# 一次** app_id，而应用被挂起后 `sceLncUtilGetAppIdOfRunningBigApp()` 返回
# 0xffffffff —— 解析必然失败、ResumeApp 根本没发出去、游戏一直冻着。
# 宿主上没法造「挂起后解析失败」（桩直接收 app_id），所以用「挂起时没记下来」
# 做等价变异。期望：恰好 1 条红（「恢复必须用挂起时那个 app_id」）。
set -uo pipefail
cd "$(dirname "$0")/.."

WANT="${1:-both}"

# 宿主版 libsmb2（与 run_host_tests.sh 同一个缓存位置；别放 /tmp，tmpfs 会被清）
SR_SMB_DIR=/root/libsmb2-host/libsmb2-6.2
SR_SMB_A=""
[ -f "$SR_SMB_DIR/build/lib/libsmb2.a" ] && SR_SMB_A="$SR_SMB_DIR/build/lib/libsmb2.a"
if [ -z "$SR_SMB_A" ]; then
    echo "  宿主版 libsmb2 不在（$SR_SMB_DIR）—— 先跑一次完整 test/run_host_tests.sh"
    exit 1
fi

# 变异 + 编译 + 跑。$1=模式 $2=变异后源码路径 $3=期望的红条数下限 $4=红条数上限 $5=grep 判据
run_case() {
    mode="$1"; src="$2"; want_min="$3"; want_max="$4"; pat="$5"
    echo
    echo "════════ 变异 $mode ════════"
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
        "$src" \
        src/save_remote.c src/curl_init.c src/game_saves.c src/game_job.c \
        src/json_helpers.c src/utils.c src/inflate.c src/zip_io.c \
        src/psaccounts/third_party/cJSON.c \
        test/stub_servers.c test/stub_archive_global.c test/game_auto_test.c \
        -o /tmp/_ga_mut_t "$SR_SMB_A" -lpthread -lm -lcurl; then
        echo "  变异体编译失败（那也算红，但先看看是不是脚本坏了）"; return 1
    fi
    rm -rf /tmp/pldmgr_ga_mut
    OUT=$(/tmp/_ga_mut_t 2>&1)
    echo "$OUT" | grep -aE "FAIL|checks," | head -20
    # -a：输出里是中文，不加这个 grep 会把流当二进制、-c 只数到 1 次。
    n_hit=$(echo "$OUT" | grep -ac "$pat" || true)
    n_all=$(echo "$OUT" | grep -ac "FAIL" || true)
    echo "  红的总数 = $n_all，其中判据命中 = $n_hit"
    if [ "${n_hit:-0}" -ge "$want_min" ] && [ "${n_all:-0}" -le "$want_max" ]; then
        echo "  → MUTATION OK"
        return 0
    fi
    echo "  → MUTATION BAD：期望判据命中 >=$want_min、总红 <=$want_max（实得 $n_hit / $n_all）"
    return 1
}

bad=0

if [ "$WANT" = "both" ] || [ "$WANT" = "order" ]; then
    W=$(mktemp -d)
    python3 - "$W/game_auto.c" <<'PY'
import sys, pathlib
src = pathlib.Path('src/game_auto.c').read_text(encoding='utf-8')
FREEZE = '    int frozen = (freeze_game && act == GA_ACT_PULL) ? ga_freeze_for_pull(tid, &frozen_id) : 0;'
INNER = '    ga_do_inner(tid, act, only_uid, have ? &cfg : NULL, res);'
assert src.count(FREEZE) == 1, '变异锚点（freeze 行）没找到或不止一处'
assert src.count(INNER) == 1, '变异锚点（ga_do_inner 行）没找到或不止一处'
out = src.replace(FREEZE + '\n', '', 1)
out = out.replace(INNER, INNER + '\n' + FREEZE, 1)
assert out != src and out.count(FREEZE) == 1, 'mutation failed'
pathlib.Path(sys.argv[1]).write_text(out, encoding='utf-8')
print('  变异已注入：freeze 挪到 ga_do_inner **之后**（先拉后冻）')
PY
    run_case order "$W/game_auto.c" 2 3 '挂起必须在\|第一行' || bad=1
    rm -rf "$W"
fi

if [ "$WANT" = "both" ] || [ "$WANT" = "appid" ]; then
    W=$(mktemp -d)
    python3 - "$W/game_auto.c" <<'PY'
import sys, pathlib
src = pathlib.Path('src/game_auto.c').read_text(encoding='utf-8')
# 挂起成功后把 app_id 交出去那一行 —— 去掉它 = 「挂起时没记住 app_id」，
# 也就是真机首轮那个 bug 的等价形态。
KEEP = '    if (app_id_out) *app_id_out = app_id;\n'
assert src.count(KEEP) == 1, '变异锚点（*app_id_out = app_id 行）没找到或不止一处'
out = src.replace(KEEP, '', 1)
assert out != src and KEEP not in out, 'mutation failed'
pathlib.Path(sys.argv[1]).write_text(out, encoding='utf-8')
print('  变异已注入：挂起时不交出 app_id（= 恢复时拿不到句柄）')
PY
    run_case appid "$W/game_auto.c" 1 2 '恢复必须用挂起时那个 app_id' || bad=1
    rm -rf "$W"
fi

echo
if [ "$bad" -eq 0 ]; then
    echo "MUTATION OK：变异都被抓住了 —— 顺序断言与 app_id 断言都不是虚的"
    exit 0
fi
echo "MUTATION BAD：有变异没被抓住（守卫不可信）"
exit 1
