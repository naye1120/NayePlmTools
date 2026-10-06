#!/usr/bin/env python3
# [CN-FORK] v0.5.2.116 test/make_game_fixture.py
#
# 给 test/game_saves_test.c 造一份存档侧的主机夹具：真机的
# /user/home/<uid>/savedata/<TID>/<槽> 布局。
#
# 为什么非要两个用户目录、而且**第一个必须是空的**：
# game_saves_root 的规则是「取体积最大的命中」（见 src/game_saves.c 的头注释）——
# 一台主机常有多个用户目录，空的那个先被扫到，整个列表就会变成「这个存档 0 字节」。
# 夹具不把这条逼出来，这个 bug 回来了也没人知道。
#
# 用法：python3 test/make_game_fixture.py [/tmp/pldmgr_saves_test]
# 环境：python3 标准库，无第三方依赖。

import os
import shutil
import sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/pldmgr_saves_test"
HOME = os.path.join(ROOT, "home")

TID_PS4 = "CUSA12345"     # gs_kind：CUSA/PPJB 开头算 PS4
TID_PS5 = "PPSA012345"    # 其余算 PS5

# 下面几个数就是 test/game_saves_test.c 里的断言 —— 改了这里必须同步改那边，
# 否则测试会「对着自己刚造的数」自证清白。
SLOT_DIR = "SaveSlot"            # PS5 形态：一整个目录
SLOT_DIR_BYTES = 1100            # sce_sys/param.sfo 100 + data.bin 600 + sub/nested.dat 400
SLOT_DIR_FILES = 3
SLOT_FILE = "sdimg_CUSA12345"    # PS4 形态：单个文件
SLOT_FILE_BYTES = 250
SLOT_FILE_FILES = 1
HIDDEN = ".upl_1234567890"       # 上传的临时目录：一级子项里也必须被跳过

AUTO_PATH = os.path.join(ROOT, "saves_auto.json")   # 与 -DGAME_SAVES_AUTO_PATH 对应


def blob(path, n):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"\xa5" * n)


def main():
    if os.path.isdir(ROOT):
        shutil.rmtree(ROOT)

    # ① 空用户目录 —— 必须输给下面那个有数据的（game_saves_root 取体积最大的命中）
    os.makedirs(os.path.join(HOME, "00000001", "savedata", TID_PS4))

    # ② 有数据的用户目录：两种槽形态 + 一个该被跳过的隐藏目录
    a = os.path.join(HOME, "00000002", "savedata", TID_PS4)
    blob(os.path.join(a, SLOT_DIR, "sce_sys", "param.sfo"), 100)
    blob(os.path.join(a, SLOT_DIR, "data.bin"), 600)
    blob(os.path.join(a, SLOT_DIR, "sub", "nested.dat"), 400)
    blob(os.path.join(a, SLOT_FILE), SLOT_FILE_BYTES)
    blob(os.path.join(a, HIDDEN, "junk.bin"), 999)

    # ③ 另一个 tid，只为验 gs_kind 的另一条分支（CUSA=PS4，其余=PS5）
    blob(os.path.join(HOME, "00000002", "savedata", TID_PS5, "slot1", "a.dat"), 50)

    # 自动备份配置不建：夹具目录整个刚 rm 过，「默认值」这一段断言得从干净状态起。
    assert not os.path.exists(AUTO_PATH)

    print("fixture ready: %s" % ROOT)
    print("  %s: root=00000002 slots=%d bytes=%d files=%d"
          % (TID_PS4, 2, SLOT_DIR_BYTES + SLOT_FILE_BYTES,
             SLOT_DIR_FILES + SLOT_FILE_FILES))
    print("  %s: slots=1" % TID_PS5)


if __name__ == "__main__":
    main()
