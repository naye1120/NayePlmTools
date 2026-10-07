#!/bin/bash
# [CN-FORK] Host-side unit tests (WSL clang, no PS5 SDK needed).
#   bash test/run_host_tests.sh
set -u
cd "$(dirname "$0")/.."

fail=0
# run_one <name> <source> [extra cflags...]
run_one() {
    name="$1"; src="$2"; shift 2
    echo "=== $name ==="
    # 末尾 -lpthread -lm：media_scan 要起打包 worker 线程，stb_image_write 要 libm。
    if ! clang -Wall -Wextra -O0 -Isrc/psaccounts "$@" "$src" "test/${name}_test.c" -o "/tmp/${name}_test" -lpthread -lm; then
        echo "  BUILD FAILED"; fail=1; return
    fi
    "/tmp/${name}_test" || fail=1
}

run_one slot_resolve   src/psaccounts/npsignin.c
run_one offact_genid   src/psaccounts/offact.c
# safety.c really creates SAFETY_DIR, so point it at a temp dir on the host.
# 注意：SAFETY_DIR 若与测试**可执行文件同名**（都是 /tmp/safety_origin_test），
# safety_init 一 mkdir 就把 ld 之后的输出路径占成目录了，第二次跑必报
# `Is a directory`。所以这里换成一个不同名的目录。
rm -rf /tmp/safety_origin_dir
run_one safety_origin  src/psaccounts/safety.c -DSAFETY_DIR='"/tmp/safety_origin_dir"'

# [CN-FORK] v0.5.2.115 游戏管理 → 多媒体管理（src/game_media.c）
# 这条测试比另外三个重：夹具（一个列序照抄真机的 av_content.db + 它引用的真实文件）
# 要 python3 现造，产出的 zip 也要 python3 复验 —— 缺 python3 就整段跳过，不半跑。
MEDIA_ROOT=/tmp/pldmgr_media_test
if command -v python3 >/dev/null 2>&1; then
    echo "=== media_fixture ==="
    rm -rf "$MEDIA_ROOT"
    if ! python3 test/make_media_fixture.py "$MEDIA_ROOT"; then
        echo "  FIXTURE FAILED"; fail=1
    else
        run_one media_scan src/game_media.c \
            -Iinclude -Isrc/psaccounts/third_party \
            -DGAME_MEDIA_ROOT='"/tmp/pldmgr_media_test/av_contents"' \
            -DGAME_MEDIA_DB_PATH='"/tmp/pldmgr_media_test/av_content.db"' \
            -DGAME_MEDIA_THUMB_DIR='"/tmp/pldmgr_media_test/thumbs"' \
            -DGAME_MEDIA_PACK_DIR='"/tmp/pldmgr_media_test/pack"' \
            -DGAME_MEDIA_USER_PREFIX='"/tmp/pldmgr_media_test"' \
            src/game_job.c src/zip_io.c src/inflate.c src/json_helpers.c src/utils.c \
            src/psaccounts/third_party/cJSON.c src/psaccounts/third_party/stb_impl.c
        # log_server.c 不进来：它的头要 microhttpd.h（主机上没有）。
        # pldmgr_log 由 test/media_scan_test.c 自己提供（默认静音）。

        if [ -f "$MEDIA_ROOT/list.json" ]; then
            echo "=== media_verify json ==="
            python3 test/media_verify.py json "$MEDIA_ROOT/list.json" 5 || fail=1

            # v0.5.2.126 起 zip 模式多两个参数：tid + 游戏名称。
            # 条目名 = <tid>-<游戏名>/<游戏名>-<时间戳>.<ext>，校验脚本会**独立算一遍**再比。
            # 游戏名与 test/media_scan_test.c 里 game_title_get_name 桩钉的那个一致。
            echo "=== media_verify zip (subset 2) ==="
            python3 test/media_verify.py zip "$MEDIA_ROOT/pack_subset.zip" "$MEDIA_ROOT" \
                PPSA99008 宇宙机器人 \
                "/av_contents/photo/NPXS40087/PPSA99008/18c/20261005_165454_00474347.png" \
                "/av_contents/photo/NPXS40087/PPSA99008/18c/20261005_170000_00474348.jxr" \
                || fail=1

            echo "=== media_verify zip (all 5) ==="
            python3 test/media_verify.py zip "$MEDIA_ROOT/pack_all.zip" "$MEDIA_ROOT" \
                PPSA99008 宇宙机器人 \
                "/av_contents/photo/NPXS40087/PPSA99008/18c/20261005_165454_00474347.png" \
                "/av_contents/photo/NPXS40087/PPSA99008/18c/20261005_170000_00474348.jxr" \
                "/av_contents/photo/NPXS40087/PPSA99008/18c/20261005_180000_00474349.png" \
                "/av_contents/video/NPXS40087/PPSA99008/18c/20261006_101112_001.mp4" \
                "/av_contents/video/NPXS40087/PPSA99008/18c/20261006_102233_002.mp4" \
                || fail=1

            # [CN-FORK] v0.5.2.122 单条 1.5 MiB 的包：专门压 zip_write 的
            # 「256 KB 分块 × N 次 + 条目收尾 CRC 回填」那条路（用户报的
            # 「打包卡在 0 B / 58.7 MB」正落在这里）。CRC 由 zipfile 逐条复算，
            # 分块累积只要错一位这里就红。
            echo "=== media_verify zip (big, 跨 256KB 分块) ==="
            python3 test/media_verify.py zip "$MEDIA_ROOT/pack_big.zip" "$MEDIA_ROOT" \
                PPSA99008 宇宙机器人 \
                "/av_contents/video/NPXS40087/PPSA99008/18c/20261006_101112_001.mp4" \
                || fail=1

            # v0.5.2.126 同一秒连拍两张：摊平后条目名必然撞，必须去重成 …-2。
            # ids 的顺序**必须与包内条目顺序一致**（谁拿干净名字取决于顺序），
            # 这里是 DB 行序 = 010 在前、011 在后。
            echo "=== media_verify zip (同一秒去重) ==="
            python3 test/media_verify.py zip "$MEDIA_ROOT/pack_dup.zip" "$MEDIA_ROOT" \
                PPSA99012 同秒游戏 \
                "/av_contents/photo/NPXS40087/PPSA99012/18c/20261010_120000_010.png" \
                "/av_contents/photo/NPXS40087/PPSA99012/18c/20261010_120000_011.png" \
                || fail=1
        else
            echo "=== media_verify SKIPPED (list.json 没生成) ==="
            fail=1
        fi
    fi
