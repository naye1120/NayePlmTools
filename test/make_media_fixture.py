#!/usr/bin/env python3
# [CN-FORK] v0.5.2.115 test/make_media_fixture.py
#
# 给 test/media_scan_test.c 造一份主机夹具：一个**能被 src/game_media.c 那套极简 SQLite
# 解析器读出来**的 av_content.db，外加它引用到的真实文件。
#
# 为什么不用真机抓回来的库（D:\pldmanager\_ftp_probe\media\av_content.db）：
#   ① 那里面带真机 account 相关列，不该进仓库；
#   ② 夹具必须自给自足 —— 换台机器 clone 下来也要能跑，
#      而 FTP 取证数据只在本机有。
# 所以这里用 sqlite3 现造一个**列序照抄真机 schema 的子集**（真机 tbl_PhotoVideo 66 列，
# 这里留 19 列、相对顺序与真机一致）：game_media.c 是「按列名定位序号」不是「写死序号」，
# 列数变了也该读对 —— 这正是这个夹具要守住的性质。
#
# 用法：python3 test/make_media_fixture.py [/tmp/pldmgr_media_test]
# 环境：python3 + 标准库（sqlite3 / zlib / struct），无第三方依赖。

import os
import re
import shutil
import sqlite3
import struct
import sys
import time
import zlib

ROOT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/pldmgr_media_test"

PHOTO = os.path.join(ROOT, "av_contents", "photo")
THUMB = os.path.join(ROOT, "av_contents", "thumbnails", "photo")
VIDEO = os.path.join(ROOT, "av_contents", "video")

# 真机：/av_contents/photo/<生成器>/<TID>/<3位hex>/<时间戳_随机>.<ext>
GEN = "NPXS40087"
TID = "PPSA99008"     # 主测试游戏：5 条（3 截图 + 2 录屏）
OTHER = "PPSA00001"   # 另一个游戏 1 条 —— 删除测试拿它开刀，免得把主集合改小
SHARD = "18c"


def png_bytes(w, h, rgb):
    """纯标准库拼一张 PNG（8bit 真彩，整幅同色 → 压完很小）。"""
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    row = b"\x00" + (bytes(rgb) * w)
    raw = row * h
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9))
            + chunk(b"IEND", b""))


def mtime_of(path):
    """文件名里的 YYYYMMDD_HHMMSS → 本地时间戳（取不到就返回 None）。

    ⚠️ **夹具必须把 mtime 钉死**（v0.5.2.126）。zip 内条目名用的是文件**真实 mtime**
    （下载名精确到秒），不钉的话每次跑出来的名字都不一样 —— C 侧那条「同一秒的两张
    要被去重成 …-2」就没法断言了（会时红时绿，比不测更糟）。
    钉成文件名里那个时间还有额外好处：下载名与原始文件名里的时刻终于对得上。"""
    m = re.search(r"(\d{8})_(\d{6})", os.path.basename(path))
    if not m:
        return None
    return time.mktime(time.strptime(m.group(1) + m.group(2), "%Y%m%d%H%M%S"))


def put(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)
    ts = mtime_of(path)
    if ts is not None:
        os.utime(path, (ts, ts))
    return path


def rel(media_type, *parts):
    """绝对路径 → content_path（相对媒体根，即去掉 /user 前缀）。
    ⚠️ 第一个参数是 photo/video 这一层，必须拼进去 —— 漏了它 content_path 会少一段，
    stat 全部落空，表现是「库读到了、列也对，但一条都列不出来」（踩过一次）。"""
    return os.path.join("/av_contents", media_type, *parts).replace(os.sep, "/")


# ── 文件 ────────────────────────────────────────────────────────────────────
p1 = os.path.join(PHOTO, GEN, TID, SHARD, "20261005_165454_00474347.png")
p2 = os.path.join(PHOTO, GEN, TID, SHARD, "20261005_170000_00474348.jxr")
p3 = os.path.join(PHOTO, GEN, TID, SHARD, "20261005_180000_00474349.png")
v1 = os.path.join(VIDEO, GEN, TID, SHARD, "20261006_101112_001.mp4")
v2 = os.path.join(VIDEO, GEN, TID, SHARD, "20261006_102233_002.mp4")
o1 = os.path.join(PHOTO, GEN, OTHER, "9a1", "20261004_090000_00000001.png")

