#!/bin/bash
F='/mnt/c/Users/wugua/.qoder/cache/projects/pldmanager-7229bc1e/agent-tools/5ad731a3/f4fff4fa.txt'
grep -oE '"path": *"[^"]+"' "$F" | grep -v node_modules | grep -iE 'sys|info|stat|hw|main|www|api|src' | head -60
