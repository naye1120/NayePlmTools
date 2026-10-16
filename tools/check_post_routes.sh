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

# 第三张表（v0.6.0.177 加）：is_post_route —— 方法闸门。**这就是当年 405 的根因**：
# 这三张表分开写，只补 gr_dispatch_post + gr_post_limit 而漏了方法闸门的话，
# 请求体收得到、分支也写了，但闸门把 POST 当成 GET 拒掉，前端看到的就是
# 「好几个功能无缘无故 405」，而且 mock 层（mock-policy.js）同样只抄了上游那 14 条，
# 214 项回归照样全绿。三张表必须一起过，静态钉死。
disp=$(body 'static int gr_dispatch_post' | grep -o 'ROUTE_[A-Z_]*' | sort -u)
lim=$(body 'static size_t gr_post_limit' | grep -o 'ROUTE_[A-Z_]*' | sort -u)
gate=$(body 'static int is_post_route' | grep -o 'ROUTE_[A-Z_]*' | sort -u)

echo "=== gr_dispatch_post 分发的路由（$(echo "$disp" | wc -l) 条）==="
echo "$disp" | sed 's/^/  /'
echo "=== gr_post_limit 登记的路由（$(echo "$lim" | wc -l) 条）==="
echo "$lim" | sed 's/^/  /'
echo "=== is_post_route 方法闸门放行的路由（$(echo "$gate" | wc -l) 条）==="
echo "$gate" | sed 's/^/  /'

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

# ① 收了体却不放行方法 = 收完白收（405）
nogate=$(comm -23 <(echo "$lim") <(echo "$gate"))
# ② 分发了却不放行方法 = 分支永远跑不到（405，比 ① 更隐蔽，因为两表看着都齐）
nogate2=$(comm -23 <(echo "$disp") <(echo "$gate"))
nogate=$(printf '%s\n%s\n' "$nogate" "$nogate2" | sed '/^$/d' | sort -u)
if [ -n "$nogate" ]; then
    echo
    echo "!! 下列路由能收请求体 / 有分发分支，却没在 is_post_route 里放行："
    echo "$nogate" | sed 's/^/     /'
    echo "   后果：方法闸门强制 GET 而前端发 POST → 405（v0.6.0.177 那批 bug）。"
    exit 1
fi

echo
echo "OK  三张表一致：限长表 / 分发器里的每条路由都已在方法闸门登记"
exit 0
