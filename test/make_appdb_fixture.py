#!/usr/bin/env python3
# [CN-FORK] 合成一份最小 app.db —— test/game_mgr_test.c 的夹具。
#
# 真机的 /system_data/priv/mms/app.db 主机上当然没有，而 src/game_mgr.c 走的
# 「已安装游戏」整条链路是自研的只读 SQLite 解析器（st_bwalk / st_rec_col）。
# 所以这里就用标准 sqlite3 建一张 tbl_contentinfo，让那套解析器自己去读 ——
# 夹具**不参与解析**，只负责把文件摆对：解析器读得到，等于这套解析确实认得
# 真库的页与记录格式。
#
# 列序是硬约束（src/game_mgr.c 的 st_cb_enum 按**下标**取）：
#   col0 = titleId  —— 必须恰好 9 位且过 gm_tid_is_game（PPSA/CUSA/PPJB/EMUA/PAUS）
#   col2 = titleName
# 其余列是**按列名**在建表 SQL 里现定位的（gm_db_row → gm_sql_col_index），
# 顺序无所谓，但名字必须一字不差，否则详情里 content_id / install_time /
# last_launch / appdb_size / can_uninstall 全是空的 —— 那 5 个字段正好是
# GM_DB_COLNAMES 那 5 个。这里照真库给全，让按名查找那条路也走一遍。
# sqlite_master 那一侧由 st_cb_master 按 col1 == "tbl_contentinfo" 找根页，
# 标准 CREATE TABLE 自然满足。
# [CN-FORK] v0.5.2.127 来源类别（pkg / image / folder）的夹具。
#
# 判据是 ShadowMount 落在 /user/app/<TID>/ 下的两个文本文件，而 /user/app 在主机上
# 当然没有 —— 所以 src/game_mgr.c 把这两个根做成了可覆盖宏（GAME_MGR_APP_BASE /
# GAME_MGR_SYSTEM_EX，手法同 GAME_MGR_APPDB_PATH），本夹具就把那两棵树现造出来：
#
#   <root>/user_app/<TID>/mount_img.lnk  首行 = 源镜像路径（带镜像扩展名）→ image
#   <root>/user_app/<TID>/mount.lnk      首行 = 裸目录源路径             → folder
#   两个都没有                                                          → pkg（内置安装）
#   <root>/system_ex/app/<TID>/sce_sys/param.json 存在 → mounted=true
#
# 三款游戏刚好覆盖三个类别 + mounted 的真假两支，正是真机上被标错的那批。
import os
import shutil
import sqlite3
import sys

ROOT_DEFAULT = "/tmp/pldmgr_gm_test"

# 与 test/game_mgr_test.c 里的 TID_PS5 / TID_PS4 / TID_FOLDER 一一对应，改一个就得改两个。
TID_PS5 = "PPSA00001"     # 镜像游戏（mount_img.lnk 指向 .ffpfsc）
TID_PS4 = "CUSA00002"     # 真·内置安装 PKG（两个 lnk 都没有）
TID_FOLDER = "PPSA00003"  # 裸目录游戏（mount.lnk 指向外置目录），且此刻挂载中
NAME_PS5 = "Test Game One"
NAME_PS4 = "测试游戏二"
NAME_FOLDER = "Test Game Three"

# 镜像游戏的源镜像路径 —— 内容无所谓（主机上不存在），断言只比对字符串。
IMG_PATH = "/mnt/ext1/%s.ffpfsc" % TID_PS5
# 镜像游戏的运行时挂载点（mount.lnk 首行）。故意放在 /mnt/shadowmnt 下：
# gr_running() 只认这一处为「游戏还在跑」，主机测试不碰它，但真机语义要对得上。
IMG_RUNTIME = "/mnt/shadowmnt/%s_abcd1234" % TID_PS5
# 裸目录游戏的源目录。**必须在夹具根之下、且真实存在** —— v0.5.2.136 起列表体积走
# 「只读缓存 + 后台预热」这条路，测试要能验「预热前 size=0 / 预热后拿到真实体积」，
# 所以这里真的造一棵目录树出来（原来指 /mnt/ext1，主机上根本不存在，du 恒为 0，
# 那条断言就只验得到「没扫」，验不到「扫到了」）。
# 两个文件的总字节数必须与 test/game_mgr_test.c 的 FOLDER_BYTES 一致。
FOLDER_BYTES = 4096 + 1000
def folder_path(root):
    return os.path.join(root, "src", TID_FOLDER)


