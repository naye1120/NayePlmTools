#!/usr/bin/env python3
# [CN-FORK] v0.5.2.139 test/make_game_fixture.py
#
# 给 test/game_saves_test.c 造一份存档侧的主机夹具，**按真机实测布局**
# （2026-10，192.168.1.214）：
#
#   /user/home/<uid>/savedata_prospero/<TID>/sdimg_*   ← PS5 存档真正在这里
#   /user/home/<uid>/savedata/<TID>/sdimg_*            ← PS4 存档位
#   /user/home/<uid>/savedata/sce_backup2|4/           ← 系统备份槽（空的）
#
# 为什么这份夹具非改不可：早期版本造的是 <uid>/savedata/<TID>/<槽>，而真机上
# 那个位置只有空的 sce_backup*。测试全绿、真机上每个游戏的存档列表却永远是空的
# —— 夹具把错的布局「证实」成了对的。**夹具必须跟着真机走，不能跟着代码走。**
#
# 这份夹具要逼出来的五条规则：
#   ① 只有系统备份槽的用户不算「有存档」（00000001）
#   ② 多个用户都存过同一个游戏 → 列表出多条，互不覆盖（00000002/03/05）
#   ③ 同一用户两处都有 → 只取 savedata_prospero 那条（00000005）
#   ④ PS4 走 savedata/，kind=PS4，.bin 密封密钥也计入（00000004）
#   ⑤ <GS_HOME> 下的隐藏目录不是用户（.upl_leftover）
#
# 用法：python3 test/make_game_fixture.py [/tmp/pldmgr_saves_test]
# 环境：python3 标准库，无第三方依赖。

import os
import shutil
import sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/pldmgr_saves_test"
HOME = os.path.join(ROOT, "home")

TID_PS5 = "PPSA012345"    # PS5：savedata_prospero 下
TID_PS4 = "CUSA12345"     # PS4：savedata 下
TID_NONE = "PPSA99999"    # 谁都没存过 → 列表必须是空的

# 下面几个数就是 test/game_saves_test.c 里的断言 —— 改了这里必须同步改那边，
# 否则测试会「对着自己刚造的数」自证清白。
U2_BYTES = 600 + 300 + 200      # sdimg_STARBOARD + sdimg_sce_sdmemory + sdimg_sce_bu_sdmemory
U2_FILES = 3
U3_BYTES = 400
U3_FILES = 1
U5_BYTES = 150                  # savedata_prospero 那份；savedata 下那 777 不该被算进来
U5_FILES = 1
U5_DECOY_BYTES = 777            # 只用来证明它没被算进去
U4_BYTES = 250 + 60             # sdimg_CUSA12345 + 它的 .bin 密封密钥
U4_FILES = 2

AUTO_PATH = os.path.join(ROOT, "saves_auto.json")   # 与 -DGAME_SAVES_AUTO_PATH 对应


def blob(path, n):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"\xa5" * n)


def main():
    if os.path.isdir(ROOT):
        shutil.rmtree(ROOT)

    # ⑤ <GS_HOME> 下的隐藏目录：不能被当成用户目录
    blob(os.path.join(HOME, ".upl_leftover", "savedata_prospero", TID_PS5, "sdimg_X"), 999)

    # ① 只有系统备份槽的用户 —— 真机上每个用户都有这两个，空的不算存档
    os.makedirs(os.path.join(HOME, "00000001", "savedata", "sce_backup2"))
    os.makedirs(os.path.join(HOME, "00000001", "savedata", "sce_backup4"))

    # ② + ④ PS5 主存档（3 个 sdimg：当前 + sdmemory + 一个 _bu_ 备份）
    p5 = os.path.join(HOME, "00000002", "savedata_prospero", TID_PS5)
    blob(os.path.join(p5, "sdimg_STARBOARD"), 600)
    blob(os.path.join(p5, "sdimg_sce_sdmemory"), 300)
    blob(os.path.join(p5, "sdimg_sce_bu_sdmemory"), 200)

    # ② 第二个用户也存过同一个游戏 → 列表要出第二条
    blob(os.path.join(HOME, "00000003", "savedata_prospero", TID_PS5, "sdimg_STARBOARD"), U3_BYTES)

    # ④ PS4：savedata/ 下，单个 sdimg_ 文件 + 它的 .bin 密封密钥
    blob(os.path.join(HOME, "00000004", "savedata", TID_PS4, "sdimg_" + TID_PS4), 250)
    blob(os.path.join(HOME, "00000004", "savedata", TID_PS4, "sdimg_" + TID_PS4 + ".bin"), 60)
    os.makedirs(os.path.join(HOME, "00000004", "savedata", "sce_backup2"))

    # ③ 两处都有 → 只该出 savedata_prospero 那条
    blob(os.path.join(HOME, "00000005", "savedata_prospero", TID_PS5, "sdimg_STARBOARD"), U5_BYTES)
    blob(os.path.join(HOME, "00000005", "savedata", TID_PS5, "sdimg_STARBOARD"), U5_DECOY_BYTES)

    # 自动备份配置不建：夹具目录整个刚 rm 过，「默认值」这一段断言得从干净状态起。
    assert not os.path.exists(AUTO_PATH)

    print("fixture ready: %s" % ROOT)
    print("  %s: 3 users (00000002 %dB/%df, 00000003 %dB/%df, 00000005 %dB/%df)"
          % (TID_PS5, U2_BYTES, U2_FILES, U3_BYTES, U3_FILES, U5_BYTES, U5_FILES))
    print("  %s: 1 user  (00000004 %dB/%df, kind=PS4)" % (TID_PS4, U4_BYTES, U4_FILES))
    print("  %s: 0 users" % TID_NONE)


if __name__ == "__main__":
    main()
