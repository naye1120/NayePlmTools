import React, { useRef, useState, useEffect } from 'react'
import { Zap, Usb } from 'lucide-react'
import { cn, parsePayloadName, cleanDisplayName } from '../../utils/helpers'
import { useTranslation } from 'react-i18next'

const PayloadName = ({ path = '', version: repoVersion, className, versionClassName, stacked = false, hideIcon = false, lastUpdate = null, hideVersion = false, centered = false, displayName: metaDisplayName = null }) => {
  const { t } = useTranslation();
  const safePath = typeof path === 'string' ? path : '';
  let { displayName, version, isDelay, delaySeconds } = parsePayloadName(safePath);
  const isUsb = Boolean(safePath && safePath.startsWith('/mnt/usb'));

  if (metaDisplayName) displayName = metaDisplayName;

  const activeVersion = repoVersion || version;
  if (displayName && activeVersion) {
    displayName = cleanDisplayName(displayName, activeVersion);
  }

  if (repoVersion) version = repoVersion;
  if (hideVersion) version = null;
  if (isDelay) {
    displayName = t("autoload.delay_item", "Delay ({{seconds}}s)", { seconds: delaySeconds || 1 });
  }

  const containerRef = useRef(null);
  const textRef = useRef(null);
  const [scrollAmount, setScrollAmount] = useState(0);

  useEffect(() => {
    const checkOverflow = () => {
      if (containerRef.current && textRef.current) {
        const overflow = textRef.current.scrollWidth - containerRef.current.clientWidth;
        setScrollAmount(overflow > 0 ? overflow : 0);
      }
    };
    
    // Check initially and on resize
    checkOverflow();
    window.addEventListener('resize', checkOverflow);
    return () => window.removeEventListener('resize', checkOverflow);
  }, [displayName]);

  return (
    <div className={cn("flex min-w-0 flex-1 w-full", stacked ? "flex-col items-stretch" : "items-center space-x-3", className)}>
      <div className="flex items-center space-x-2 min-w-0 flex-1">
        {isDelay && !hideIcon && <Zap className="w-4 h-4 text-ps-blue shrink-0" />}
        {isUsb && !hideIcon && <Usb className="w-5 h-5 text-ps-blue shrink-0 mr-1" />}
        {/* [CN-FORK] v0.6.0 合入：保留上游的「静态截断 + hover/focus 跑马灯露出全名」结构，
            但把 fork v0.5.2.9x 定下的 text-xl 加回两处文字节点 —— fork 的口径是
            「各页面（StorageHub/Autoload/Sources/加载遮罩）条目名字号完全统一，
            不受调用方 wrapper 字号影响」。上层没有 .group 时跑马灯不触发，
            表现与原来的纯截断一致，属于严格增强。 */}
        <div
          className="overflow-hidden min-w-0 flex-1 relative"
          ref={containerRef}
        >
          {/* Static truncated text with ellipsis */}
          <div
             className={cn(
               "truncate text-xl font-bold leading-tight transition-opacity",
               centered ? "text-center" : "text-left",
               scrollAmount > 0 ? "group-hover:opacity-0 group-focus-within:opacity-0" : ""
             )}
          >
            {displayName}
          </div>

          {/* Scrolling text overlay (only visible on hover/focus if overflowing) */}
          <div
            ref={textRef}
            className={cn(
              "absolute top-0 left-0 text-xl font-bold text-left leading-tight whitespace-nowrap inline-block transition-opacity",
              scrollAmount > 0 ? "opacity-0 group-hover:opacity-100 group-focus-within:opacity-100 animate-marquee" : "opacity-0 pointer-events-none"
            )}
            style={{ '--scroll-amount': `${scrollAmount}px` }}
          >
            {displayName}
          </div>
        </div>
      </div>
      {(version || lastUpdate) && (
        <div className={cn("flex items-center", stacked ? "mt-1" : "")}>
          {version && (
            <span className={cn(
              stacked
                ? "text-[11px] font-bold tracking-wider text-ps-blue opacity-90"
                : "text-[10px] px-2 py-0.5 bg-ps-blue/10 text-ps-blue font-bold rounded-md border border-ps-blue/20 shrink-0",
              versionClassName)}>
              {version}
            </span>
          )}
          {version && lastUpdate && (
            <span className="text-[11px] text-zinc-600 mx-2">•</span>
          )}
          {lastUpdate && (
            <span className="text-[11px] font-bold tracking-wider text-zinc-500 uppercase">
              {lastUpdate}
            </span>
          )}
        </div>
      )}
    </div>
  );
};

export default PayloadName