put(p1, png_bytes(640, 360, (30, 90, 200)))
# HDR 截图：stb 解不了 .jxr —— 真机上就是靠 icon_path 兜的，这里**故意不给 icon**，
# 用来覆盖 game_media_thumb 的 -2（→ 404）分支。
put(p2, b"\x49\x49\x00\x01" + os.urandom(256))
put(p3, png_bytes(1920, 1080, (200, 40, 40)))
# 录屏：假 mp4（我们不解视频，只做 stat / 打包 / 直出）
# ⚠️ v1 刻意给到 ~1.5 MB，**不是**为了逼真：zip_write 每次只读 256 KB，
# 原来两个 4 KB 的文件意味着「一个分块都没读满过」—— on_bytes 只回调一次、
# CRC 回填那两行 lseek 只在收尾走一遍，那条路等于从来没被测过。
# 用户报的「打包卡在 0 B / 58.7 MB」正好落在这条路上，所以这里必须跨过 256 KB。
put(v1, b"\x00\x00\x00\x18ftypmp42" + os.urandom(1536 * 1024))
put(v2, b"\x00\x00\x00\x18ftypmp42" + os.urandom(4096))
put(o1, png_bytes(320, 180, (10, 200, 10)))

# ── v0.5.2.122 专供：录屏**有** icon_path，但那份是 .jxr（HDR 封面帧）──────────
# 真机上录屏的封面帧就是 .jxr，而 stb 解不了 .jxr。旧代码（.120 初版）只看
# 「icon 在不在」就走进解码分支 → 解码失败 → 回 -2 → **视频缩略图永远空白**，
# 这正是用户复现的那个症状。这条夹具让「icon 存在且 stat 得过」与「解不动」
# 同时成立，把「必须回占位图」这条性质钉死。单独一个 tid，免得动到主集合的计数。
JXR_TID = "PPSA99009"
v3 = os.path.join(VIDEO, GEN, JXR_TID, SHARD, "20261007_120000_003.mp4")
put(v3, b"\x00\x00\x00\x18ftypmp42" + os.urandom(2048))
ICON_JXR = os.path.join(ROOT, "av_contents", "thumbnails", "video", GEN, JXR_TID, SHARD,
                        "20261007_120000_003.mp4.jxr")
put(ICON_JXR, b"\x49\x49\xbc\x01" + os.urandom(512))   # 假 JXR 头 + 随机体

# ── v0.5.2.124 专供①：封面帧的目录**只有 icon_path 知道**，不能靠 id 推 ────────
# 用户口径（第二、第三轮）：「视频的预览图不对，应该是视频里面某一帧画面」→「视频封面也没有」。
# .123 第一版按 id 推同构目录（/thumbnails/<与 id 同链路>），可 include/game_media.h
# §7a 白纸黑字写着「录屏目录层级是否与 photo 同构」**尚未在真机验证过**（取证时机器上
# 只有截图）。而 icon_path 是**媒体库自己写进库里的**，它指哪儿就是哪儿，最权威。
# 这条夹具刻意让两者**不一致**：录屏在 .../video/<GEN>/<TID>/<shard>/ 下，
# 而它的缩略图在 .../thumbnails/video/<TID>/<shard>/ 下（**少了生成器那一段**）。
# 于是「按 id 推」必落空，只有「认 icon_path 所在目录」才找得到 —— 这正是 .124 的修法。
# （.123 的夹具把两份路径摆成同一个目录，所以它测不出这条差别，白绿了一场。）
COVER_TID = "PPSA99010"
v4 = os.path.join(VIDEO, GEN, COVER_TID, SHARD, "20261008_090000_004.mp4")
put(v4, b"\x00\x00\x00\x18ftypmp42" + os.urandom(4096))
# 缩略图目录：**没有** GEN 这一层，与录屏目录不同构
ICON_DIR_C = os.path.join(ROOT, "av_contents", "thumbnails", "video", COVER_TID, SHARD)
ICON_HDR = os.path.join(ICON_DIR_C, "20261008_090000_004.mp4.jxr")
put(ICON_HDR, b"\x49\x49\xbc\x01" + os.urandom(512))   # HDR 那份：存在、stat 得过、解不动
# 同目录的真封面帧（.jpeg）由 C 侧 write_cover_jpeg() 写进同一个目录
# （put 已经把目录建出来了）。

