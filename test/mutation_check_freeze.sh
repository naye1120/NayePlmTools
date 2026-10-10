#!/bin/bash
# [CN-FORK] v0.5.2.152 变异对照（四个变异，都必须被抓住）。
# [CN-FORK] v0.5.2.154 又加三个（gate / snap / runblock），共七个。
#
# 跑法：bash test/mutation_check_freeze.sh            （EXIT 0 = 全部都被抓住）
#       bash test/mutation_check_freeze.sh order      （只跑顺序那个）
#       bash test/mutation_check_freeze.sh handle     （只跑「凭据没交出去」那个）
#       bash test/mutation_check_freeze.sh kind       （只跑「机制代号没交出去」那个）
#       bash test/mutation_check_freeze.sh verdict    （只跑「验证不可用就回落」那个）
#       bash test/mutation_check_freeze.sh gate       （只跑「远程不比本地新还去拉」那个）
#       bash test/mutation_check_freeze.sh snap       （只跑「退出时不快照」那个）
#       bash test/mutation_check_freeze.sh runblock   （只跑「运行中也让推拉」那个）
# 不接进 run_host_tests.sh：它要重编七遍（与 mutation_check_media.sh 同一理由）。
#
# ── 变异 ①「order」：把 freeze 挪到 ga_do_inner **之后**（先拉后冻）──────────
# 这个修复的全部价值就在**顺序**上（用户口径：暂停游戏 → 拉取存档）。而顺序恰恰是
# 「测试照样绿、行为已经错」的重灾区：桩被调到了、日志里那几行都在、调用次数也对，
# 一个只断言「冻结过一次 / 放行过一次」的用例在**错误顺序**下照样全绿。
# 所以变异必须做在**位置**上。期望：恰好 2 条红（顺序那两条）。
#
# ── 变异 ②「handle」：冻结后不把凭据交上去（= `.150` 真机首轮那个 bug 的等价形态）
# `.150` 真机首轮：「恢复失败」的下一秒就是「已退出」。真根因是恢复代码**又解析了
# 一次** app_id，而应用被挂起后 `sceLncUtilGetAppIdOfRunningBigApp()` 返回
# 0xffffffff —— 解析必然失败、ResumeApp 根本没发出去、游戏一直冻着。
# `.152` 换成 SIGSTOP 之后凭据变成 pid，但「当场重找」一样是灾难：游戏重启过一次
# 就会拿到新进程，而被冻住的那个再也没人放。
# 宿主上没法造「解析失败」（桩直接收凭据），所以用「冻结时没往上传」做等价变异。
# 期望：恰好 1 条红（「放行必须用冻结时报的凭据」）。
#
# ── 变异 ③「kind」：不把机制代号交上去（= 拿错一套 API 去放行）─────────────
# `.152` 起有两套冻结机制（SIGSTOP / LncUtil），放行必须知道冻的时候用的是哪套：
# 用 `ResumeApp` 去解一个被 SIGSTOP 停住的进程 = 它永远不会被放行。
# 期望：恰好 1 条红（「放行该收到冻结时报的机制代号」）。
#
# ── 变异 ④「verdict」：验证不可用时错误地 return -1（= `.152` 真机那个 bug）──
# `.152` 真机日志：「读不到 pid=582 的进程状态，进程级冻结跳过」→ 下一秒就
# 「已暂停（LncUtil 挂起）」。也就是说**主方案在发 SIGSTOP 之前就放弃了**，
# 回落 LncUtil，游戏跳回主屏还切不回来（用户口径：「远程拉取成功后，并没有将
# 游戏拉到前台」）。判定策略被抽成纯函数 `ga_freeze_verdict()` 就是为了这一格，
# 所以这个变异必须被抓住 —— 它等价于「把 `.152` 的 bug 原样放回去」。
# 期望：恰好 3 条红（判定表那条 + 两条「不许回落」的策略断言）。
#
# ── 变异 ⑤「gate」：拉取前不再比新旧（= 用旧存档盖掉新进度 + 刚推上去的又拉回来）──
# `.154` 用户口径：「如果本地存档比远程存档要更新，则不拉取」。这条判定的全部价值
# 就是**跳过那一次拉取**，而「跳过」在真机上是个看不出来的结果 —— 面板上就是一次
# 没动静的拉取，没报错。所以必须用「本地那份的字节一个都没变」来证，而不是看返回码。
# `.162` 用户又加一句「本地和远程一模一样就别拉」，判定由此变成**两条出口**
# （有 src_mtime 走严格更新；没有则退回 `.154` 那条兜底）。
# ⚠ 变异必须**两条一起掐**：只掐一条时，另一条仍然会跳过 —— 于是被改坏的那条路径
#   一个字节都没被测到（端到端全绿），而这恰恰是「测试看着有效、其实盖不住」的形状。
# 期望：纯函数 4 条 + 端到端 3 条 = 7 条红，其中判据命中 6 条。
#
# ── 变异 ⑥「snap」：退出时不生成快照步骤（= 功能静默消失）──────────────────
# `.154` 用户口径：「每次游戏结束后，在本地快照存一份存档；定时的功能不要了」——
# 触发点整个从定时守护搬到 `game_auto_plan` 的 GA_ACT_SNAP。把它删掉，面板上
# **一点异常都看不出来**（快照列表只是不再增长），所以这条变异必须被抓住。
# 期望：`game_auto_plan` 那一段里所有提到「快照」的断言全红。
#
# ── 变异 ⑦「runblock」：不拦运行中的手动推拉（= 游戏正写盘时去打包）────────
# `.154` 用户口径：「游戏进行中，拉取和推送存档的动作不让做」。删掉那道闸门，
# 用户点按钮会拿到一个**看起来成功**的结果，而盘上那份可能是撕裂的半截存档。
# 期望：推 / 拉各 2 条（返回码 + 提示文案）+ 「不该留下开始日志」1 条 = 恰好 5 条红。
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
        -DGA_HOST \
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

