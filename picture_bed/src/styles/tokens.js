/**
 * LinkCloud Design Tokens
 * 单一来源：颜色 / 间距 / 字号 / 圆角 / 阴影 / 动效 / 字体
 * 不要在组件里写死颜色，所有颜色走 useTheme().colors
 */

const space = {
  0: '0px', 1: '4px', 2: '8px', 3: '12px', 4: '16px',
  5: '24px', 6: '32px', 7: '48px', 8: '64px', 9: '96px',
};

const radius = {
  xs: '4px', sm: '6px', md: '8px', lg: '12px', xl: '16px', xxl: '28px', pill: '999px',
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
  sans: `'Source Sans 3', -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif`,
  heading: `'Lexend', 'Source Sans 3', sans-serif`,
  mono: `'JetBrains Mono', ui-monospace, SFMono-Regular, Menlo, Consolas, monospace`,
};

// ========== Dark (default) ==========
const dark = {
  name: 'dark',
  colors: {
    bg: '#171713',
    bgElevated: '#202019',
    panel: '#202019',
    panel2: '#29281F',
    panelHover: '#312F24',
    border: '#373527',
    borderStrong: '#514D38',
    text: '#F3EEDF',
    text2: '#B8AE97',
    text3: '#817762',
    accent: '#3F5F3B',
    accentHover: '#547A4E',
    accentSoft: 'rgba(91, 127, 77, 0.18)',
    accentBorder: 'rgba(123, 155, 101, 0.42)',
    success: '#6F8F57',
    warn: '#D18A2F',
    danger: '#B05A45',
    info: '#A36F2C',
    // 图谱节点配色（按文件类别）
    graphDoc: '#8C7A4A',
    graphImage: '#7A8F5B',
    graphCode: '#B8644A',
    graphArchive: '#C58A36',
    graphOther: '#9A927E',
    graphEdge: 'rgba(243,238,223,0.18)',
    graphEdgeHover: '#C58A36',
    // 杂项
    overlay: 'rgba(0,0,0,0.6)',
    scrollbar: '#514D38',
    grid: 'rgba(243,238,223,0.045)',
    canvasUmber:    'rgba(124, 83, 45, 0.20)',
    canvasAmber:    'rgba(211, 148, 57, 0.16)',
    canvasMoss:     'rgba(91, 127, 77, 0.18)',
    canvasCream:    'rgba(245, 229, 188, 0.10)',
    canvasBase:     '#1C1B15',
    chromeBg:       'rgba(32, 32, 25, 0.76)',
    chromeBorder:   'rgba(243, 238, 223, 0.10)',
  },
  shadow: {
    sm: '0 1px 2px rgba(0,0,0,0.4)',
    md: '0 4px 16px rgba(0,0,0,0.35)',
    lg: '0 12px 32px rgba(0,0,0,0.45)',
    glow: '0 0 0 1px rgba(123,155,101,0.34), 0 8px 24px rgba(91,127,77,0.18)',
    float: '0 24px 60px -20px rgba(0,0,0,0.6), 0 8px 20px -8px rgba(0,0,0,0.4)',
  },
};

// ========== Light ==========
const light = {
  name: 'light',
  colors: {
    bg: '#F7F0DF',
    bgElevated: '#FFF9EA',
    panel: '#FFF9EA',
    panel2: '#EFE4CB',
    panelHover: '#E9DDC0',
    border: '#DED0B2',
    borderStrong: '#BFAE86',
    text: '#211F18',
    text2: '#5E5848',
    text3: '#8A806A',
    accent: '#335C3A',
    accentHover: '#244B2D',
    accentSoft: 'rgba(51, 92, 58, 0.11)',
    accentBorder: 'rgba(51, 92, 58, 0.30)',
    success: '#5E7D46',
    warn: '#B86F18',
    danger: '#A84935',
    info: '#9A6426',
    graphDoc: '#8B6F37',
    graphImage: '#5F7A45',
    graphCode: '#B75C43',
    graphArchive: '#C17A1E',
    graphOther: '#7B725E',
    graphEdge: 'rgba(0,0,0,0.18)',
    graphEdgeHover: '#B86F18',
    overlay: 'rgba(0,0,0,0.45)',
    scrollbar: '#BFAE86',
    grid: 'rgba(33,31,24,0.045)',
    canvasUmber:    '#E8C08D',
    canvasAmber:    '#F5DCA7',
    canvasMoss:     '#D7E0C2',
    canvasCream:    '#FFF3D5',
    canvasBase:     '#FFF9EA',
    chromeBg:       'rgba(255, 249, 234, 0.88)',
    chromeBorder:   'rgba(33, 31, 24, 0.10)',
  },
  shadow: {
    sm: '0 1px 2px rgba(33,31,24,0.06)',
    md: '0 4px 16px rgba(33,31,24,0.07)',
    lg: '0 12px 32px rgba(33,31,24,0.12)',
    glow: '0 0 0 1px rgba(51,92,58,0.26), 0 8px 24px rgba(51,92,58,0.12)',
    float: '0 24px 60px -20px rgba(33,31,24,0.18), 0 8px 20px -8px rgba(33,31,24,0.10)',
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