# ── v0.5.2.124 专供②：**扩展名骗人** —— 同目录只有一份「名字像 JPEG、内容不是」──
# .123 只按扩展名（md_is_stb_image(md_mime_of(...))）放行候选，于是会挑中这个文件；
# 它一路传到 stbi_load_from_memory 失败 → 返回 -2 → HTTP 404 → 界面从「占位图」
# 退化成「一块空白」。.124 按**文件头魔数**认，必须跳过它、回落到占位图
# （rc=0 + 深底白三角），**不能**是 404。
# 只放一份诱饵（不放真图），命中与否才是确定的 —— readdir 顺序不参与判定。
BADC_TID = "PPSA99011"
v5 = os.path.join(VIDEO, GEN, BADC_TID, SHARD, "20261009_100000_005.mp4")
put(v5, b"\x00\x00\x00\x18ftypmp42" + os.urandom(4096))
ICON_DIR_B = os.path.join(ROOT, "av_contents", "thumbnails", "video", GEN, BADC_TID, SHARD)
put(os.path.join(ICON_DIR_B, "20261009_100000_005.mp4.jxr"),
    b"\x49\x49\xbc\x01" + os.urandom(512))             # 解不动的那份
put(os.path.join(ICON_DIR_B, "20261009_100000_005.mp4.jpeg"),
    b"this is plainly not a JPEG\n" * 8)               # 诱饵：后缀骗人，内容不是图

# ── v0.5.2.126 专供：同一秒连拍两张 → 压缩包里会撞名 ────────────────────────
# 用户口径：「压缩包下级文件夹是 游戏编码-游戏名称，这个文件夹里面直接就是截图和视频」。
# 摊平到这一层之后，条目名只剩「游戏名称-YYYYMMDD_HHMMSS.ext」——原文件名里那截
# 随机数被丢掉了，于是**同一秒的两张必然同名**。zip 里两条同名条目在 Windows
# 资源管理器里解压是互相覆盖：那是**静默丢文件**，比名字难看严重得多。
# 所以 md_zname_unique 要给后一条补 -2。这条夹具给的就是「同秒、不同随机数」那一对
# （文件名里的时刻相同 → mtime 也被钉成同一秒）。
# ⚠️ 它必须**带游戏名**（见 media_scan_test.c 的桩）：没有游戏名时下载名会退回原始
# 文件名，两个随机数不同就撞不上了，这条夹具也就白摆了。
DUP_TID = "PPSA99012"
put(os.path.join(PHOTO, GEN, DUP_TID, SHARD, "20261010_120000_010.png"),
    png_bytes(320, 180, (11, 22, 33)))
put(os.path.join(PHOTO, GEN, DUP_TID, SHARD, "20261010_120000_011.png"),
    png_bytes(320, 180, (44, 55, 66)))

# 副文件：同目录 <原名>.meta（JSON）与 <原名>.dat（1024B），删除时要一并干掉
put(p1 + ".meta", b'{"ageLevel":1,"appVer":"01.00","segmentInfo":"game"}')
put(p1 + ".dat", os.urandom(1024))

# 缩略图：真机是 .jpeg，由 test/media_scan_test.c 用 stb_image_write 现写一张
# （Python 标准库造不出 JPEG）。path 必须与下面 DB 里 row1 的 icon_path 一致。
ICON = os.path.join(THUMB, GEN, TID, SHARD, "20261005_165454_00474347.png.jpeg")

# ── av_content.db ───────────────────────────────────────────────────────────
# 列名与**相对顺序**照抄真机 tbl_PhotoVideo（66 列里的 19 列）。game_media.c 靠
# md_sql_col_index 按列名数逗号，所以这里的顺序不是装饰：
#   · width 必须排在 icon_width 前面（前缀撞车）
#   · duration 必须排在 duration_ms 前面（前缀撞车）
# 这两条真机上也是这个顺序，夹具照抄，防止哪天「优化」成硬编码序号。
DDL = """CREATE TABLE tbl_PhotoVideo (
  mrid INTEGER PRIMARY KEY AUTOINCREMENT,
  parental_level TEXT,
  duration INTEGER,
  duration_ms INTEGER,
  width INTEGER,
  height INTEGER,
  icon_width INTEGER,
  icon_height INTEGER,
  mime_type INTEGER,
  content_type TEXT,
  generator_title_id TEXT,
  content_path TEXT,
  appli_title_id TEXT,
  optional_title_id TEXT,
  imported_date INTEGER,
  last_update_date INTEGER,
  title TEXT,
  icon_path TEXT,
  created_time INTEGER
)"""

