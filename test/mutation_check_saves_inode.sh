#!/bin/bash
# [CN-FORK] v0.5.2.149 变异对照：把 gs_swap_dir 改回「rename 整个目录」的旧写法，
# test/game_saves_test.c 里那几条「目录 inode 不许变」的断言必须变红。
#
# 为什么值得单列一条脚本（这是本工程第三次为「测试有效性」单独写对照）：
#   这条守卫防的是**真机上已经发生过的事故** —— 「游戏启动时拉取提示成功、但游戏里
#   还是老存档」。根因就是 `rename(target, old)` 换掉了存档目录的 inode，而那一刻
#   游戏已经把目录绑住了。宿主上如果只断言「内容对不对」，旧写法照样全绿（内容确实
#   换对了，只是换的是另一个 inode），所以必须**专门盯 st_ino**，而这条断言的有效性
#   只能靠「把修复掐掉看它红不红」来证明。
#
# 跑法：bash test/mutation_check_saves_inode.sh
# 不接进 run_host_tests.sh：它要重编一整遍（和 mutation_check_media.sh 同一理由）。
set -uo pipefail
cd "$(dirname "$0")/.."

W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT

python3 - "$W/game_saves.c" <<'PY'
import sys, pathlib
src = pathlib.Path('src/game_saves.c').read_text(encoding='utf-8')

# 旧实现（.148 及以前）：无条件把 target 整个 rename 走
OLD = '''static int gs_swap_dir(const char *parent, const char *target, const char *src,
                       char *err, size_t errsz) {
    char old[800];
    snprintf(old, sizeof(old), "%s/.old_%u_%u", parent, (unsigned)getpid(), (unsigned)time(NULL));
    int had_old = 0;
    struct stat ost;
    if (lstat(target, &ost) == 0) {
        gs_rmtree(old);
        if (rename(target, old) == 0) had_old = 1;
    }
    if (rename(src, target) != 0) {
        if (had_old) rename(old, target);
        if (err && errsz) snprintf(err, errsz, "\\u65e0\\u6cd5\\u5199\\u5165\\u5b58\\u6863\\u76ee\\u5f55\\uff08%s\\uff09", strerror(errno));
        return -1;
    }
    if (had_old) gs_rmtree(old);
    return 0;
}
'''

a = src.index('/* \u628a from \u91cc\u7684\u6240\u6709\u6761\u76ee')   # gs_move_entries 的注释开头
b = src.index('/* \u2500\u2500 \u88c1\u526a\u4e0e\u9884\u7b97')       # 下一节
out = src[:a] + OLD + '\n' + src[b:]
assert out != src and 'gs_move_entries' not in out, 'mutation failed'
pathlib.Path(sys.argv[1]).write_text(out, encoding='utf-8')
print('  变异已注入：gs_swap_dir 回到 `rename(target, old)` 旧写法（gs_move_entries 一并移除）')
PY

echo
echo "=== 现造夹具（与 run_host_tests.sh 同一步）==="
# ⚠ 不造夹具的话，game_saves 的用例会因为「存档根都找不到」而**全部**变红 ——
# 那是假阳性，会让人以为变异成功了。所以这里必须先造，然后只盯**该红的那几条**。
SAVES_ROOT=/tmp/pldmgr_saves_test
rm -rf "$SAVES_ROOT"
python3 test/make_game_fixture.py "$SAVES_ROOT" || { echo "  FIXTURE FAILED"; exit 1; }
echo "  夹具就绪：$SAVES_ROOT"

echo
echo "=== 用变异后的 game_saves.c 编 test/game_saves_test.c ==="
if ! clang -Wall -Wextra -O0 -Isrc/psaccounts -Iinclude -Isrc/psaccounts/third_party \
    -DGS_HOME='"/tmp/pldmgr_saves_test/home"' \
    -DGAME_SAVES_AUTO_PATH='"/tmp/pldmgr_saves_test/saves_auto.json"' \
    -DGAME_SAVES_PACK_DIR='"/tmp/pldmgr_saves_test/packs"' \
    -DGAME_SAVES_SNAP_DIR='"/tmp/pldmgr_saves_test/snap"' \
    "$W/game_saves.c" \
    src/game_job.c src/json_helpers.c src/utils.c src/inflate.c src/zip_io.c \
    src/psaccounts/third_party/cJSON.c test/stub_archive_global.c test/stub_sr_stat.c \
    test/game_saves_test.c -o "$W/t" -lpthread -lm; then
    echo "  变异体编译失败（那也算红，但先看看是不是脚本坏了）"; exit 1
fi

OUT=$("$W/t" 2>&1)
echo "$OUT" | grep -aE "FAIL|checks," | head -20

# 只认这两条：目录 inode 被换掉（上传那条 + 还原那条）。
# 别的 FAIL 一律当脚本坏了 —— 变异只该动 gs_swap_dir 一个函数。
# -a：输出里是中文，不加这个 grep 会把流当二进制、-c 只数到 1 次。
n_ino=$(echo "$OUT" | grep -ac "换掉了" || true)
n_all=$(echo "$OUT" | grep -ac "FAIL" || true)
echo
echo "  红的总数 = $n_all，其中「目录本身被换掉了」= $n_ino"
if [ "${n_ino:-0}" -ge 2 ] && [ "${n_all:-0}" -le 4 ]; then
    echo "MUTATION OK：旧写法让「目录 inode 不许变」的断言红了两条（上传 + 还原），"
    echo "             且没有别的连带红 —— 守卫真的咬得住"
    exit 0
fi
echo "MUTATION BAD：期望恰好那两条红（实得 ino=$n_ino / 总 $n_all）—— 守卫不可信"
exit 1