else
    echo "=== media_scan SKIPPED: 需要 python3 做夹具与 zip 校验 ==="
fi

# [CN-FORK] v0.5.2.116 游戏管理 → 存档（src/game_saves.c）
# 夹具同样是 python3 现造（两个用户目录、两种槽形态、一个该被跳过的 .upl_），
# 缺 python3 就整段跳过，不半跑。
SAVES_ROOT=/tmp/pldmgr_saves_test
if command -v python3 >/dev/null 2>&1; then
    echo "=== game_saves_fixture ==="
    rm -rf "$SAVES_ROOT"
    if ! python3 test/make_game_fixture.py "$SAVES_ROOT"; then
        echo "  FIXTURE FAILED"; fail=1
    else
        # src/game_mgr.c 不进来：那是另一条测试（下方 game_mgr），它自带
        # test/host_mnt_compat.h 顶掉 getmntinfo。这里 game_mgr_sfo_text 仍是桩 ——
        # 桩成「读不到」正好逼出 gs_slot_name 的兜底分支，
        # ② 里断言的就是「读不到 SFO 就用子项名」。
        run_one game_saves src/game_saves.c \
            -Iinclude -Isrc/psaccounts/third_party \
            -DGS_HOME='"/tmp/pldmgr_saves_test/home"' \
            -DGAME_SAVES_AUTO_PATH='"/tmp/pldmgr_saves_test/saves_auto.json"' \
            -DGAME_SAVES_PACK_DIR='"/tmp/pldmgr_saves_test/packs"' \
            src/game_job.c src/json_helpers.c src/utils.c src/inflate.c src/zip_io.c \
            src/psaccounts/third_party/cJSON.c
    fi
