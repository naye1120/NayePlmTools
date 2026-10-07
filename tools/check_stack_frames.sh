#!/bin/bash
# [CN-FORK] v0.5.2.141 静态守卫：**别把大块内存放在栈上**。
#
# 为什么值得单列一条守卫（这是本工程栽过两次的同一个坑）：
#   `.116` 改磁贴图标 / 背景 → 死机   （tt_apply 里一个 unsigned char[65536]）
#   `.141` 点「立即备份」    → 死机   （gs_copy_file 里一个 char buf[32*1024]）
# 两次都**只在真机上炸、宿主测试全绿**，因为真机的 HTTP 是
# MHD_USE_THREAD_PER_CONNECTION —— 每条连接一条 pthread 线程，栈只有 64KB 上下
# （宿主上量出来的临界点：整条链超过 56KB 就 SIGSEGV），而宿主测试跑在主线程的
# 8MB 栈上，这点数组根本不构成压力。
#
# 所以规则很简单：**任何函数的栈帧都不许超过 32KB**。真需要更大的缓冲就走堆
# （gs_copy_file 现在就是这么做的：缓冲区挂在 gs_cp_t 上，由调用方 malloc）。
#
# 说明：这是一道**粗网**，不是精确的防线 —— 它只能抓「单个函数就吃掉几十 KB」
# 这一种形态。真正的防线是 test/game_saves_test.c 里那条「在 32KB 线程栈上跑完
# 整条快照链」的用例（精确到调用链，且带负向对照）。两条一起才完整。
#
# 用法：bash tools/check_stack_frames.sh [阈值字节，默认 32768]
set -u
cd "$(dirname "$0")/.."

LIMIT="${1:-32768}"
SDK="${PS5_SDK:-/opt/ps5-payload-sdk}"

# 没有 SDK 头就整段跳过（与宿主套件里「缺 python3 就跳」同一个处理）：
# http_server.c 那批文件要 microhttpd.h，主机上只有 SDK 里那份。
if [ ! -f "$SDK/target/include/microhttpd.h" ]; then
    echo "=== check_stack_frames SKIPPED: 找不到 $SDK/target/include/microhttpd.h ==="
    exit 0
fi
command -v clang >/dev/null 2>&1 || { echo "=== check_stack_frames SKIPPED: 没有 clang ==="; exit 0; }

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

INC="-Iinclude -I$SDK/target/include -Isrc/psaccounts -Isrc/psaccounts/third_party -Ithird_party/sqlite3"
for f in src/*.c src/psaccounts/*.c; do
    clang -Os -w -fstack-usage $INC -c "$f" -o "$OUT/$(basename "$f" .c).o" >/dev/null 2>&1
done

# .su 的格式：<file>:<line>:<function>\t<bytes>\t<qualifier>
cat "$OUT"/*.su 2>/dev/null | awk -F'\t' -v lim="$LIMIT" '$2 + 0 > lim { print $2 "\t" $1 }' \
    | sort -rn > "$OUT/over.txt"

n=$(wc -l < "$OUT/over.txt" | tr -d ' ')
if [ "$n" = "0" ]; then
    echo "=== check_stack_frames: 全部函数栈帧 <= ${LIMIT} B ==="
    exit 0
fi

echo "=== check_stack_frames: $n 个函数栈帧超过 ${LIMIT} B ==="
# 第一列是字节数，第二列是 file:line:function
sed 's/^/  /' "$OUT/over.txt"
echo
echo "把这些缓冲挪到堆上（参考 src/game_saves.c 的 gs_cp_t：调用方 malloc 一次，"
echo "整条调用链共用）。真机每条连接线程只有 64KB 栈，栈上放几十 KB 必死机。"
exit 1