def write_lnk(path, line):
    with open(path, "w", encoding="utf-8") as f:
        f.write(line + "\n")


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else ROOT_DEFAULT
    # 整棵清掉再重建：上一次跑剩下的 lnk 会让「这次该是 pkg 的那款」变成 image，
    # 断言就会时好时坏 —— 那正是最难查的一类假失败。
    if os.path.isdir(root):
        shutil.rmtree(root)
    os.makedirs(root, exist_ok=True)
    app_base = os.path.join(root, "user_app")
    system_ex = os.path.join(root, "system_ex")
    os.makedirs(app_base, exist_ok=True)

    # ① 镜像游戏：mount_img.lnk 定档，mount.lnk 指运行时挂载点（此刻没挂，文件不存在）。
    os.makedirs(os.path.join(app_base, TID_PS5), exist_ok=True)
    write_lnk(os.path.join(app_base, TID_PS5, "mount_img.lnk"), IMG_PATH)
    write_lnk(os.path.join(app_base, TID_PS5, "mount.lnk"), IMG_RUNTIME)

    # ② 裸目录游戏：只有 mount.lnk，直接指向源目录；再给它一个 /system_ex 挂载点
    #    = mounted 为真的那一支（照 ShadowMountPlus is_data_mounted 的判据）。
    os.makedirs(os.path.join(app_base, TID_FOLDER), exist_ok=True)
    folder = folder_path(root)
    write_lnk(os.path.join(app_base, TID_FOLDER, "mount.lnk"), folder)
    # 真实内容：两个普通文件（一个在子目录里），总量 FOLDER_BYTES。
    # gm_dir_size 只累加**普通文件**的 st_size、并跳过 '.' 开头的条目，
    # 所以这里刻意放一个 .hidden（不算）来钉住这条口径。
    os.makedirs(os.path.join(folder, "sub"), exist_ok=True)
    with open(os.path.join(folder, "data.bin"), "wb") as f:
        f.write(b"\0" * 4096)
    with open(os.path.join(folder, "sub", "extra.bin"), "wb") as f:
        f.write(b"\0" * 1000)
    with open(os.path.join(folder, ".hidden"), "wb") as f:
        f.write(b"\0" * 999)
    mnt_sce = os.path.join(system_ex, "app", TID_FOLDER, "sce_sys")
    os.makedirs(mnt_sce, exist_ok=True)
    with open(os.path.join(mnt_sce, "param.json"), "w", encoding="utf-8") as f:
        f.write("{}")

    # ③ 真·内置 PKG（TID_PS4）：**什么都不建** —— 连目录都不建。
    #    /user/app/<TID> 不存在，两个 lnk 也读不到，这才是 pkg 的完整形态。
    out = os.path.join(root, "app.db")

    # 连同 WAL/回滚日志一并清掉，免得上一次跑剩下的残留盖住本次的行。
    for suffix in ("", "-journal", "-wal", "-shm"):
        path = out + suffix
        if os.path.exists(path):
            os.remove(path)

    con = sqlite3.connect(out)
    try:
        con.execute(
            "CREATE TABLE tbl_contentinfo ("
            " titleId TEXT NOT NULL,"   # col0  st_cb_enum 按下标取
            " contentId TEXT,"          # col1
            " titleName TEXT,"          # col2  st_cb_enum 按下标取
            " lastAccessTime TEXT,"     # col3  ↓ 以下 4 个按列名取（GM_DB_COLNAMES）
            " uninstallable INTEGER,"   # col4
            " size INTEGER,"            # col5
            " installTime TEXT"         # col6
            ")"
        )
        con.executemany(
            "INSERT INTO tbl_contentinfo"
            " (titleId, contentId, titleName, lastAccessTime,"
            "  uninstallable, size, installTime)"
            " VALUES (?,?,?,?,?,?,?)",
            [
                (TID_PS5, "up0000-TEST_0000000000000-SAVE00001", NAME_PS5,
                 "2026/10/05 16:54:54", 1, 42949672960, "2026/01/02 10:11:12"),
                (TID_PS4, "up0000-TEST_0000000000000-SAVE00002", NAME_PS4,
                 "2026/10/06 10:11:12", 0, 17179869184, "2025/12/31 23:59:59"),
                (TID_FOLDER, "up0000-TEST_0000000000000-SAVE00003", NAME_FOLDER,
                 "2026/10/07 08:09:10", 1, 32212254720, "2026/03/04 05:06:07"),
            ],
        )
        con.commit()
    finally:
        con.close()

    size = os.path.getsize(out)
    # src/game_mgr.c 里 game_enum_appdb 自己就卡 100..64MB 与「至少 200 字节」，
    # 夹具给不出 200 字节就是夹具的错，别让它悄悄变成「列表空」。
    if size < 200:
        sys.exit("fixture too small: %d bytes" % size)
    print("wrote %s (%d bytes)" % (out, size))
    print("  app_base  = %s" % app_base)
    print("  system_ex = %s" % system_ex)
    print("  %s -> image  (%s)" % (TID_PS5, IMG_PATH))
    print("  %s -> pkg    (no lnk files)" % TID_PS4)
    print("  %s -> folder (%s, mounted, %d bytes)"
          % (TID_FOLDER, folder_path(root), FOLDER_BYTES))


if __name__ == "__main__":
    main()