# (content_path, icon_path, w, h, duration, duration_ms, tid, mime, kind)
ROWS = [
    (rel("photo", GEN, TID, SHARD, "20261005_165454_00474347.png"),
     "/av_contents/thumbnails/photo/%s/%s/%s/20261005_165454_00474347.png.jpeg" % (GEN, TID, SHARD),
     3840, 2160, 0, 0, TID, 131083, "png"),
    (rel("photo", GEN, TID, SHARD, "20261005_170000_00474348.jxr"),
     "", 3840, 2160, 0, 0, TID, 131098, "jxr"),
    (rel("photo", GEN, TID, SHARD, "20261005_180000_00474349.png"),
     "", 1920, 1080, 0, 0, TID, 131083, "png"),
    (rel("video", GEN, TID, SHARD, "20261006_101112_001.mp4"),
     "", 1280, 720, 0, 15000, TID, 131072, "mp4"),
    (rel("video", GEN, TID, SHARD, "20261006_102233_002.mp4"),
     "", 1280, 720, 12, 0, TID, 131072, "mp4"),
    (rel("photo", GEN, OTHER, "9a1", "20261004_090000_00000001.png"),
     "", 320, 180, 0, 0, OTHER, 131083, "png"),
    # 录屏 + .jxr 封面帧：icon_path 非空且文件真的在，但 stb 解不动 ——
    # 缩略图必须回**占位图**（深色底 + 播放三角），不能回 404。见上面 JXR_TID 那段。
    (rel("video", GEN, JXR_TID, SHARD, "20261007_120000_003.mp4"),
     "/av_contents/thumbnails/video/%s/%s/%s/20261007_120000_003.mp4.jxr" % (GEN, JXR_TID, SHARD),
     1280, 720, 0, 8000, JXR_TID, 131072, "mp4"),
    # 录屏 + 封面帧，但**缩略图目录与录屏目录不同构**（缩略图少一层生成器）：
    # icon_path 指到 .jxr，缩略图必须落到 icon_path 所在目录里那份 .jpeg 的**真画面**上。
    # 「按 id 推同构目录」在这条上必落空 —— 见 COVER_TID 那段。
    (rel("video", GEN, COVER_TID, SHARD, "20261008_090000_004.mp4"),
     "/av_contents/thumbnails/video/%s/%s/20261008_090000_004.mp4.jxr" % (COVER_TID, SHARD),
     1280, 720, 0, 6000, COVER_TID, 131072, "mp4"),
    # 录屏 + 同目录只有一份「后缀像 JPEG、内容不是」的诱饵：必须跳过诱饵回占位图，
    # 不能挑中它 → 解码失败 → 404。见 BADC_TID 那段。
    (rel("video", GEN, BADC_TID, SHARD, "20261009_100000_005.mp4"),
     "/av_contents/thumbnails/video/%s/%s/%s/20261009_100000_005.mp4.jxr" % (GEN, BADC_TID, SHARD),
     1280, 720, 0, 9000, BADC_TID, 131072, "mp4"),
    # 同一秒连拍两张：压缩包摊平后必然撞名，必须去重（见上面 DUP_TID 那段）。
    (rel("photo", GEN, DUP_TID, SHARD, "20261010_120000_010.png"),
     "", 320, 180, 0, 0, DUP_TID, 131083, "png"),
    (rel("photo", GEN, DUP_TID, SHARD, "20261010_120000_011.png"),
     "", 320, 180, 0, 0, DUP_TID, 131083, "png"),
]

db = os.path.join(ROOT, "av_content.db")
if os.path.exists(db):
    os.remove(db)

con = sqlite3.connect(db)
con.execute("PRAGMA page_size=4096")   # 真机就是 4096
con.execute("PRAGMA journal_mode=DELETE")  # 真机是回滚日志模式，不是 WAL
con.execute("PRAGMA encoding='UTF-8'")
con.execute(DDL)
con.executemany(
    "INSERT INTO tbl_PhotoVideo (parental_level, duration, duration_ms, width, height,"
    " icon_width, icon_height, mime_type, content_type, generator_title_id, content_path,"
    " appli_title_id, title, icon_path, created_time)"
    " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
    [(None, d, dm, w, h, None, None, mime, kind, GEN, cp, tid, None, ic or None, 0)
     for (cp, ic, w, h, d, dm, tid, mime, kind) in ROWS],
)
con.commit()
con.close()
os.chmod(db, 0o444)  # 铁律①：库只读。测试里也会断言它是 444。

# ── 汇总清单，给 shell / C 测试复用 ─────────────────────────────────────────
manifest = os.path.join(ROOT, "manifest.txt")
with open(manifest, "w") as f:
    for (cp, ic, _w, _h, _d, _dm, tid, _m, kind) in ROWS:
        f.write("%s\t%s\t%s\n" % (tid, kind, cp))
    f.write("ICON\t%s\t%s\n" % (TID, ICON))

print("fixture ready: %s (%d rows, %d bytes db)" % (ROOT, len(ROWS), os.path.getsize(db)))
print("icon (written by C test): %s" % ICON)
