#!/bin/bash
T=/opt/ps5-payload-sdk/target
echo '=== kenv.h? ==='
ls "$T/include/kenv.h" 2>&1
echo '=== AppInstUtil all symbols ==='
nm -D --defined-only "$T/lib/libSceAppInstUtil.so" | awk '{print $3}' | head -80
echo '=== SystemService Get* symbols ==='
nm -D --defined-only "$T/lib/libSceSystemService.so" | awk '{print $3}' | grep -i -E 'get' | head -40
echo '=== SysCore all ==='
nm -D --defined-only "$T/lib/libSceSysCore.so" | awk '{print $3}' | head -40
