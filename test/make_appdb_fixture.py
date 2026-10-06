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
import os
import sqlite3
import sys

ROOT_DEFAULT = "/tmp/pldmgr_gm_test"

# 与 test/game_mgr_test.c 里的 TID_PS5 / TID_PS4 一一对应，改一个就得改两个。
TID_PS5 = "PPSA00001"
TID_PS4 = "CUSA00002"
NAME_PS5 = "Test Game One"
NAME_PS4 = "测试游戏二"


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else ROOT_DEFAULT
    os.makedirs(root, exist_ok=True)
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


if __name__ == "__main__":
    main()
