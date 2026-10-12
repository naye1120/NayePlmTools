#!/bin/bash
# [CN-FORK] v0.5.2.173 变异对照（两个变异，都必须被抓住）。
#
# ── 变异 ①「resign」：导入时**不剥**别人的 uid 前缀（= 「改签」根本没做）──
# `.173` 用户口径：「导入的时候如果是其他用户的存档，直接改签成当前选定用户的。」
# 存档镜像是加密 PFS 镜像，读不出原主人，所以「改签」落地成两件事：**换落点**（由
# `save` 决定，本来就对）+ **把文件名归一**（剥掉别人留下的 `<uid>_` 前缀）。
# 这个变异把第二步掐掉 —— 于是盘上出现 `00000004_sdimg_RESIGN`，而主机要找的是
# `sdimg_RESIGN` → 「导入报成功、进游戏没存档」，与 `.165` 那个 bug 是同一个病。
# 期望：恰好 2 条红（「该落到选定用户下、名字剥成 sdimg_RESIGN」+「前缀没被剥掉」）。
# ⚠ 判据必须是**文件名**而不是返回码：落错名字时 rc 照样是 0、文件数照样是 1，
# 只看「导入成功了吗」这条用例在变异下全绿。
#
# ── 变异 ②「manifest」：把 garlic 自己的记账写进主机存档目录（= `.172` 的行为）──
# 真机取证：`/user/home/1a1d3627/savedata_prospero/PPSA21567/manifest.json`（367 B）
# 就是 `.172` 之前被原样写进去的 —— 那是**主机的**系统目录，不该出现第三方文件。
# 期望：恰好 2 条红（文件数变成 3 而不是 2 + 那个文件真的躺在存档目录里）。
# ⚠ 反向对照写在用例里（`weird_thing.dat` 必须照收）：这条守卫很容易被「改成
# 只收 sdimg_* 的白名单」这种修法顺手做坏，而白名单会把用户的存档悄悄丢掉。
#
# 跑法：bash test/mutation_check_resign.sh              （两个都跑）
#       bash test/mutation_check_resign.sh resign       （只跑 ①）
#       bash test/mutation_check_resign.sh manifest     （只跑 ②）
# 不接进 run_host_tests.sh：它要重编两遍（与 mutation_check_media.sh 同一理由）。
set -uo pipefail
cd "$(dirname "$0")/.."

ONLY="${1:-}"
bad=0

# 变异 + 编译 + 跑。$1=模式 $2=变异后源码路径 $3=期望的红条数 $4=判据 grep
run_case() {
    mode="$1"; src="$2"; want="$3"; pat="$4"
    echo
    echo "════════ 变异 $mode ════════"

    # ⚠ 每次都要**现造夹具**：不造的话「存档根都找不到」，用例会整片变红 ——
    # 那是假阳性，会让人以为变异成功了。这里只盯**该红的那几条**。
    SAVES_ROOT=/tmp/pldmgr_saves_test
    rm -rf "$SAVES_ROOT"
    python3 test/make_game_fixture.py "$SAVES_ROOT" >/dev/null || {
        echo "  FIXTURE FAILED"; return 1
    }

    if ! clang -Wall -Wextra -O0 -Isrc/psaccounts -Iinclude -Isrc/psaccounts/third_party \
        -DGS_HOME='"/tmp/pldmgr_saves_test/home"' \
        -DGAME_SAVES_AUTO_PATH='"/tmp/pldmgr_saves_test/saves_auto.json"' \
        -DGAME_SAVES_PACK_DIR='"/tmp/pldmgr_saves_test/packs"' \
        -DGAME_SAVES_SNAP_DIR='"/tmp/pldmgr_saves_test/snap"' \
        -DGS_SNAP_QUIET_SEC=0 \
        "$src" \
        src/game_job.c src/json_helpers.c src/utils.c src/inflate.c src/zip_io.c \
        src/psaccounts/third_party/cJSON.c test/stub_archive_global.c test/stub_sr_stat.c \
        test/game_saves_test.c -o /tmp/_resign_mut_t -lpthread -lm; then
        echo "  变异体编译失败（那也算红，但先看看是不是脚本坏了）"; return 1
    fi
    OUT=$(/tmp/_resign_mut_t 2>&1)
    echo "$OUT" | grep -aE "FAIL|checks," | head -20
    # -a：输出里是中文，不加这个 grep 会把流当二进制、-c 只数到 1 次。
    n_hit=$(echo "$OUT" | grep -ac "$pat" || true)
    n_all=$(echo "$OUT" | grep -ac "FAIL" || true)
    echo "  红的总数 = $n_all，其中判据命中 = $n_hit"
    if [ "${n_hit:-0}" -ge "$want" ] && [ "${n_all:-0}" -le "$want" ]; then
        echo "  → MUTATION OK"
        return 0
    fi
    echo "  → MUTATION BAD：期望判据命中 >=$want、总红 <=$want（实得 $n_hit / $n_all）"
    return 1
}

W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT

# ── 变异 ① resign：把「剥别人 uid 前缀」整块关掉 ────────────────────────────
if [ -z "$ONLY" ] || [ "$ONLY" = resign ]; then
    python3 - "$W/resign.c" <<'PY'
import sys, pathlib
src = pathlib.Path('src/game_saves.c').read_text(encoding='utf-8')
anchor = '    if (g && g->nuids > 0) {'
assert src.count(anchor) == 1, '锚点没找到或不止一处'
out = src.replace(anchor, '    if (0) {', 1)   # 结构不动，只把那一块短路掉
assert out != src, 'mutation failed'
pathlib.Path(sys.argv[1]).write_text(out, encoding='utf-8')
print('  变异已注入：gs_gx_name_for 不再剥别人的 uid 前缀')
PY
    run_case resign "$W/resign.c" 2 '该落到选定用户\|别人 uid 前缀没被剥掉' || bad=1
fi

# ── 变异 ② manifest：把 garlic 的 manifest.json 又写回去 ──────────────────
if [ -z "$ONLY" ] || [ "$ONLY" = manifest ]; then
    python3 - "$W/manifest.c" <<'PY'
import sys, pathlib
src = pathlib.Path('src/game_saves.c').read_text(encoding='utf-8')
anchor = '    if (strcmp(base, "manifest.json") == 0) { g->skipped++; return 0; }\n'
assert src.count(anchor) == 1, '锚点没找到或不止一处'
out = src.replace(anchor, '', 1)
assert out != src and anchor not in out, 'mutation failed'
pathlib.Path(sys.argv[1]).write_text(out, encoding='utf-8')
print('  变异已注入：garlic 的 manifest.json 又会被写进主机存档目录')
PY
    run_case manifest "$W/manifest.c" 2 'manifest.json 不该计数\|记账被写进了主机的存档目录' || bad=1
fi

echo
if [ "$bad" -eq 0 ]; then
    echo "全部变异都被抓住：⑭b 那组断言真的咬得住（改签 + 记账不落地）"
else
    echo "有变异没被抓住 —— ⑭b 那组断言不可信"
fi
exit $bad
