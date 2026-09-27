#!/bin/bash
F='/mnt/c/Users/wugua/.qoder/cache/projects/pldmanager-7229bc1e/agent-tools/5ad731a3/67f0911d.txt'
grep -oE '"full_name": *"[^"]+"' "$F" | head -10
echo '=== search 2: system_version ps5 payload ==='
curl -s 'https://api.github.com/search/repositories?q=ps5+sysversion' | grep -E '"full_name"|"description"' | head
echo '=== search 3: repos code hw.model via search users? skip ==='
true
