#!/usr/bin/env python3
# [CN-FORK] v0.5.2.115 test/media_verify.py — 给 test/media_scan_test.c 的产出做机器校验。
#
# C 侧没法验 zip（仓库里没有 zlib），这里用标准库 zipfile 把关：
#   逐条 CRC（testzip）、内容与源文件逐字节一致、条目名与 content_path 的映射对得上、
#   压缩方式必须是 STORED（我们自己写的 zip，没有 deflate）。
#
# 用法：
#   media_verify.py json <list.json> <期望 count>
#   media_verify.py zip  <pack.zip> <夹具根目录> <content_path> [<content_path>…]

import json
import re
import sys
import zipfile

ZIP_PREFIX = "/av_contents/"  # zip 内条目名 = content_path 去掉这段


def fail(msg):
    print("  VERIFY FAIL: %s" % msg)
    sys.exit(1)


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


def check_zip(path, root, ids):
    expected = []
    for cid in ids:
        if not cid.startswith(ZIP_PREFIX):
            fail("期望的 id 不是 content_path: %r" % cid)
        expected.append(cid[len(ZIP_PREFIX):])
    expected.sort()

    try:
        zf = zipfile.ZipFile(path, "r")
    except Exception as e:
        fail("zip 打不开: %s" % e)

    bad = zf.testzip()
    if bad is not None:
        fail("CRC 校验失败的条目: %r" % bad)

    got = sorted(zf.namelist())
    if got != expected:
        fail("条目不符\n    期望 %r\n    实际 %r" % (expected, got))

    total = 0
    for cid in ids:
        name = cid[len(ZIP_PREFIX):]
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
    print("  verify zip OK: %d entries, %d bytes, STORED, CRC 全对" % (len(expected), total))


def main():
    if len(sys.argv) < 2:
        fail("用法见文件头")
    mode = sys.argv[1]
    if mode == "json":
        if len(sys.argv) != 4:
            fail("json 模式要 3 个参数")
        check_json(sys.argv[2], int(sys.argv[3]))
    elif mode == "zip":
        if len(sys.argv) < 5:
            fail("zip 模式要 ≥4 个参数")
        check_zip(sys.argv[2], sys.argv[3], sys.argv[4:])
    else:
        fail("未知模式 %r" % mode)


if __name__ == "__main__":
    main()
