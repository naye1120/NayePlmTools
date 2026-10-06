#!/bin/bash
# [CN-FORK] v0.5.2.127 test/mutation_check_game_mgr.sh
#
# 变异测试：证明 game_mgr_test 里「来源类别」那几条断言不是自证空转。
#
# 为什么值得单独验 —— 这一课是用户拿两张截图换来的：
#   · 详情弹窗给一款 .ffpfsc 镜像游戏和一款裸目录游戏都打了「已安装 PKG」，
#     连带物理路径、已挂载、can_manage_source（删除按钮消失）一起错。
#   · 根因是拿 a.d_src（**从哪枚举到的**）当来源类型，而 ShadowMount 的游戏同样
#     注册在 app.db 里，d_src 也是 installed。
# 新断言如果在主机上是空转的（比如夹具压根没造那两个 lnk 文件），改回旧实现它照样绿，
# 就等于什么都没测。所以把旧实现放回来跑一遍，确认**真的会红**。
#
# 不接进 run_host_tests.sh（要跑好几遍完整编译，偏慢），出包前手动跑。
#
# 用法：bash test/mutation_check_game_mgr.sh
set -u
cd "$(dirname "$0")/.."

ROOT=/tmp/pldmgr_gm_test
MUT=/tmp/pldmgr_mut_gm
INC="-Iinclude -Isrc/psaccounts/third_party"
# 与 run_host_tests.sh 的 game_mgr 那一段**必须逐字一致**，否则变异体跑的不是同一套夹具。
DEFS="-DGAME_MGR_APPDB_PATH=\"/tmp/pldmgr_gm_test/app.db\" \
 -DGAME_MGR_APP_BASE=\"/tmp/pldmgr_gm_test/user_app\" \
 -DGAME_MGR_SYSTEM_EX=\"/tmp/pldmgr_gm_test/system_ex\""
SRCS="test/game_mgr_test.c src/game_title.c src/json_helpers.c src/utils.c \
 src/psaccounts/third_party/stb_impl.c"

# run_mutant <名字> <sed 表达式> <期望最少红几条>
# 每个变异体：源码补丁 → 夹具重建 → 编译 → 跑。
# 夹具每次重建：lnk 文件是**上一次**留下的残留会让「这次该是 pkg 的那款」变成 image，
# 断言就会时好时坏 —— 那正是最难查的一类假绿。
run_mutant() {
    name="$1"; sedexpr="$2"; want="$3"
    sed "$sedexpr" src/game_mgr.c > "$MUT/m_$name.c"
    if ! grep -aq "MUT_$name" "$MUT/m_$name.c"; then
        echo "  [$name] 补丁没打上 —— src/game_mgr.c 的锚点变了，请更新本脚本"; return 1
    fi
    rm -rf "$ROOT"
    python3 test/make_appdb_fixture.py "$ROOT" >/dev/null || { echo "  [$name] 夹具失败"; return 1; }
    clang -w -O0 -Isrc/psaccounts $INC $DEFS \
        -include test/host_mnt_compat.h \
        "$MUT/m_$name.c" $SRCS -o "$MUT/t_$name" -lpthread -lm || {
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
echo "=== 变异测试：来源类别 ==="

# ① 把来源类别退回「看 a.d_src」的老实现（v0.5.2.126 及以前）。
#    期望：夹具里 src=installed 的那款镜像游戏被误判成 pkg ——
#    source_type / image_type / path / runtime_path / installed_pkg / can_manage_source
#    至少 6 条红（真机上就是这 6 条一起错的）。
run_mutant srcdsrc \
  's#^\( *\)const char \*source_type = gm_class_of_fmt(a\.d_fmt);#\1const char *source_type = !strcmp(a.d_src, "installed") ? "pkg" : (!strcmp(a.d_fmt, "folder") ? "folder" : "image"); /*MUT_srcdsrc*/#' \
  6 || fail=1

# ② 把 image_type 退回「只在四个镜像扩展名里挑」的写法 —— 判据没错，但
#    它必须跟着 source_type 走；若哪天有人把它改成只看 fmt，pkg/folder 游戏
#    就会带出一个空的/错的 image_type。
run_mutant imgtype \
  's#^\( *\)const char \*image_type = !strcmp(source_type, "image") ? a\.d_fmt : "";#\1const char *image_type = a.d_fmt; /*MUT_imgtype*/#' \
  2 || fail=1

# ③ 掐掉「mount_img.lnk 命中就用镜像文件当物理源」，让镜像游戏掉进 mount.lnk 那支。
#    期望：镜像游戏的物理路径变成运行时挂载点（/mnt/shadowmnt/…_abcd1234）而不是
#    那个 .ffpfsc 文件 —— 详情与 game_mgr_source 各红一条。
#    真机上是同一处错：物理路径回 /user/app/PPSA17599 而不是
#    /mnt/ext1/PPSA17599三十三号.ffpfsc。
run_mutant pathimg \
  's#^\( *\)if (gm_read_lnk(f, line, sizeof(line)) \&\& gm_ext_is_image(line)) {#\1if (0) { /*MUT_pathimg*/#' \
  2 || fail=1

echo "=== 变异测试：已挂载判据 ==="

# ④ 把 mounted 退回「runtime_path 存在即已挂载」的老实现。
#    期望：夹具里那款**造了 /system_ex 挂载点**的裸目录游戏不再被判成已挂载
#    （真机上 PPSA01773 就是这样被误标成「已挂载」的：裸目录游戏的源目录一直都在）。
run_mutant mounted \
  's#^\( *\)int mounted = !installed_pkg \&\& gm_is_data_mounted(tid);#\1int mounted = runtime[0] != 0; /*MUT_mounted*/#' \
  1 || fail=1

echo "==== 变异测试: $([ $fail -eq 0 ] && echo PASS || echo FAIL) ===="
exit $fail
