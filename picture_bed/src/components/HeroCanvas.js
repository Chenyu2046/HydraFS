import React from 'react';
import styled from '@emotion/styled';
import { NavLink } from 'react-router-dom';
import Logo from './Logo';

/**
 * HeroCanvas
 * 克制的知识云产品画布
 * - 圆角 28px、占满内容区
 * - 顶部内置 floating pill nav
 * - 子组件分两栏（左 hero 文案 / 右 mockup）
 *
 * 视觉 only。所有跳转用 NavLink 走真实路由。
 */

const Canvas = styled.section`
  position: relative;
  margin: 16px 16px 0;
  padding: 80px 56px 56px;
  border-radius: 28px;
  overflow: hidden;
  background: ${p => p.theme.colors.canvasBase};
  box-shadow: ${p => p.theme.shadow.float};
  isolation: isolate;

  &::before {
    content: '';
    position: absolute; inset: 0;
    background:
      radial-gradient(ellipse 60% 80% at 12% 18%, ${p => p.theme.colors.canvasMoss} 0%, transparent 60%),
      radial-gradient(ellipse 70% 60% at 88% 80%, ${p => p.theme.colors.canvasAmber} 0%, transparent 65%),
      radial-gradient(ellipse 50% 50% at 70% 8%, ${p => p.theme.colors.canvasCream} 0%, transparent 60%),
      radial-gradient(ellipse 40% 40% at 18% 92%, ${p => p.theme.colors.canvasUmber} 0%, transparent 55%);
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

/* ===== Floating pill nav（装饰 + 真实跳转） ===== */
const PillNav = styled.nav`
  position: absolute;
  top: 18px; left: 50%;
  transform: translateX(-50%);
  display: flex; align-items: center; gap: 4px;
  padding: 6px;
  border-radius: 999px;
  background: ${p => p.theme.colors.chromeBg};
  border: 1px solid ${p => p.theme.colors.chromeBorder};
  backdrop-filter: blur(20px) saturate(180%);
  -webkit-backdrop-filter: blur(20px) saturate(180%);
  box-shadow: ${p => p.theme.shadow.md};
  z-index: 5;

  .brand {
    display: inline-flex; align-items: center; gap: 8px;
    padding: 6px 14px 6px 8px;
    color: ${p => p.theme.colors.text};
    font-weight: 700;
    font-size: 13px;
    letter-spacing: -0.2px;
    text-decoration: none;
    .dot {
      width: 22px; height: 22px;
      border-radius: 7px;
      background: linear-gradient(135deg, ${p => p.theme.colors.accent}, ${p => p.theme.colors.accentHover});
      display: grid; place-items: center;
      color: #fff; font-size: 11px; font-weight: 800;
    }
  }

  a.link {
    padding: 7px 14px;
    border-radius: 999px;
    color: ${p => p.theme.colors.text2};
    font-size: 13px;
    text-decoration: none;
    transition: all 160ms ease;
    &:hover { color: ${p => p.theme.colors.text}; background: ${p => p.theme.colors.panelHover}; }
    &.active { color: ${p => p.theme.colors.text}; background: ${p => p.theme.colors.panel2}; }
  }

  .cta {
    margin-left: 6px;
    padding: 7px 16px;
    border-radius: 999px;
    background: ${p => p.theme.colors.text};
    color: ${p => p.theme.colors.bg};
    font-size: 13px; font-weight: 600;
    text-decoration: none;
    transition: opacity 160ms ease;
    &:hover { opacity: 0.85; }
  }

  @media (max-width: 880px) {
    .link { display: none; }
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

const NAV_ITEMS = [
  { to: '/files',     label: '文件管理' },
  { to: '/knowledge', label: 'AI Wiki' },
  { to: '/graph',     label: '知识图谱' },
  { to: '/shared',    label: '分享管理' },
];

export const HeroCanvas = ({ children }) => (
  <Canvas>
    <PillNav>
      <NavLink to="/" className="brand">
        <span className="dot" style={{ background: 'transparent', padding: 0 }}>
          <Logo size={20} glow={false} />
        </span>
        LinkCloud
      </NavLink>
      {NAV_ITEMS.map(it => (
        <NavLink key={it.to} to={it.to}
          className={({ isActive }) => 'link' + (isActive ? ' active' : '')}>
          {it.label}
        </NavLink>
      ))}
      <NavLink to="/files" className="cta">进入工作台</NavLink>
    </PillNav>
    <Inner>{children}</Inner>
  </Canvas>
);

export const ProductWindow = ({ url = 'linkcloud.app/graph', live = true, children }) => (
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