# 共用的「删掉一整行」注入器。$1=输出路径 $2=要删掉的那一行（含缩进与换行）
inject_drop_line() {
    python3 - "$1" "$2" <<'PY'
import sys, pathlib
src = pathlib.Path('src/game_auto.c').read_text(encoding='utf-8')
anchor = sys.argv[2]
assert src.count(anchor) == 1, '变异锚点没找到或不止一处: %r' % anchor
out = src.replace(anchor, '', 1)
assert out != src and anchor not in out, 'mutation failed'
pathlib.Path(sys.argv[1]).write_text(out, encoding='utf-8')
PY
}

bad=0

if [ "$WANT" = "both" ] || [ "$WANT" = "order" ]; then
    W=$(mktemp -d)
    python3 - "$W/game_auto.c" <<'PY'
import sys, pathlib
src = pathlib.Path('src/game_auto.c').read_text(encoding='utf-8')
FREEZE = ('    int frozen = (freeze_game && act == GA_ACT_PULL)\n'
          '                     ? ga_freeze_for_pull(tid, &frozen_kind, &frozen_handle) : 0;')
INNER = '    ga_do_inner(tid, act, only_uid, have ? &cfg : NULL, res);'
assert src.count(FREEZE) == 1, '变异锚点（freeze 那两行）没找到或不止一处'
assert src.count(INNER) == 1, '变异锚点（ga_do_inner 行）没找到或不止一处'
out = src.replace(FREEZE + '\n', '', 1)
out = out.replace(INNER, INNER + '\n' + FREEZE, 1)
assert out != src and out.count(FREEZE) == 1, 'mutation failed'
pathlib.Path(sys.argv[1]).write_text(out, encoding='utf-8')
print('  变异已注入：freeze 挪到 ga_do_inner **之后**（先拉后冻）')
PY
    run_case order "$W/game_auto.c" 2 3 '冻结必须在\|第一行' || bad=1
    rm -rf "$W"
fi

if [ "$WANT" = "both" ] || [ "$WANT" = "handle" ]; then
    W=$(mktemp -d)
    # 策略层把凭据往上传的那一行 —— 去掉它 = 「冻住了，但没人记住冻的是谁」。
    inject_drop_line "$W/game_auto.c" '    if (handle_out) *handle_out = handle;
'
    echo "  变异已注入：冻结后不把凭据交出去（= 放行时拿不到句柄）"
    run_case handle "$W/game_auto.c" 1 1 '放行必须用冻结时报的凭据' || bad=1
    rm -rf "$W"
fi

if [ "$WANT" = "both" ] || [ "$WANT" = "kind" ]; then
    W=$(mktemp -d)
    inject_drop_line "$W/game_auto.c" '    if (kind_out) *kind_out = kind;
'
    echo "  变异已注入：不把机制代号交出去（= 拿错一套 API 去放行）"
    run_case kind "$W/game_auto.c" 1 1 '放行该收到冻结时报的机制代号' || bad=1
    rm -rf "$W"
fi

if [ "$WANT" = "both" ] || [ "$WANT" = "verdict" ]; then
    W=$(mktemp -d)
    # 「验证不可用」那一格返回 -1 = 放弃 SIGSTOP、回落 LncUtil。正是 `.152` 真机那个 bug。
    python3 - "$W/game_auto.c" <<'PY'
import sys, pathlib
src = pathlib.Path('src/game_auto.c').read_text(encoding='utf-8')
OLD = '    if (!have_ctl || !ctl_adv) return 0;'
NEW = '    if (!have_ctl || !ctl_adv) return -1;'
assert src.count(OLD) == 1, '变异锚点（verdict 第一行）没找到或不止一处'
out = src.replace(OLD, NEW, 1)
assert out != src and NEW in out, 'mutation failed'
pathlib.Path(sys.argv[1]).write_text(out, encoding='utf-8')
print('  变异已注入：验证不可用时 return -1（= 放弃 SIGSTOP、回落 LncUtil）')
PY
    run_case verdict "$W/game_auto.c" 3 3 '16 种组合\|不许.*回落' || bad=1
    rm -rf "$W"
