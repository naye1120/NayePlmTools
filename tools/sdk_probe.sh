#!/bin/bash
SDK=/opt/ps5-payload-sdk/target
echo '=== ps5 include dir ==='
ls "$SDK/include/ps5/"
echo '=== find Sce lib headers ==='
find "$SDK/include" -iname 'libSce*' 2>/dev/null | head -30
for f in libSceSysCore libSceSystemService libSceIpmi libSceRegMgr libSceAppInstUtil; do
  echo "--- $f.so symbols (filtered):"
  nm -D --defined-only "$SDK/lib/$f.so" 2>/dev/null | grep -i -E 'version|serial|model|target|fw|seed' | head -15
done
echo '=== kernel_get_fw_version in libs ==='
grep -rn 'kernel_get_fw_version' "$SDK" 2>/dev/null | head
echo '=== sdk repo tree (filtered) ==='
curl -s 'https://api.github.com/repos/ps5-payload-dev/sdk/git/trees/master?recursive=1' | grep -oE '"path": "[^"]*"' | grep -i -E 'kernel|crt|sysdbg|sys_proc|version|payload' | head -50
