#!/bin/bash
echo '=== repo search: hardware info ==='
curl -s 'https://api.github.com/search/repositories?q=ps5+hardware+info' | grep -E '"full_name"|"description"' | head -20
echo '=== repo search: sysinfo/ps5 ==='
curl -s 'https://api.github.com/search/repositories?q=ps5+payload+sysinfo' | grep -E '"full_name"|"description"' | head -10
echo '=== code search via grep.app ==='
curl -s 'https://grep.app/api/search?q=hw.model&filter[lang][0]=C' | head -c 2000
