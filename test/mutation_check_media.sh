#!/bin/bash
# [CN-FORK] v0.5.2.124 test/mutation_check_media.sh
#
# 变异测试：证明 media_scan 里那几条**关键**用例不是自证空转。
#
# 为什么单独搞一个脚本 —— 这一课是用两轮真机返工换来的：
#   · .123 的封面用例把「icon_path 所在目录」与「按 id 推出来的目录」摆成同一个目录，
#     于是它把**错的假设**测成了绿的（真机上封面照样没有）。
#   · 用例只要不断言「画面内容」，给一张纯黑图也能过。
# 所以：改完封面帧那段 / 打包取包那段，跑一遍这个，确认把关键那几行掐掉之后
# 测试**真的会红**。不接进 run_host_tests.sh（要跑好几遍完整编译，偏慢），出包前手动跑。
#
# 用法：bash test/mutation_check_media.sh
set -u
cd "$(dirname "$0")/.."

ROOT=/tmp/pldmgr_media_test
MUT=/tmp/pldmgr_mut
INC="-Isrc/psaccounts -Iinclude -Isrc/psaccounts/third_party"
DEFS="-DGAME_MEDIA_ROOT=\"$ROOT/av_contents\" -DGAME_MEDIA_DB_PATH=\"$ROOT/av_content.db\" \
 -DGAME_MEDIA_THUMB_DIR=\"$ROOT/thumbs\" -DGAME_MEDIA_PACK_DIR=\"$ROOT/pack\" \
 -DGAME_MEDIA_USER_PREFIX=\"$ROOT\""
# game_media.c 与 game_job.c **都在**基表里：变异哪个就把哪个换成变异体。
# （game_media.c 平时在 run_host_tests.sh 里是「被变异体顶替」的角色，
#  而变异 game_job.c 时又必须带上未变异的 game_media.c —— 放基表里最省事。）
SRCS="test/media_scan_test.c src/game_media.c src/game_job.c src/zip_io.c src/inflate.c \
 src/json_helpers.c src/utils.c src/psaccounts/third_party/cJSON.c \
 src/psaccounts/third_party/stb_impl.c"

# run_mutant <名字> <源文件> <sed 表达式> <期望最少红几条>
# 每个变异体：源码补丁 → **夹具与缩略图缓存重建** → 编译 → 跑。
# 缓存不重建的话上一个变异体留下的缩略图会让下一个「假绿」（踩过：st_mtime 撞上就串图）。
run_mutant() {
    name="$1"; srcfile="$2"; sedexpr="$3"; want="$4"
    sed "$sedexpr" "$srcfile" > "$MUT/m_$name.c"
    if ! grep -aq "MUT_$name" "$MUT/m_$name.c"; then
        echo "  [$name] 补丁没打上 —— $srcfile 的锚点变了，请更新本脚本"; return 1
    fi
    rm -rf "$ROOT"
    python3 test/make_media_fixture.py "$ROOT" >/dev/null || { echo "  [$name] 夹具失败"; return 1; }
    # 被变异的那个文件要**顶掉**它在 SRCS 里的原件，否则重复定义（game_job.c 就在 SRCS 里）。
    srcs=""; replaced=0
    for s in $SRCS; do
        if [ "$s" = "$srcfile" ]; then srcs="$srcs $MUT/m_$name.c"; replaced=1
        else srcs="$srcs $s"; fi
    done
    [ "$replaced" = 1 ] || srcs="$srcs $MUT/m_$name.c"
    clang -w -O0 $INC $DEFS $srcs -o "$MUT/t_$name" -lpthread -lm || {
        echo "  [$name] 编译失败"; return 1; }
    got=$("$MUT/t_$name" 2>&1 | grep -ac 'FAIL L')
    if [ "$got" -ge "$want" ]; then
        echo "  [$name] ok — 掐掉后 $got 条断言变红（期望 >=$want）"
        return 0
    fi
    echo "  [$name] ✗ 只红了 $got 条（期望 >=$want）—— 这条用例是空转的，必须修"
    return 1
}

rm -rf "$MUT"; mkdir -p "$MUT"
fail=0
echo "=== 变异测试：封面帧 ==="

# ① 掐掉「认 icon_path 所在目录」，退回 .123 的「按 id 推同构目录」。
#    期望：C_VIDC 取不到真画面 → 落到占位图 → 左红右蓝两条断言红。
run_mutant iconpath src/game_media.c \
  's#^\( *\)if (md_scan_cover_dir(d1, base, blen, abs, absz, st) == 0) return 0;#\1/*MUT_iconpath*/;#' \
  2 || fail=1

# ② 掐掉魔数校验，退回 .123 的「只看扩展名」。
#    期望：挑中「后缀像 JPEG 的假图」→ 解码失败 → -2/404（不是占位图）
#    → 「应回占位图」+「诊断日志」共 4 条红。
run_mutant magic src/game_media.c \
  's#^\( *\)if (!md_magic_image(cand)) continue;.*#\1/*MUT_magic*/;#' \
  4 || fail=1

echo "=== 变异测试：打包取包门禁 ==="

# ③ 把「任务必须已成功收尾」这道门拆掉（gj_ready 恒真）。
#    期望：打包途中 game_media_pack_open 不再回 -2 —— 要么取到半截包（大小前后不一致），
#    要么成品还没 rename 出来直接 -3；两条都算「门禁被击穿」，断言变红。
run_mutant nolgate src/game_job.c \
  's#^    int ok = (match \&\& g_job.done \&\& g_job.ok) ? 1 : 0;#    int ok = match; /*MUT_nolgate*/#' \
  1 || fail=1

echo "=== 变异测试：压缩包顶层文件夹 ==="

# ④ 把条目名退回 .124 的平铺布局（content_path 去掉 /av_contents/），
#    也就是「游戏编码-游戏名称/」这一层整个不存在。
#    期望：media_scan_test.c 里那组结构断言全红 —— 顶层文件夹出现 0 次（应 10）、
#    旧布局 photo/… 与 video/… 又冒出来、文件夹下直接跟文件名那条也不再匹配、
#    同秒那一对的两条名字也对不上。
#    用户口径：「压缩包下级文件夹是 游戏编码-游戏名称，这个文件夹里面直接就是截图和视频」。
run_mutant noplain src/game_media.c \
  's#^\( *\)md_zname_unique(e\[n\]\.zname, sizeof(e\[n\]\.zname), folder, dl, e, n);#\1snprintf(e[n].zname, sizeof(e[n].zname), "%s", paths.v[i] + 13); /*MUT_noplain*/#' \
  4 || fail=1

# ⑤ 掐掉去重（同一秒的两张直接同名）。
#    期望：夹具里那对同秒文件的断言红 —— 干净名字那条变 1 次（少了一条）、
#    -2 那条变 0 次；而**没有去重的话 zip 里会有两条同名条目**，Windows 解压互相覆盖。
#    用户口径：「这个文件夹里面直接就是截图和视频」—— 摊平就必须承担撞名的风险。
run_mutant nodedup src/game_media.c \
  's#^\( *\)md_zname_unique(e\[n\]\.zname, sizeof(e\[n\]\.zname), folder, dl, e, n);#\1snprintf(e[n].zname, sizeof(e[n].zname), "%s/%s", folder, dl); /*MUT_nodedup*/#' \
  1 || fail=1

echo "==== 变异测试: $([ $fail -eq 0 ] && echo PASS || echo FAIL) ===="
exit $fail
