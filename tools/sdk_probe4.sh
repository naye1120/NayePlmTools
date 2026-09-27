#!/bin/bash
F='/mnt/c/Users/wugua/.qoder/cache/projects/pldmanager-7229bc1e/agent-tools/5ad731a3/f64183cc.txt'
echo '=== property-style strings (XX/yy) ==='
grep -oE '"[A-Z][A-Z_0-9]+/[A-Za-z0-9_]+"' "$F" | sort -u | head -60
echo '=== version strings ==='
grep -oE '"[A-Za-z_/.-]*[Vv]ersion[A-Za-z_/.-]*"' "$F" | sort -u | head -30
echo '=== how it reads: function calls ==='
grep -nE 'sceRegMgr|RegMgr|ProcessProperty|GetDriverReg|LoadTable' "$F" | head -20
