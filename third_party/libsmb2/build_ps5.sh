#!/usr/bin/env bash
# 用 PS5 payload SDK 交叉编译 libsmb2，产物落到 ./ps5/{lib,include}
#
# 用法（WSL 里）：bash build_ps5.sh
#
# 三条踩过的坑：
#   1. 构建目录别放 /tmp —— WSL 的 /tmp 是 tmpfs，空闲重启会清空
#   2. 必须用 ${CMAKE}（prospero-cmake，它会设 CMAKE_TOOLCHAIN_FILE），
#      系统的 cmake 编出来是宿主机的
#   3. source prospero.sh 不往 PATH 加东西，prospero-clang 直接敲会 command not found，
#      要用 ${CC} / ${NM} 这些变量
set -e

SDK=${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}
WORK=${WORK:-/root/libsmb2-ps5}
HERE=$(cd "$(dirname "$0")" && pwd)

mkdir -p "$WORK" && cd "$WORK"
[ -f libsmb2-6.2.tar.gz ] || cp "$HERE/libsmb2-6.2.tar.gz" .

# 与 pacbrew-repo 的 libsmb2/PKGBUILD 逐字一致
echo "8e1f9efc6b2e0f6546f0fe121ac0ddf4fc2f0908ae5a6bd1f185be7c9e0bcbb3  libsmb2-6.2.tar.gz" \
  | sha256sum -c -

rm -rf libsmb2-libsmb2-6.2
tar xzf libsmb2-6.2.tar.gz

source "$SDK/toolchain/prospero.sh"
cd libsmb2-libsmb2-6.2
${CMAKE} -DCMAKE_BUILD_TYPE=Release -B build -S .
make -C build -j8

mkdir -p "$HERE/ps5/lib" "$HERE/ps5/include"
cp build/lib/libsmb2.a "$HERE/ps5/lib/"
rm -rf "$HERE/ps5/include/smb2"
cp -r include/smb2 "$HERE/ps5/include/"

echo
echo "OK  $HERE/ps5/lib/libsmb2.a"
ls -l "$HERE/ps5/lib/libsmb2.a"
