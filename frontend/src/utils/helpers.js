import { clsx } from 'clsx'
import { twMerge } from 'tailwind-merge'

/* [CN-FORK] v0.5.2.98 允许用 ?device=ps5 在电脑/手机上预览电视端形态（遥控器空间导航、
 * 大命中区、隐藏底部栏）。真机 UA 本来就命中，不看这个参数；线上地址没人带这个 query。
 * v0.6.0 上游给这两个常量补了 `typeof navigator !== 'undefined'` 守卫（Node 侧 import
 * 这个模块时不会一上来就抛 ReferenceError）—— 纯健壮性增强，采纳。 */
export const isPS5 = typeof navigator !== 'undefined' && (/PlayStation/i.test(navigator.userAgent) ||
  /[?&]device=ps5(&|$)/.test(typeof window !== 'undefined' ? window.location.search : ''));
export const isIOS = typeof navigator !== 'undefined' && (/iPad|iPhone|iPod/.test(navigator.userAgent) || (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1));

export function cn(...inputs) {
  return twMerge(clsx(inputs))
}

export const getBaseName = (filename) => {
  if (!filename || typeof filename !== 'string') return '';
  let clean = filename.split('/').pop().replace(/\.(elf|bin)$/i, '');
  const versionMatch = clean.match(/[_\s.-]v?(\d+[\d.a-z-]*)/i);
  if (versionMatch) {
    clean = clean.replace(versionMatch[0], '');
  }
  return clean.replace(/[_\s.-]ps[45]$/i, '').toLowerCase().trim();
};

export const parsePayloadName = (path) => {
  if (!path || typeof path !== 'string') return { displayName: '', version: null, isDelay: false };
  if (path.startsWith('!')) {
    const ms = parseInt(path.substring(1));
    return { displayName: `Delay (${ms / 1000}s)`, version: null, isDelay: true, delaySeconds: ms / 1000 };
  }

  let name = path.split('/').pop().replace(/\.(elf|bin)$/i, '');

  // Try to find version pattern like _v1.0, -v1.0, _1.0, .v1.0, or space separated
  const versionMatch = name.match(/[_\s.-](v?\d+[\d.a-z-]*)/i);
  let version = null;

  if (versionMatch) {
    version = versionMatch[1];
    name = name.replace(versionMatch[0], '');
  }

  name = name.replace(/[_\s.-]ps[45]$/i, '');

  return {
    displayName: name.replace(/_/g, ' ').replace(/-/g, ' ').trim(),
    version: version,
    isDelay: false
  };
};

export const isSystemPayload = (filename) => {
  if (!filename || typeof filename !== 'string') return false;
  const name = filename.split('/').pop().toLowerCase();
  return (
    name.startsWith('pldmgr') ||
    name.includes('payload-manager') ||
    name.includes('payload_manager') ||
    name.includes('payload manager') ||
    name.includes('payloadmanager') ||
    name === 'pldmgr'
  );
};

export const formatVersionTag = (version, maxLen = 8) => {
  if (!version || typeof version !== 'string') return '';
  const trimmed = version.trim();
  if (trimmed.length <= maxLen) return trimmed;
  const keepCount = Math.max(1, maxLen - 2);
  return `...${trimmed.slice(-keepCount)}`;
};

export const formatSourceTag = (sourceName, maxLen = 20) => {
  if (!sourceName || typeof sourceName !== 'string') return '';
  const trimmed = sourceName.trim();
  if (trimmed.length <= maxLen) return trimmed;
  return `${trimmed.slice(0, maxLen - 3)}...`;
};

export const cleanDisplayName = (name, version) => {
  if (!name || typeof name !== 'string') return '';
  let cleaned = name.trim();
  if (version) {
    try {
      const bareVer = String(version).replace(/^v/i, '').replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
      const verSuffixRegex = new RegExp(`[\\s_\\.-]+v?${bareVer}$`, 'i');
      cleaned = cleaned.replace(verSuffixRegex, '').trim();
    } catch {
      /* ignore regex error if any */
    }
  }
  return cleaned;
};
