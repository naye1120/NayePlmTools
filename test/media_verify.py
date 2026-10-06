#!/usr/bin/env python3
# [CN-FORK] v0.5.2.115 test/media_verify.py — 给 test/media_scan_test.c 的产出做机器校验。
#
# C 侧没法验 zip（仓库里没有 zlib），这里用标准库 zipfile 把关：
#   逐条 CRC（testzip）、内容与源文件逐字节一致、**条目名的结构**、
#   压缩方式必须是 STORED（我们自己写的 zip，没有 deflate）。
#
# v0.5.2.126 起条目名不再是「content_path 去掉 /av_contents/」，而是
#   <游戏编码-游戏名称>/<游戏名称-YYYYMMDD_HHMMSS.<ext>>
# （用户口径：「压缩包下级文件夹是 游戏编码-游戏名称，这个文件夹里面直接就是截图和视频」）。
# 所以这里要把那份命名**独立算一遍**再比对 —— 与 DDS 头那套一个思路：
# 规格在两边各写一次，对不上就红。游戏名里的非法字符、扩展名小写、时间戳取
# **源文件真实 mtime 的本地时间**，都要跟 C 侧一致。
#
# 用法：
#   media_verify.py json <list.json> <期望 count>
#   media_verify.py zip  <pack.zip> <夹具根目录> <tid> <游戏名称> <content_path> [<content_path>…]

import json
import os
import re
import sys
import time
import zipfile

ZIP_PREFIX = "/av_contents/"  # content_path 的前缀


def fail(msg):
    print("  VERIFY FAIL: %s" % msg)
    sys.exit(1)


# ── 与 src/game_media.c 的 md_sanitize_name / md_ext_of / md_stamp_compact 同规格 ──
def sanitize(s):
    out = []
    for ch in s:
        o = ord(ch)
        if ch in " :/\\<>\"|?*" or o < 0x20 or o == 0x7F:
            out.append("-")
        else:
            out.append(ch)
    return "".join(out)


def ext_of(path):
    m = re.search(r"\.([A-Za-z0-9]+)$", path)
    return m.group(1).lower() if m else "bin"


def compact_stamp(path):
    return time.strftime("%Y%m%d_%H%M%S", time.localtime(os.stat(path).st_mtime))


def expected_name(root, tid, game, cid):
    src = root + cid
    g = sanitize(game)
    return "%s-%s/%s-%s.%s" % (tid, g, g, compact_stamp(src), ext_of(cid))


def unique_name(base, prior):
    """与 src/game_media.c 的 md_zname_unique 同规格：撞名就补 -2 / -3 …
    下载名只精确到秒，同一秒连拍两张就会撞；zip 里两条同名条目在 Windows 上
    解压是互相覆盖（静默丢文件），所以这一层必须去重。"""
    if base not in prior:
        return base
    folder, fn = base.split("/", 1)
    dot = fn.rfind(".")
    stem, ext = (fn[:dot], fn[dot:]) if dot > 0 else (fn, "")
    k = 2
    while True:
        cand = "%s/%s-%d%s" % (folder, stem, k, ext)
        if cand not in prior:
            return cand
        k += 1


def check_json(path, expect_count):
    try:
        with open(path, "r", encoding="utf-8") as f:
            doc = json.load(f)
    except Exception as e:  # 分隔符/转义写坏时第一个炸的就是这里
        fail("list.json 解析不了: %s" % e)

    if doc.get("tid") is None or "items" not in doc or "count" not in doc:
        fail("缺 tid/items/count 字段: %r" % list(doc.keys()))
    if doc["count"] != len(doc["items"]):
        fail("count=%r 但 items 有 %d 条" % (doc["count"], len(doc["items"])))
    if doc["count"] != expect_count:
        fail("count=%d，期望 %d" % (doc["count"], expect_count))

    seen = set()
    for it in doc["items"]:
        for k in ("id", "kind", "name", "mtime", "size", "w", "h", "duration"):
            if k not in it:
                fail("item 缺字段 %s: %r" % (k, it))
        if it["kind"] not in ("screenshot", "video"):
            fail("kind 必须是 screenshot/video，得到 %r" % it["kind"])
        if not re.match(r"^\d{4}-\d{2}-\d{2} \d{2}:\d{2}$", it["mtime"]):
            fail("mtime 格式不对: %r" % it["mtime"])
        if not it["id"].startswith("/av_contents/"):
            fail("id 不是 content_path: %r" % it["id"])
        if not isinstance(it["size"], int) or it["size"] <= 0:
            fail("size 不是正整数: %r" % it["size"])
        if not isinstance(it["duration"], (int, float)) or it["duration"] < 0:
            fail("duration 不是非负数: %r" % it["duration"])
        if it["id"] in seen:
            fail("id 重复: %r" % it["id"])
        seen.add(it["id"])

    print("  verify json OK: %d items, tid=%s" % (doc["count"], doc.get("tid")))