else
    echo "=== game_saves SKIPPED: 需要 python3 做夹具 ==="
fi

# [CN-FORK] v0.5.2.117 游戏列表**静默变空**（build_game_json 漏清 want）→ src/game_mgr.c
# 这条是那三道门全都放行、真机才炸出来的那类 bug 的补漏：
#   ① 这个文件以前主机上编不过（getmntinfo 是 BSD 专有）→ 由 test/host_mnt_compat.h
#      顶成「永远 0 条挂载」，game_mgr.c 本身一个字不改；
#   ② app.db 主机上没有 → python3 现造一张最小 tbl_contentinfo 当夹具。
# 缺 python3 就整段跳过，不半跑。
GM_ROOT=/tmp/pldmgr_gm_test
if command -v python3 >/dev/null 2>&1; then
    echo "=== appdb_fixture ==="
    rm -rf "$GM_ROOT"
    if ! python3 test/make_appdb_fixture.py "$GM_ROOT"; then
        echo "  FIXTURE FAILED"; fail=1
    else
        # [CN-FORK] v0.5.2.127 除了 app.db，还要把 /user/app 与 /system_ex 两棵树指到
        # 夹具目录：来源类别（pkg / image / folder）的判据就是 /user/app/<TID>/ 下那两个
        # lnk 文件，mounted 的判据是 /system_ex/app/<TID>/sce_sys/param.json。
        # 不指过去的话，这条判定在主机上永远只走得到「没有 lnk → pkg」那一支 ——
        # 而真机炸的正是另外两支（ShadowMount 的游戏被当成已安装 PKG）。
        run_one game_mgr src/game_mgr.c \
            -Iinclude -Isrc/psaccounts/third_party \
            -include test/host_mnt_compat.h \
            -DGAME_MGR_APPDB_PATH='"/tmp/pldmgr_gm_test/app.db"' \
            -DGAME_MGR_APP_BASE='"/tmp/pldmgr_gm_test/user_app"' \
            -DGAME_MGR_SYSTEM_EX='"/tmp/pldmgr_gm_test/system_ex"' \
            src/game_title.c src/json_helpers.c src/utils.c \
            src/psaccounts/third_party/stb_impl.c
    fi
else
    echo "=== game_mgr SKIPPED: 需要 python3 做夹具 ==="
fi

# [CN-FORK] v0.5.2.132 改名要写游戏源 sce_sys/param.json（src/game_title.c）
# 这段是全仓库唯一会**写进用户游戏包**的逻辑，改坏 param.json 不可逆，所以单独测。
# 夹具是真机 FTP 拉下来的真实 param.json（CRLF + 2 空格美化格式，1/3/10 个语言键），
# 不是手写的最小样例 —— 手写样例测不出缩进被踩、CRLF 被改这类真问题。
# 夹具不在仓库里（真机数据，不入库），缺了就 SKIP，不半跑。
#
# 不用 run_one：这个测试用 #include 把 game_title.c / json_helpers.c 拉进同一翻译单元
# （这样才能碰到 static 的 tt_param_rename），并且运行时要收一个夹具目录参数。
PARAM_FIXTURE_DIR="${PARAM_FIXTURE_DIR:-$PWD/../_ftp_probe/evidence/recon}"
echo "=== param_rename ==="
if ls "$PARAM_FIXTURE_DIR"/*.json >/dev/null 2>&1; then
    if ! clang -Wall -Wextra -O0 -Iinclude -Isrc -Isrc/psaccounts \
        -Isrc/psaccounts/third_party -Itest \
        test/param_rename_test.c src/psaccounts/third_party/cJSON.c \
        -o /tmp/param_rename_test -lm; then
        echo "  BUILD FAILED"; fail=1
    else
        /tmp/param_rename_test "$PARAM_FIXTURE_DIR" || fail=1
    fi
else
    echo "=== param_rename SKIPPED: 夹具不在 $PARAM_FIXTURE_DIR ==="
fi

echo "==== overall: $([ $fail -eq 0 ] && echo PASS || echo FAIL) ===="
exit $fail
