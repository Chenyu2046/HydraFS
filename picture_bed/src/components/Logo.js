import React from 'react';

/**
 * LinkCloud Logo
 * 一枚橄榄色知识节点印章：外层代表分布式存储，内层代表双链关系。
 * 纯 SVG，跟随 currentColor 不生效（自带渐变），可通过 size 控制大小。
 */
const Logo = ({ size = 28, glow = true, title = 'LinkCloud' }) => {
  const id = React.useId();
  const w = size;
  const h = size;
  return (
    <svg
      width={w}
      height={h}
      viewBox="0 0 48 48"
      fill="none"
      xmlns="http://www.w3.org/2000/svg"
      role="img"
      aria-label={title}
      style={{ display: 'block', filter: glow ? `drop-shadow(0 4px 12px rgba(51,92,58,0.38))` : 'none' }}
    >
      <defs>
        <linearGradient id={`top-${id}`} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#A8B37A" />
          <stop offset="55%" stopColor="#5F7A45" />
          <stop offset="100%" stopColor="#335C3A" />
        </linearGradient>
        <linearGradient id={`bot-${id}`} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#3F5F3B" />
          <stop offset="100%" stopColor="#1F3324" />
        </linearGradient>
        <linearGradient id={`side-${id}`} x1="0" y1="0" x2="1" y2="1">
          <stop offset="0%" stopColor="#C58A36" />
          <stop offset="100%" stopColor="#7A4D1E" />
        </linearGradient>
        <linearGradient id={`spark-${id}`} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#FFFFFF" stopOpacity="0.95" />
          <stop offset="100%" stopColor="#FFFFFF" stopOpacity="0.15" />
        </linearGradient>
      </defs>

      <polygon points="24,4 42,16 24,22 6,16" fill={`url(#top-${id})`} />
      <polygon points="24,22 42,16 24,44 6,16" fill={`url(#bot-${id})`} />
      <polygon points="6,16 24,22 24,44" fill={`url(#side-${id})`} opacity="0.85" />
      <polygon points="42,16 24,22 24,44" fill={`url(#side-${id})`} opacity="0.65" />

      <polygon points="24,4 26,16 24,22 22,16" fill={`url(#spark-${id})`} opacity="0.85" />

      {/* 外描边 */}
      <polygon
        points="24,4 42,16 24,44 6,16"
        fill="none"
        stroke="rgba(255,255,255,0.25)"
        strokeWidth="0.75"
        strokeLinejoin="round"
      />
      {/* 腰线 */}
      <polyline
        points="6,16 24,22 42,16"
        fill="none"
        stroke="rgba(255,255,255,0.35)"
        strokeWidth="0.6"
        strokeLinejoin="round"
      />
    </svg>
  );
};

export default Logo;
