#!/bin/bash
F='/mnt/c/Users/wugua/.qoder/cache/projects/pldmanager-7229bc1e/agent-tools/5ad731a3/b567aa07.txt'
grep -oE '"name": *"[^"]+"' "$F" | head -20
echo '=== etaHEN main repo: look for toolbox/hwinfo files ==='
curl -s 'https://api.github.com/search/code?q=repo:etaHEN/etaHEN+kenv' -H 'Accept: application/vnd.github+json' | head -c 300
