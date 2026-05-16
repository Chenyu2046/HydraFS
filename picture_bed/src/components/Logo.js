/**
 * HydraFS Logo — 方向 A · 黑曜石折叠节点
 *
 * 设计语义：
 *  - 倾斜的多边形 = 一个"被折叠的文件"
 *  - 内部 3 个节点 + 连接线 = knowledge graph
 *  - 斜向高光 = 黑曜石光泽
 *
 * 可选 props:
 *  size:        number  默认 24
 *  withWordmark boolean 是否带 HydraFS 文字
 *  tone:        'auto'|'mono'|'gradient'  色调
 */

import React from 'react';
import styled from '@emotion/styled';

const Wrap = styled.span`
  display: inline-flex;
  align-items: center;
  gap: 10px;
  line-height: 1;
  user-select: none;
`;

const Wordmark = styled.span`
  font-weight: 700;
  font-size: ${p => p.$size}px;
  letter-spacing: -0.4px;
  color: ${p => p.theme.colors.text};
  font-family: ${p => p.theme.fontFamily?.sans || 'inherit'};
  /* 让 Hydra 的 H 与 FS 之间有视觉重音 */
  & b { color: ${p => p.theme.colors.text}; }
  & em {
    font-style: normal;
    color: ${p => p.theme.colors.text2};
    font-weight: 600;
  }
`;

/**
 * SVG 黑曜石折叠节点
 * - 24x24 viewBox
 * - 外形：六边形（hexagon），左上→右下渐变模拟黑曜石折射
 * - 内部 3 节点 + 2 条连接线：上、左下、右下
 * - 一道斜向高光（white opacity 0.18）
 */
const Mark = ({ size = 24, tone = 'auto' }) => {
  const gradId = React.useId();
  const glowId = React.useId();
  return (
    <svg
      width={size}
      height={size}
      viewBox="0 0 24 24"
      fill="none"
      xmlns="http://www.w3.org/2000/svg"
      aria-hidden="true"
      style={{ display: 'block' }}
    >
      <defs>
        <linearGradient id={gradId} x1="3" y1="2" x2="21" y2="22" gradientUnits="userSpaceOnUse">
          {tone === 'mono' ? (
            <>
              <stop offset="0%" stopColor="currentColor" stopOpacity="0.92" />
              <stop offset="100%" stopColor="currentColor" stopOpacity="0.78" />
            </>
          ) : (
            <>
              <stop offset="0%" stopColor="#1E1B4B" />
              <stop offset="55%" stopColor="#3730A3" />
              <stop offset="100%" stopColor="#6366F1" />
            </>
          )}
        </linearGradient>
        <linearGradient id={glowId} x1="6" y1="4" x2="18" y2="14" gradientUnits="userSpaceOnUse">
          <stop offset="0%" stopColor="#FFFFFF" stopOpacity="0" />
          <stop offset="50%" stopColor="#FFFFFF" stopOpacity="0.38" />
          <stop offset="100%" stopColor="#FFFFFF" stopOpacity="0" />
        </linearGradient>
      </defs>

      {/* 主体六边形（折叠的文件） */}
      <path
        d="M12 1.6 L21.2 6.8 V17.2 L12 22.4 L2.8 17.2 V6.8 Z"
        fill={`url(#${gradId})`}
      />

      {/* 内部折角细线（暗示"折叠" / 切面） */}
      <path
        d="M12 1.6 V22.4 M2.8 6.8 L21.2 17.2"
        stroke="#FFFFFF"
        strokeOpacity="0.08"
        strokeWidth="0.8"
      />

      {/* 黑曜石斜向高光 */}
      <path
        d="M12 1.6 L21.2 6.8 V11 L12 6 Z"
        fill={`url(#${glowId})`}
      />

      {/* 内部 knowledge graph：3 节点 + 2 边 */}
      {/* 节点位置：上(12,7.5) 左下(8.5,14.5) 右下(15.5,14.5) */}
      <line x1="12" y1="7.5" x2="8.5" y2="14.5" stroke="#A5B4FC" strokeOpacity="0.85" strokeWidth="0.9" strokeLinecap="round" />
      <line x1="12" y1="7.5" x2="15.5" y2="14.5" stroke="#A5B4FC" strokeOpacity="0.85" strokeWidth="0.9" strokeLinecap="round" />
      <line x1="8.5" y1="14.5" x2="15.5" y2="14.5" stroke="#A5B4FC" strokeOpacity="0.45" strokeWidth="0.9" strokeLinecap="round" strokeDasharray="1.2 1.6" />

      <circle cx="12" cy="7.5" r="1.7" fill="#F5F3FF" />
      <circle cx="8.5" cy="14.5" r="1.4" fill="#C7D2FE" />
      <circle cx="15.5" cy="14.5" r="1.4" fill="#C7D2FE" />

      {/* 节点内的高光小点 */}
      <circle cx="11.55" cy="7.05" r="0.45" fill="#FFFFFF" opacity="0.9" />
    </svg>
  );
};

const Logo = ({
  size = 24,
  withWordmark = false,
  wordmarkSize,
  tone = 'auto',
  onClick,
  style,
  className,
}) => {
  const ws = wordmarkSize || Math.round(size * 0.78);
  return (
    <Wrap onClick={onClick} style={style} className={className}>
      <Mark size={size} tone={tone} />
      {withWordmark && (
        <Wordmark $size={ws}>
          <b>Hydra</b><em>FS</em>
        </Wordmark>
      )}
    </Wrap>
  );
};

export default Logo;
