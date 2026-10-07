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

# [CN-FORK] v0.5.2.138 静态守卫：POST 路由的「分发表」与「限长表」必须一致。
# 放在最前面 —— 它毫秒级，抓的是主机套件完全看不见的那类错：路由在
# gr_dispatch_post 里分发了、却漏登记 gr_post_limit，于是请求体根本不收、
# 客户端看到连接被直接关掉（`.137` 的真机原形）。mock 里没有这一层，
# 214 项前端回归照样全绿，所以只能靠静态检查。
echo "=== check_post_routes (静态守卫) ==="
bash tools/check_post_routes.sh || fail=1

# [CN-FORK] v0.5.2.141 静态守卫：不许把大块内存放栈上（真机连接线程只有 64KB 栈）。
# `.116`「改图标死机」与 `.141`「点立即备份死机」是同一个坑，两次都宿主全绿、
# 真机必炸 —— 所以既要有这道静态粗网，也要有 game_saves 里那条 32KB 小栈用例。
# 需要 PS5 SDK 的头（http_server.c 要 microhttpd.h）；没有就自己跳过。
echo "=== check_stack_frames (静态守卫) ==="
bash tools/check_stack_frames.sh || fail=1

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
# 夹具同样是 python3 现造，**按真机实测布局**（savedata_prospero/<TID>/sdimg_*，
# PS4 走 savedata/，多用户各一条）。缺 python3 就整段跳过，不半跑。
# ⚠ 这份夹具曾经造的是错的布局（<uid>/savedata/<TID>/<槽>），测试全绿而真机上
#   每个游戏的存档列表永远是空的 —— 夹具必须跟着真机走，不能跟着代码走。
SAVES_ROOT=/tmp/pldmgr_saves_test
if command -v python3 >/dev/null 2>&1; then
    echo "=== game_saves_fixture ==="
    rm -rf "$SAVES_ROOT"
    if ! python3 test/make_game_fixture.py "$SAVES_ROOT"; then
        echo "  FIXTURE FAILED"; fail=1
    else
        # src/game_mgr.c 不进来：那是另一条测试（下方 game_mgr），它自带
        # test/host_mnt_compat.h 顶掉 getmntinfo。game_saves.c 现在也不再引用
        # game_mgr_sfo_text（整目录粒度用不到 SFO 取名字），所以没有桩了。
        #
        # -DGS_SNAP_QUIET_SEC=0：夹具是**刚造出来**的，文件 mtime 就是「现在」，
        # 不置 0 的话「源目录最近被写过就跳过本轮」那条会把每一份快照都挡掉，
        # 快照那一段等于没测。真机上这个值是 20 秒（存档正写到一半时拷出来是撕裂的）。
        run_one game_saves src/game_saves.c \
            -Iinclude -Isrc/psaccounts/third_party \
            -DGS_HOME='"/tmp/pldmgr_saves_test/home"' \
            -DGAME_SAVES_AUTO_PATH='"/tmp/pldmgr_saves_test/saves_auto.json"' \
            -DGAME_SAVES_PACK_DIR='"/tmp/pldmgr_saves_test/packs"' \
            -DGAME_SAVES_SNAP_DIR='"/tmp/pldmgr_saves_test/snap"' \
            -DGS_SNAP_QUIET_SEC=0 \
            src/game_job.c src/json_helpers.c src/utils.c src/inflate.c src/zip_io.c \
            src/psaccounts/third_party/cJSON.c
    fi
else
    echo "=== game_saves SKIPPED: 需要 python3 做夹具 ==="
fi

