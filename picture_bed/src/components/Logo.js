import React from 'react';

/**
 * LinkCloud Logo — Obsidian 黑曜石风格
 * 一颗紫色六边形宝石：上半面冷光，下半面深紫，中央高光裂纹。
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
      style={{ display: 'block', filter: glow ? `drop-shadow(0 4px 12px rgba(124,92,255,0.45))` : 'none' }}
    >
      <defs>
        {/* 上半面：冷紫高光 */}
        <linearGradient id={`top-${id}`} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#C4B5FD" />
          <stop offset="55%" stopColor="#8B6CFF" />
          <stop offset="100%" stopColor="#6E4FF0" />
        </linearGradient>
        {/* 下半面：深紫黑曜 */}
        <linearGradient id={`bot-${id}`} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#4B2EBF" />
          <stop offset="100%" stopColor="#2A1466" />
        </linearGradient>
        {/* 侧面：中等紫 */}
        <linearGradient id={`side-${id}`} x1="0" y1="0" x2="1" y2="1">
          <stop offset="0%" stopColor="#6A4CD9" />
          <stop offset="100%" stopColor="#3D1FA0" />
        </linearGradient>
        {/* 中央高光裂纹 */}
        <linearGradient id={`spark-${id}`} x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="#FFFFFF" stopOpacity="0.95" />
          <stop offset="100%" stopColor="#FFFFFF" stopOpacity="0.15" />
        </linearGradient>
      </defs>

      {/* 六边形宝石外形（顶点上下尖） */}
      {/* 顶部三角面 */}
      <polygon points="24,4 42,16 24,22 6,16" fill={`url(#top-${id})`} />
      {/* 底部倒三角面 */}
      <polygon points="24,22 42,16 24,44 6,16" fill={`url(#bot-${id})`} />
      {/* 左侧斜面 */}
      <polygon points="6,16 24,22 24,44" fill={`url(#side-${id})`} opacity="0.85" />
      {/* 右侧斜面 */}
      <polygon points="42,16 24,22 24,44" fill={`url(#side-${id})`} opacity="0.65" />

      {/* 中央高光裂纹 */}
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
