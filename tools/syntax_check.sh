#!/bin/bash
# [CN-FORK] v0.5.2.137 单编语法检查：只对本次改动的 C 文件跑一遍真机 CFLAGS。
# 目的与之前那次一样：不动全量构建，先把语法/声明错挡在推真机之前。
set -u
cd /mnt/d/pldmanager/ps5-payload-manager || exit 1
SDKT=/opt/ps5-payload-sdk/target
INC="-Iinclude -I$SDKT/include -Isrc/psaccounts -Isrc/psaccounts/third_party -Ithird_party/sqlite3"
CC=/opt/ps5-payload-sdk/bin/prospero-clang
rc=0
for f in src/game_title.c src/http_server.c src/main.c; do
  echo "--- $f ---"
  out=$("$CC" -Os -Wall -ffunction-sections -fdata-sections $INC -fsyntax-only "$f" 2>&1)
  code=$?
  if [ -n "$out" ]; then echo "$out" | head -30; fi
  echo "    exit=$code"
  [ "$code" -ne 0 ] && rc=1
done
echo "TOTAL_RC=$rc"
exit $rc
