import { ConfigProvider } from 'antd';

export const theme = {
  token: {
    colorPrimary: '#2563EB',
    colorSuccess: '#059669',
    colorWarning: '#D97706',
    colorError: '#DC2626',
    colorInfo: '#2563EB',
    colorTextBase: '#0F172A',
    colorBgBase: '#FFFFFF',
    borderRadius: 10,
    fontFamily: `'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif`,
    fontSize: 14,
    lineHeight: 1.6,
    controlHeight: 38,
    boxShadow: '0 1px 3px rgba(0,0,0,0.06), 0 1px 2px rgba(0,0,0,0.04)',
    wireframe: false,
  },
  components: {
    Button: {
      borderRadius: 8,
      controlHeight: 38,
      paddingContentHorizontal: 20,
      fontWeight: 500,
    },
    Card: {
      borderRadius: 12,
      padding: 24,
    },
    Input: {
      borderRadius: 8,
      controlHeight: 38,
    },
    Table: {
      borderRadius: 12,
      headerBg: '#F8FAFC',
      headerColor: '#475569',
      rowHoverBg: '#F1F5F9',
    },
    Menu: {
      itemBorderRadius: 8,
      itemHeight: 38,
    },
    Tag: {
      borderRadius: 6,
    },
    Modal: {
      borderRadius: 16,
    },
  },
};
