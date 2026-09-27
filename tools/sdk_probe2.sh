#!/bin/bash
echo '=== sdk samples ==='
curl -s 'https://api.github.com/repos/ps5-payload-dev/sdk/git/trees/master?recursive=1' -o /tmp/sdktree.json
grep -oE '"path": ?"samples/[^"]*"' /tmp/sdktree.json | head -40
echo '=== websrv files ==='
curl -s 'https://api.github.com/repos/ps5-payload-dev/websrv/git/trees/master?recursive=1' -o /tmp/websrv.json
grep -oE '"path": ?"[^"]*"' /tmp/websrv.json | head -40
echo '=== y2ton/hw or serial search on github code (via search page may 401) ==='
grep -i -E 'sysctl|hw_model|serial' /tmp/sdktree.json | head -5