def check_zip(path, root, tid, game, ids):
    # ⚠️ ids 必须**按包内条目的先后顺序**给：撞名时「谁拿到干净名字、谁被补 -2」
    # 取决于顺序（见 md_zname_unique）。顺序对不上这里会红，不会静默放过。
    expected = []
    for cid in ids:
        if not cid.startswith(ZIP_PREFIX):
            fail("期望的 id 不是 content_path: %r" % cid)
        src = root + cid
        if not os.path.exists(src):
            fail("夹具里没有这个文件: %r" % src)
        expected.append(unique_name(expected_name(root, tid, game, cid), expected))

    # 结构性质先独立断言一遍（不依赖下面的逐条比对）：
    # 每个条目名**恰好一个 '/'**（顶层文件夹 + 文件名，下面不再有子目录），
    # 且所有条目共用同一个顶层文件夹。
    folders = set()
    for nm in expected:
        if nm.count("/") != 1:
            fail("期望的条目名不是「一层目录 + 文件名」: %r" % nm)
        folders.add(nm.split("/")[0])
    if len(folders) != 1:
        fail("期望的条目名不在同一个顶层文件夹里: %r" % folders)

    try:
        zf = zipfile.ZipFile(path, "r")
    except Exception as e:
        fail("zip 打不开: %s" % e)

    bad = zf.testzip()
    if bad is not None:
        fail("CRC 校验失败的条目: %r" % bad)

    got = sorted(zf.namelist())
    if len(got) != len(set(got)):
        fail("包里有**重名**条目（解压会互相覆盖）: %r" % got)
    if got != sorted(expected):
        fail("条目名不符\n    期望 %r\n    实际 %r" % (sorted(expected), got))

    # 实际产出的那份也过一遍同样的结构性质 —— 上面那份是我们算的，这份是包里真有的。
    got_folders = set()
    for nm in got:
        if nm.count("/") != 1:
            fail("条目 %r 不是「一层目录 + 文件名」（多了子目录或没有目录）" % nm)
        got_folders.add(nm.split("/")[0])
    if len(got_folders) != 1:
        fail("条目散在多个顶层文件夹里: %r" % got_folders)
    if got_folders != folders:
        fail("顶层文件夹不是「%s」: %r" % (list(folders)[0], got_folders))

    total = 0
    for i, cid in enumerate(ids):
        name = expected[i]
        info = zf.getinfo(name)
        if info.compress_type != zipfile.ZIP_STORED:
            fail("%s 压缩方式 %d，应为 STORED(0)" % (name, info.compress_type))
        if info.file_size != info.compress_size:
            fail("%s 的 size 字段与实际不符" % name)
        with open(root + cid, "rb") as f:
            src = f.read()
        data = zf.read(name)
        if data != src:
            fail("%s 内容与源文件不一致（%d vs %d 字节）" % (name, len(data), len(src)))
        if info.CRC != (zipfile.crc32(data) & 0xFFFFFFFF):
            fail("%s 头里的 CRC 与实算不符" % name)
        total += len(data)

    if zf.testzip() is not None:
        fail("第二次 testzip 仍报坏条目")
    zf.close()
    print("  verify zip OK: %d entries, %d bytes, STORED, CRC 全对, 顶层文件夹=%s"
          % (len(expected), total, list(folders)[0]))


def main():
    if len(sys.argv) < 2:
        fail("用法见文件头")
    mode = sys.argv[1]
    if mode == "json":
        if len(sys.argv) != 4:
            fail("json 模式要 3 个参数")
        check_json(sys.argv[2], int(sys.argv[3]))
    elif mode == "zip":
        if len(sys.argv) < 7:
            fail("zip 模式要 ≥6 个参数（zip 根目录 tid 游戏名 至少一个 content_path）")
        check_zip(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], sys.argv[6:])
    else:
        fail("未知模式 %r" % mode)


if __name__ == "__main__":
    main()
