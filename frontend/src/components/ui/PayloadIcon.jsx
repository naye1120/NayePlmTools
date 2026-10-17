import React, { useState } from 'react';
import { cn } from '../../utils/helpers';

const COLORS = [
  ['#3b82f6', '#1d4ed8'], // blue
  ['#10b981', '#047857'], // emerald
  ['#8b5cf6', '#6d28d9'], // violet
  ['#f59e0b', '#b45309'], // amber
  ['#ef4444', '#b91c1c'], // red
  ['#ec4899', '#be185d'], // pink
  ['#14b8a6', '#0f766e'], // teal
];

const stringToHash = (str) => {
  let hash = 0;
  if (!str) return 0;
  for (let i = 0; i < str.length; i++) {
    hash = str.charCodeAt(i) + ((hash << 5) - hash);
  }
  return Math.abs(hash);
};

const PayloadIcon = ({ icon, name, className }) => {
  const [failedIcon, setFailedIcon] = useState(null);
  const isDataUri = typeof icon === 'string' && icon.startsWith('data:image/');

  if (isDataUri && failedIcon !== icon) {
    return (
      <div className={cn("rounded-2xl overflow-hidden border border-white/10 shrink-0 bg-white/5", className)}>
        <img
          src={icon}
          alt={name || 'Payload'}
          className="w-full h-full object-cover"
          onError={() => setFailedIcon(icon)}
        />
      </div>
    );
  }

  // [CN-FORK] 兜底：仓库没有真图标时，用「名字第一个字」染色块替代原来的立方体。
  // Array.from 保证 CJK / 代理对也能取到真正的第一个字，而不是半个字符。
  const hash = stringToHash(name || 'Payload');
  const [colorStart, colorEnd] = COLORS[hash % COLORS.length];
  const trimmed = typeof name === 'string' ? name.trim() : '';
  const initial = trimmed ? Array.from(trimmed)[0].toUpperCase() : '?';

  return (
    <div 
      className={cn("rounded-2xl flex items-center justify-center border border-white/10 shrink-0 text-white font-bold relative overflow-hidden select-none", className)}
      style={{
        background: `linear-gradient(135deg, ${colorStart}, ${colorEnd})`
      }}
    >
      {initial}
    </div>
  );
};

export default PayloadIcon;