# ── [CN-FORK] v0.5.2.142 存档的「远程推拉 + 自动执行引擎」两件套 ────────────
# save_remote = 传输层（FTP / HTTP，libcurl）；game_auto = 状态机 + 守护线程。
# 两条都用 test/stub_servers.c 里那两个**真协议**桩（自己实现的极小 FTP / HTTP
# 服务器，监听 127.0.0.1 随机端口），所以测试里的「推送」是真的搬了字节、
# 真的发了 STOR / RETR / PUT —— 不是对着一个假函数打桩。
#
# curl 头从哪来（这是 .142 踩过的坑，别再踩）：
#   · WSL 上 /usr/include/curl/curl.h **不存在**（没装 libcurl4-openssl-dev），
#     但 /usr/lib/x86_64-linux-gnu/libcurl.so 在；
#   · SDK 的 /opt/ps5-payload-sdk/target/include/curl 有全套头。
#   于是编译用 SDK 头、链接用系统库（两边同为 8.18.0，ABI 对得上）。
#   **绝不能直接 -I 整个 SDK include 目录** —— 那底下有整套 FreeBSD 头，会把
#   glibc 的 stdio.h 盖掉，链接期一片 `undefined reference to __error /
#   __stderrp / _CurrentRuneLocale`。只把 curl/ 这一个子目录单独软链出来。
# 缺头或缺库就整段 SKIP，不半跑（与 check_stack_frames 同一态度）。
SR_CURL_INC=""
if [ -f /usr/include/curl/curl.h ]; then
    SR_CURL_INC=/usr/include
elif [ -f /opt/ps5-payload-sdk/target/include/curl/curl.h ]; then
    mkdir -p /tmp/pldmgr_curl_inc
    ln -sfn /opt/ps5-payload-sdk/target/include/curl /tmp/pldmgr_curl_inc/curl
    SR_CURL_INC=/tmp/pldmgr_curl_inc
fi
# [CN-FORK] v0.5.2.143 SMB 那一档（save_remote.c）走 libsmb2，而仓里那份
# third_party/libsmb2/libsmb2.a 是 **FreeBSD/PS5 的目标文件**，链不进宿主的
# glibc 可执行文件。所以宿主这边用同一份 tarball 另编一份 Linux 的：
#   · 源码就是 third_party/libsmb2/libsmb2-6.2.tar.gz（与 PS5 那份**同一个包**，
#     不联网、不另下，避免出现「宿主一份、真机另一份」）；
#   · 编一次缓存到 /root/libsmb2-host —— **别放 /tmp**，WSL 的 /tmp 是 tmpfs，
#     空闲重启会清空，于是每次跑全套都要重编两分钟。
SR_SMB_INC=third_party/libsmb2/include
SR_SMB_DIR=/root/libsmb2-host/libsmb2-6.2
SR_SMB_A=""
# 构建开关：
#   · -DBUILD_SHARED_LIBS=OFF 不能省：默认只出 libsmb2.so（静态那条 add_library
#     只在 PICO_BOARD 分支里），而我们要的是 build/lib/libsmb2.a —— 少了这个开关
#     就是「编成功了但 .a 不存在」，下面整段 SKIP，症状看起来像 cmake 坏了。
#   · -DENABLE_GSSAPI=OFF -DENABLE_LIBKRB5=OFF：宿主这边只是测传输逻辑，走的是
#     用户名/密码（NTLM），**根本用不到 Kerberos 封装**。开着的话 .a 会链进 host
#     系统的 libgssapi_krb5 / libkrb5，链接期报一串 undefined reference
#     （gss_acquire_cred 等）。关掉后 .a 自包含，不依赖系统的 krb5。
#     ⚠ 这条只影响**宿主测试**用的那份 libsmb2；PS5 真机那份由 PS5 SDK 的构建流程
#     单独编（不带 gssapi 也好、带也好，与宿主这份互不干扰）。
SR_SMB_FLAGS="-DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DENABLE_GSSAPI=OFF -DENABLE_LIBKRB5=OFF"
SR_SMB_MARK="$SR_SMB_DIR/build/.cn_fork_nogss"
if [ ! -f "$SR_SMB_DIR/build/lib/libsmb2.a" ] || [ ! -f "$SR_SMB_MARK" ]; then
    echo "=== 宿主版 libsmb2：构建（缓存到 $SR_SMB_DIR）==="
    mkdir -p "$SR_SMB_DIR"
    tar xzf third_party/libsmb2/libsmb2-6.2.tar.gz -C "$SR_SMB_DIR" --strip-components=1 2>/dev/null
    rm -rf "$SR_SMB_DIR/build"
    ( cd "$SR_SMB_DIR" && cmake $SR_SMB_FLAGS -B build -S . >/dev/null 2>&1 \
      && make -C build -j8 >/dev/null 2>&1 && touch "$SR_SMB_MARK" )
