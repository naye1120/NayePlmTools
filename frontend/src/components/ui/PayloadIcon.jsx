import React, { useState } from 'react';
import { cn } from '../../utils/helpers';
import { Package } from 'lucide-react';

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

  // Fallback
  const hash = stringToHash(name || 'Payload');
  const [colorStart, colorEnd] = COLORS[hash % COLORS.length];

  return (
    <div 
      className={cn("rounded-2xl flex items-center justify-center border border-white/10 shrink-0 text-white relative overflow-hidden", className)}
      style={{
        background: `linear-gradient(135deg, ${colorStart}, ${colorEnd})`
      }}
    >
      <Package className="w-1/2 h-1/2 opacity-90" />
    </div>
  );
};

export default PayloadIcon;
