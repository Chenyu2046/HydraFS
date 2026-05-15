import React, { createContext, useContext, useEffect, useMemo, useState, useCallback } from 'react';
import { ThemeProvider as EmotionThemeProvider } from '@emotion/react';
import { ConfigProvider, theme as antdTheme } from 'antd';
import { themes, tokens, buildAntdTheme } from '../styles/tokens';

const STORAGE_KEY = 'hydra.theme';

const ThemeCtx = createContext({
  mode: 'dark',
  toggle: () => {},
  setMode: () => {},
});

export const useThemeMode = () => useContext(ThemeCtx);

export const ThemeProvider = ({ children }) => {
  const [mode, setMode] = useState(() => {
    try { return localStorage.getItem(STORAGE_KEY) || 'dark'; } catch { return 'dark'; }
  });

  useEffect(() => {
    try { localStorage.setItem(STORAGE_KEY, mode); } catch {}
    document.documentElement.dataset.theme = mode;
    document.documentElement.style.colorScheme = mode;
  }, [mode]);

  const toggle = useCallback(() => setMode(m => (m === 'dark' ? 'light' : 'dark')), []);

  const emotionTheme = useMemo(() => ({
    mode,
    ...themes[mode],
    ...tokens,
  }), [mode]);

  const antdConfig = useMemo(() => ({
    ...buildAntdTheme(mode),
    algorithm: mode === 'dark' ? antdTheme.darkAlgorithm : antdTheme.defaultAlgorithm,
  }), [mode]);

  const ctxValue = useMemo(() => ({ mode, toggle, setMode }), [mode, toggle]);

  return (
    <ThemeCtx.Provider value={ctxValue}>
      <ConfigProvider theme={antdConfig}>
        <EmotionThemeProvider theme={emotionTheme}>
          {children}
        </EmotionThemeProvider>
      </ConfigProvider>
    </ThemeCtx.Provider>
  );
};
