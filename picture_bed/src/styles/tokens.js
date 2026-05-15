/**
 * HydraFS Design Tokens
 * 单一来源：颜色 / 间距 / 字号 / 圆角 / 阴影 / 动效 / 字体
 * 不要在组件里写死颜色，所有颜色走 useTheme().colors
 */

const space = {
  0: '0px', 1: '4px', 2: '8px', 3: '12px', 4: '16px',
  5: '24px', 6: '32px', 7: '48px', 8: '64px', 9: '96px',
};

const radius = {
  xs: '4px', sm: '6px', md: '8px', lg: '12px', xl: '16px', pill: '999px',
};

const fontSize = {
  micro: '11px', caption: '12px', body: '13px', bodyLg: '14px',
  h4: '15px', h3: '17px', h2: '20px', h1: '26px', display: '36px',
};

const fontWeight = { regular: 400, medium: 500, semibold: 600, bold: 700 };

const lineHeight = { tight: 1.25, normal: 1.5, relaxed: 1.65 };

const ease = {
  out: 'cubic-bezier(0.16, 1, 0.3, 1)',
  inOut: 'cubic-bezier(0.45, 0, 0.55, 1)',
};
const duration = { fast: '120ms', base: '200ms', slow: '320ms' };

const fontFamily = {
  sans: `'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif`,
  mono: `'JetBrains Mono', ui-monospace, SFMono-Regular, Menlo, Consolas, monospace`,
};

// ========== Dark (default) ==========
const dark = {
  name: 'dark',
  colors: {
    bg: '#0B0B0F',
    bgElevated: '#111114',
    panel: '#111114',
    panel2: '#16161B',
    panelHover: '#1A1A20',
    border: '#1F1F25',
    borderStrong: '#2A2A33',
    text: '#EDEDEF',
    text2: '#8A8A93',
    text3: '#5A5A63',
    accent: '#7C5CFF',
    accentHover: '#9277FF',
    accentSoft: 'rgba(124, 92, 255, 0.12)',
    accentBorder: 'rgba(124, 92, 255, 0.35)',
    success: '#3FB950',
    warn: '#F0883E',
    danger: '#F85149',
    info: '#58A6FF',
    // 图谱节点配色（按文件类别）
    graphDoc: '#7DD3FC',
    graphImage: '#C4B5FD',
    graphCode: '#FCA5A5',
    graphArchive: '#FBBF24',
    graphOther: '#94A3B8',
    graphEdge: 'rgba(255,255,255,0.18)',
    graphEdgeHover: '#7C5CFF',
    // 杂项
    overlay: 'rgba(0,0,0,0.6)',
    scrollbar: '#2A2A33',
    grid: 'rgba(255,255,255,0.04)',
  },
  shadow: {
    sm: '0 1px 2px rgba(0,0,0,0.4)',
    md: '0 4px 16px rgba(0,0,0,0.35)',
    lg: '0 12px 32px rgba(0,0,0,0.45)',
    glow: '0 0 0 1px rgba(124,92,255,0.35), 0 8px 24px rgba(124,92,255,0.18)',
  },
};

// ========== Light ==========
const light = {
  name: 'light',
  colors: {
    bg: '#FAFAF7',
    bgElevated: '#FFFFFF',
    panel: '#FFFFFF',
    panel2: '#F4F4EE',
    panelHover: '#F0F0EA',
    border: '#EAEAE3',
    borderStrong: '#D8D8D0',
    text: '#1A1A1A',
    text2: '#5A5A5A',
    text3: '#999999',
    accent: '#5B5BD6',
    accentHover: '#4848B8',
    accentSoft: 'rgba(91, 91, 214, 0.10)',
    accentBorder: 'rgba(91, 91, 214, 0.30)',
    success: '#1A7F37',
    warn: '#9A6700',
    danger: '#CF222E',
    info: '#0969DA',
    graphDoc: '#0284C7',
    graphImage: '#7C3AED',
    graphCode: '#DC2626',
    graphArchive: '#B45309',
    graphOther: '#64748B',
    graphEdge: 'rgba(0,0,0,0.18)',
    graphEdgeHover: '#5B5BD6',
    overlay: 'rgba(0,0,0,0.45)',
    scrollbar: '#D8D8D0',
    grid: 'rgba(0,0,0,0.04)',
  },
  shadow: {
    sm: '0 1px 2px rgba(15,23,42,0.06)',
    md: '0 4px 16px rgba(15,23,42,0.06)',
    lg: '0 12px 32px rgba(15,23,42,0.10)',
    glow: '0 0 0 1px rgba(91,91,214,0.30), 0 8px 24px rgba(91,91,214,0.12)',
  },
};

export const tokens = {
  space, radius, fontSize, fontWeight, lineHeight,
  ease, duration, fontFamily,
};

export const themes = { dark, light };

/**
 * 把 emotion theme 映射到 antd v5 token
 */
export const buildAntdTheme = (mode) => {
  const t = themes[mode] || dark;
  const c = t.colors;
  return {
    token: {
      colorPrimary: c.accent,
      colorSuccess: c.success,
      colorWarning: c.warn,
      colorError: c.danger,
      colorInfo: c.info,
      colorBgBase: c.bg,
      colorBgLayout: c.bg,
      colorBgContainer: c.panel,
      colorBgElevated: c.bgElevated,
      colorBorder: c.border,
      colorBorderSecondary: c.border,
      colorText: c.text,
      colorTextSecondary: c.text2,
      colorTextTertiary: c.text3,
      colorTextQuaternary: c.text3,
      borderRadius: 8,
      borderRadiusLG: 12,
      borderRadiusSM: 6,
      fontFamily: fontFamily.sans,
      fontSize: 13,
      controlHeight: 36,
      wireframe: false,
    },
    components: {
      Button: { borderRadius: 8, controlHeight: 36, fontWeight: 500, primaryShadow: 'none', defaultShadow: 'none' },
      Card: { borderRadiusLG: 12, paddingLG: 20, colorBorderSecondary: c.border },
      Input: { borderRadius: 8, controlHeight: 36, colorBgContainer: c.panel2, activeBorderColor: c.accent, hoverBorderColor: c.borderStrong },
      Table: { borderRadius: 12, headerBg: c.panel2, headerColor: c.text2, rowHoverBg: c.panelHover, colorBorderSecondary: c.border },
      Menu: { itemBorderRadius: 8, itemHeight: 36, itemSelectedBg: c.accentSoft, itemSelectedColor: c.accent },
      Tag: { borderRadiusSM: 6, defaultBg: c.panel2, defaultColor: c.text2 },
      Modal: { borderRadiusLG: 12, contentBg: c.panel, headerBg: c.panel, footerBg: c.panel },
      Drawer: { colorBgElevated: c.panel },
      Tooltip: { colorBgSpotlight: c.bgElevated, colorTextLightSolid: c.text },
      Tabs: { itemSelectedColor: c.text, itemHoverColor: c.text, inkBarColor: c.accent, itemColor: c.text2 },
      Progress: { defaultColor: c.accent },
      Dropdown: { colorBgElevated: c.bgElevated },
      Empty: { colorTextDisabled: c.text3 },
      Skeleton: { color: c.panel2, colorGradientEnd: c.panelHover },
      Segmented: { itemSelectedBg: c.panel, itemSelectedColor: c.text, trackBg: c.panel2 },
    },
  };
};