fi
if [ -f "$SR_SMB_DIR/build/lib/libsmb2.a" ]; then
    SR_SMB_A="$SR_SMB_DIR/build/lib/libsmb2.a"
else
    echo "=== save_remote / game_auto SKIPPED: 宿主版 libsmb2 编不出来 ==="
fi

printf 'int main(void){return 0;}\n' > /tmp/_curlprobe.c 2>/dev/null
if [ -n "$SR_CURL_INC" ] && [ -n "$SR_SMB_A" ] && clang /tmp/_curlprobe.c -lcurl -o /tmp/_curlprobe 2>/dev/null; then
    rm -f /tmp/_curlprobe.c /tmp/_curlprobe
    # 这两条不用 run_one：-lcurl 必须落在源文件**之后**（默认 --as-needed 下，
    # 出现在源文件之前会被直接丢掉，报一串 undefined reference）。
    rm -rf /tmp/pldmgr_sr_test
    echo "=== save_remote ==="
    if ! clang -Wall -Wextra -O0 -Iinclude -Isrc/psaccounts/third_party -Itest \
        -I"$SR_CURL_INC" -I"$SR_SMB_INC" \
        -DGS_HOME='"/tmp/pldmgr_sr_test/home"' \
        -DGAME_SAVES_AUTO_PATH='"/tmp/pldmgr_sr_test/auto.json"' \
        -DGAME_SAVES_PACK_DIR='"/tmp/pldmgr_sr_test/packs"' \
        -DGAME_SAVES_SNAP_DIR='"/tmp/pldmgr_sr_test/snap"' \
        -DGS_SNAP_QUIET_SEC=0 \
        src/save_remote.c src/curl_init.c src/game_saves.c src/game_job.c src/json_helpers.c \
        src/utils.c src/inflate.c src/zip_io.c src/psaccounts/third_party/cJSON.c \
        test/stub_servers.c test/save_remote_test.c \
        -o /tmp/save_remote_test "$SR_SMB_A" -lpthread -lm -lcurl; then
        echo "  BUILD FAILED"; fail=1
    else
        /tmp/save_remote_test || fail=1
    fi

    rm -rf /tmp/pldmgr_ga_test
    echo "=== game_auto ==="
    if ! clang -Wall -Wextra -O0 -Iinclude -Isrc/psaccounts/third_party -Itest \
        -I"$SR_CURL_INC" -I"$SR_SMB_INC" \
        -DGA_NO_LNCUTIL \
        -DROOT_DIR='"/tmp/pldmgr_ga_test"' \
        -DAUTO_PATH='"/tmp/pldmgr_ga_test/auto.json"' \
        -DGS_HOME='"/tmp/pldmgr_ga_test/home"' \
        -DGAME_SAVES_AUTO_PATH='"/tmp/pldmgr_ga_test/auto.json"' \
        -DGAME_SAVES_PACK_DIR='"/tmp/pldmgr_ga_test/packs"' \
        -DGAME_SAVES_SNAP_DIR='"/tmp/pldmgr_ga_test/snap"' \
        -DGS_SNAP_QUIET_SEC=0 \
        src/game_auto.c src/save_remote.c src/curl_init.c src/game_saves.c src/game_job.c \
        src/json_helpers.c src/utils.c src/inflate.c src/zip_io.c \
        src/psaccounts/third_party/cJSON.c \
        test/stub_servers.c test/game_auto_test.c \
        -o /tmp/game_auto_test "$SR_SMB_A" -lpthread -lm -lcurl; then
        echo "  BUILD FAILED"; fail=1
    else
        /tmp/game_auto_test || fail=1
    fi
else
    rm -f /tmp/_curlprobe.c /tmp/_curlprobe
    echo "=== save_remote / game_auto SKIPPED: 没有可用的 libcurl 头或库 ==="
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
