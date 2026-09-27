import React from 'react'

// [CN-FORK] 首页/顶栏 logo 改用统一应用图标位图（frontend/public/icon.png），
// 与 PS5 桌面图标、浏览器 favicon 保持同一套美术。
export default function LogoIcon({ className = "w-8 h-8" }) {
  return (
    <img
      src="/icon.png?v=0.5.1.12"
      alt="NayePlmTools"
      className={className}
      draggable={false}
    />
  )
}
