#!/bin/bash
# [CN-FORK] v0.5.2.138 静态守卫：gr_dispatch_post 分发的每条 POST 路由，
# 都必须在 gr_post_limit 里登记过请求体上限。
#
# 为什么值得单列一条守卫（`.137` 的教训）：这两张表是**分开写**的 ——
# gr_post_limit 决定「收不收请求体」，gr_dispatch_post 决定「收到之后怎么处理」。
# 只加后者不加前者的后果**不是** 404 / 500，而是：请求体压根不收 → 那个
# PostStatus 累加器不建 → gr_dispatch_post 永不触发 → 客户端看到的是
# **连接被直接关掉**（python 侧 RemoteDisconnected）。真机上极难从现象反推回
# 这里，而 mock 里根本没有这一层，214 项前端回归照样全绿 —— 所以用静态检查钉死。
#
# 用法：bash tools/check_post_routes.sh [源文件路径]（默认 src/http_server.c；
# 传别的路径是为了做负向对照 —— 拿一份被故意改坏的副本跑，确认它真会红）
set -u
cd "$(dirname "$0")/.."
SRC="${1:-src/http_server.c}"

# 取一个函数体：从含 <pat> 的那一行起，到第一个**顶格**的 '}' 为止。
body() {
    awk -v pat="$1" '
        !inb && index($0, pat) { inb = 1 }
        inb { print }
        inb && /^}/ { exit }
    ' "$SRC"
}

disp=$(body 'static int gr_dispatch_post' | grep -o 'ROUTE_[A-Z_]*' | sort -u)
lim=$(body 'static size_t gr_post_limit' | grep -o 'ROUTE_[A-Z_]*' | sort -u)

echo "=== gr_dispatch_post 分发的路由（$(echo "$disp" | wc -l) 条）==="
echo "$disp" | sed 's/^/  /'
echo "=== gr_post_limit 登记的路由（$(echo "$lim" | wc -l) 条）==="
echo "$lim" | sed 's/^/  /'

missing=$(comm -23 <(echo "$disp") <(echo "$lim"))
if [ -n "$missing" ]; then
    echo
    echo "!! 下列路由在 gr_dispatch_post 里分发，却没在 gr_post_limit 登记："
    echo "$missing" | sed 's/^/     /'
    echo "   后果：请求体不收 → gr_dispatch_post 永不触发 → 客户端连接被直接关掉。"
    exit 1
fi

extra=$(comm -13 <(echo "$disp") <(echo "$lim"))
if [ -n "$extra" ]; then
    echo
    echo "   （提示：限长表里这些路由没有对应的分发分支，可能是历史残留）"
    echo "$extra" | sed 's/^/     /'
fi

echo
echo "OK  分发器里的每条路由都已在限长表登记"
exit 0