fi

if [ "$WANT" = "both" ] || [ "$WANT" = "gate" ]; then
    W=$(mktemp -d)
    # 掐掉拉取前的**两条**「不拉」出口 = 每次拉取都照跑：既会用旧盖新，
    # 也会把刚推上去的那份原样拉回来。
    # [CN-FORK] v0.5.2.162 判定从一条出口变成两条（src_mtime 那条 + 老包的兜底那条）。
    # 只掐其中一条的另一条仍然会跳过，红数会少、抓不住变异 —— 所以两条一起掐。
    python3 - "$W/game_auto.c" <<'PY'
import sys, pathlib
src = pathlib.Path('src/game_auto.c').read_text(encoding='utf-8')
OLD = ('    if (remote_src_mtime > 0) return remote_src_mtime > local_mtime;\n'
       '    /* 说不清远程那份的来源（老包没有 src_mtime）→ 只能拿落盘时刻比，\n'
       '     * 那条只用来防「用旧盖新」，不足以断定「一模一样」。 */\n'
       '    if (local_mtime > remote_mtime) return 0;  /* 本地更新 → 不拿旧的盖新的 */\n')
NEW = ('    if (0) return remote_src_mtime > local_mtime;\n'
       '    if (0 && local_mtime > remote_mtime) return 0;\n')
assert src.count(OLD) == 1, '变异锚点（ga_pull_needed 的两条「不拉」出口）没找到或不止一处'
out = src.replace(OLD, NEW, 1)
assert out != src and NEW in out, 'mutation failed'
pathlib.Path(sys.argv[1]).write_text(out, encoding='utf-8')
print('  变异已注入：拉取前不再比新旧（= 旧盖新 + 刚推上去的又原样拉回来）')
PY
    run_case gate "$W/game_auto.c" 6 7 '不该拉\|不该再\|已跳过拉取\|不该被原样' || bad=1
    rm -rf "$W"
fi

if [ "$WANT" = "both" ] || [ "$WANT" = "snap" ]; then
    W=$(mktemp -d)
    # 删掉退出时的快照步骤 = 「每次游戏结束后在本地快照存一份」这条需求静默失效。
    python3 - "$W/game_auto.c" <<'PY'
import sys, pathlib
src = pathlib.Path('src/game_auto.c').read_text(encoding='utf-8')
SNAP = ('    if (p && prev_snap) {\n'
        '        out[n].act = GA_ACT_SNAP;\n'
        '        snprintf(out[n].tid, sizeof(out[n].tid), "%s", p);\n'
        '        n++;\n'
        '    }\n')
assert src.count(SNAP) == 1, '变异锚点（GA_ACT_SNAP 那一段）没找到或不止一处'
out = src.replace(SNAP, '', 1)
assert out != src and SNAP not in out, 'mutation failed'
pathlib.Path(sys.argv[1]).write_text(out, encoding='utf-8')
print('  变异已注入：退出时不生成快照步骤（= 本地快照功能静默消失）')
PY
    run_case snap "$W/game_auto.c" 6 6 '快照\|退出第二步' || bad=1
    rm -rf "$W"
fi

if [ "$WANT" = "both" ] || [ "$WANT" = "runblock" ]; then
    W=$(mktemp -d)
    # 拆掉「游戏运行中不许手动推拉」那道闸门。
    python3 - "$W/game_auto.c" <<'PY'
import sys, pathlib
src = pathlib.Path('src/game_auto.c').read_text(encoding='utf-8')
BLOCK = '''    {
        char run[32];
        run[0] = 0;
        g_probe(run, sizeof(run));
        if (run[0] && !strcmp(run, tid)) {
            if (msg) snprintf(msg, msgsz,
                              "「%s」正在运行，请先退出游戏再%s存档（运行中操作可能丢失进度）",
                              tid, act == GA_ACT_PUSH ? "推送" : "拉取");
            return -1;
        }
    }
'''
assert src.count(BLOCK) == 1, '变异锚点（运行中拒拦那一段）没找到或不止一处'
out = src.replace(BLOCK, '', 1)
assert out != src and BLOCK not in out, 'mutation failed'
pathlib.Path(sys.argv[1]).write_text(out, encoding='utf-8')
print('  变异已注入：不拦运行中的手动推拉（= 游戏正写盘时去打包）')
PY
    run_case runblock "$W/game_auto.c" 5 5 '必须被拦\|要说清是因为在运行\|不该.*留下「开始」日志' || bad=1
    rm -rf "$W"
fi

echo
if [ "$bad" -eq 0 ]; then
    echo "MUTATION OK：变异都被抓住了 —— 顺序 / 凭据 / 机制代号 / 判定策略 /"
    echo "            拉取前比新旧 / 退出时快照 / 运行中拒拦 七条断言都不是虚的"
    exit 0
fi
echo "MUTATION BAD：有变异没被抓住（守卫不可信）"
exit 1
