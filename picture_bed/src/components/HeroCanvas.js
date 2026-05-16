import React from 'react';
import styled from '@emotion/styled';

/**
 * HeroCanvas
 * Base44 风格的 pastel gradient 大画布（仅用于 Overview 首页）
 * - 圆角 28px、占满内容区
 * - 不再内嵌 nav；导航统一走 Sidebar（设计原则：单一导航源）
 * - 子组件分两栏（左 hero 文案 / 右 mockup）
 */

const Canvas = styled.section`
  position: relative;
  margin: 16px 16px 0;
  padding: 56px 56px 56px;
  border-radius: 28px;
  overflow: hidden;
  background: ${p => p.theme.colors.canvasBase};
  box-shadow: ${p => p.theme.shadow.float};
  isolation: isolate;

  /* 三层 pastel radial gradient 叠出空灵感 */
  &::before {
    content: '';
    position: absolute; inset: 0;
    background:
      radial-gradient(ellipse 60% 80% at 12% 18%, ${p => p.theme.colors.pastelLavender} 0%, transparent 60%),
      radial-gradient(ellipse 70% 60% at 88% 80%, ${p => p.theme.colors.pastelPeach} 0%, transparent 65%),
      radial-gradient(ellipse 50% 50% at 70% 8%, ${p => p.theme.colors.pastelMint} 0%, transparent 60%),
      radial-gradient(ellipse 40% 40% at 18% 92%, ${p => p.theme.colors.pastelIce} 0%, transparent 55%);
    z-index: -2;
  }
  /* 极淡的网格让画布有"产品 mockup canvas"质感 */
  &::after {
    content: '';
    position: absolute; inset: 0;
    background-image:
      linear-gradient(${p => p.theme.colors.grid} 1px, transparent 1px),
      linear-gradient(90deg, ${p => p.theme.colors.grid} 1px, transparent 1px);
    background-size: 32px 32px;
    mask-image: radial-gradient(ellipse 80% 70% at 50% 50%, #000 40%, transparent 90%);
    -webkit-mask-image: radial-gradient(ellipse 80% 70% at 50% 50%, #000 40%, transparent 90%);
    z-index: -1;
    pointer-events: none;
  }

  @media (max-width: 980px) {
    padding: 64px 24px 36px;
    margin: 12px 12px 0;
    border-radius: 22px;
  }
`;

const Inner = styled.div`
  position: relative;
  display: grid;
  grid-template-columns: 1.05fr 1fr;
  gap: 40px;
  align-items: center;
  max-width: 1320px;
  margin: 0 auto;

  @media (max-width: 980px) {
    grid-template-columns: 1fr;
    gap: 28px;
  }
`;

/* ===== window chrome（包装 MiniGraph 让它像真实产品截图） ===== */
const Window = styled.div`
  border-radius: 16px;
  overflow: hidden;
  background: ${p => p.theme.colors.panel};
  border: 1px solid ${p => p.theme.colors.border};
  box-shadow: ${p => p.theme.shadow.float};

  .titlebar {
    display: flex; align-items: center; gap: 12px;
    padding: 10px 14px;
    border-bottom: 1px solid ${p => p.theme.colors.border};
    background: ${p => p.theme.colors.panel2};
    .dots { display: inline-flex; gap: 6px; }
    .dots i {
      width: 11px; height: 11px; border-radius: 999px;
      background: #ff5f57;
      &:nth-of-type(2) { background: #febc2e; }
      &:nth-of-type(3) { background: #28c840; }
    }
    .url {
      flex: 1;
      text-align: center;
      font-family: ${p => p.theme.fontFamily.mono};
      font-size: 11.5px;
      color: ${p => p.theme.colors.text2};
      padding: 4px 10px;
      border-radius: 999px;
      background: ${p => p.theme.colors.panel};
      border: 1px solid ${p => p.theme.colors.border};
      max-width: 340px;
      margin: 0 auto;
      white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
    }
    .badge {
      font-size: 10.5px; font-weight: 700;
      letter-spacing: 0.5px;
      padding: 3px 8px;
      border-radius: 999px;
      background: ${p => p.theme.colors.accentSoft};
      color: ${p => p.theme.colors.accent};
      font-family: ${p => p.theme.fontFamily.mono};
      i {
        display: inline-block; width: 5px; height: 5px;
        border-radius: 999px; margin-right: 5px;
        background: ${p => p.theme.colors.accent};
        animation: pulse 1.6s ease-in-out infinite;
      }
    }
  }
  .body { position: relative; }
  @keyframes pulse {
    0%, 100% { opacity: 1; transform: scale(1); }
    50% { opacity: 0.4; transform: scale(0.7); }
  }
`;

export const HeroCanvas = ({ children }) => (
  <Canvas>
    <Inner>{children}</Inner>
  </Canvas>
);

export const ProductWindow = ({ url = 'hydrafs.app/graph', live = true, children }) => (
  <Window>
    <div className="titlebar">
      <span className="dots"><i /><i /><i /></span>
      <span className="url">{url}</span>
      {live && <span className="badge"><i />LIVE</span>}
    </div>
    <div className="body">{children}</div>
  </Window>
);

export default HeroCanvas;
