import { ConfigProvider } from 'antd';

export const theme = {
  token: {
    colorPrimary: '#007AFF',
    colorSuccess: '#34C759',
    colorWarning: '#FF9500',
    colorError: '#FF3B30',
    colorInfo: '#007AFF',
    colorTextBase: '#1D1D1F',
    colorBgBase: '#FFFFFF',
    colorBgLayout: '#F5F5F7',
    borderRadius: 12,
    fontFamily: `-apple-system, BlinkMacSystemFont, 'SF Pro Text', 'Segoe UI', Roboto, sans-serif`,
    fontSize: 14,
    lineHeight: 1.6,
    controlHeight: 40,
    boxShadow: '0 4px 20px rgba(0,0,0,0.05)',
    wireframe: false,
  },
  components: {
    Button: {
      borderRadius: 20,
      controlHeight: 40,
      paddingContentHorizontal: 20,
      fontWeight: 600,
      boxShadow: 'none',
    },
    Card: {
      borderRadius: 20,
      padding: 24,
      colorBorderSecondary: 'transparent',
      boxShadowTertiary: '0 8px 30px rgba(0,0,0,0.04)',
    },
    Input: {
      borderRadius: 12,
      controlHeight: 40,
      colorBgContainer: '#F2F2F7',
      colorBorder: 'transparent',
    },
    Table: {
      borderRadius: 16,
      headerBg: '#F5F5F7',
      headerColor: '#86868B',
      rowHoverBg: '#F5F5F7',
    },
    Menu: {
      itemBorderRadius: 10,
      itemHeight: 40,
    },
    Tag: {
      borderRadius: 6,
    },
    Modal: {
      borderRadius: 20,
    },
  },
};
