#!/bin/bash
curl -s -o /tmp/nid.c https://raw.githubusercontent.com/ps5-payload-dev/sdk/master/crt/nid.c
echo "size: $(wc -c < /tmp/nid.c)"
grep -n -i -E 'proc_command|sysdbg|sys_proc|target_info|psid' /tmp/nid.c | head -20
echo '=== all SYS_ names (first 80) ==='
grep -oE 'SYS_[A-Za-z0-9_]+' /tmp/nid.c | sort -u | head -80
