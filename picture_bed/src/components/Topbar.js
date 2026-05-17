import React from 'react';
import styled from '@emotion/styled';
import { Tooltip, Dropdown } from 'antd';
import { useNavigate } from 'react-router-dom';
import { SunOutlined, MoonOutlined, LogoutOutlined, UserOutlined } from '@ant-design/icons';
import { useAuth } from '../contexts/AuthContext';
import { useThemeMode } from '../contexts/ThemeContext';

const Bar = styled.header`
  position: sticky; top: 0; z-index: 40;
  height: 56px;
  display: flex; align-items: center;
  padding: 0 24px;
  background: ${p => p.$transparent ? 'transparent' : p.theme.colors.bg + 'E6'};
  backdrop-filter: ${p => p.$transparent ? 'none' : 'saturate(160%) blur(12px)'};
  border-bottom: 1px solid ${p => p.$transparent ? 'transparent' : p.theme.colors.border};
  transition: background 200ms ease, border-color 200ms ease;
`;

const Crumbs = styled.div`
  display: flex; align-items: center; gap: 8px;
  color: ${p => p.theme.colors.text2};
  font-size: 13px;
`;

const Brand = styled.span`
  color: ${p => p.theme.colors.text};
  font-weight: 600;
  letter-spacing: -0.2px;
`;

const Sep = styled.span`color: ${p => p.theme.colors.text3};`;

const Right = styled.div`
  margin-left: auto;
  display: flex; align-items: center; gap: 8px;
`;

const IconBtn = styled.button`
  width: 36px; height: 36px;
  display: grid; place-items: center;
  border-radius: 8px;
  border: 1px solid ${p => p.theme.colors.border};
  background: ${p => p.theme.colors.panel};
  color: ${p => p.theme.colors.text2};
  cursor: pointer;
  transition: all ${p => p.theme.duration.base} ${p => p.theme.ease.out};
  &:hover {
    color: ${p => p.theme.colors.text};
    border-color: ${p => p.theme.colors.borderStrong};
    background: ${p => p.theme.colors.panelHover};
  }
`;

const UserChip = styled.button`
  display: inline-flex; align-items: center; gap: 8px;
  padding: 6px 10px 6px 6px;
  border-radius: 999px;
  border: 1px solid ${p => p.theme.colors.border};
  background: ${p => p.theme.colors.panel};
  color: ${p => p.theme.colors.text};
  font-size: 13px;
  cursor: pointer;
  transition: all ${p => p.theme.duration.base} ${p => p.theme.ease.out};
  &:hover { background: ${p => p.theme.colors.panelHover}; border-color: ${p => p.theme.colors.borderStrong}; }
`;

const Avatar = styled.span`
  width: 24px; height: 24px;
  border-radius: 999px;
  background: linear-gradient(135deg, ${p => p.theme.colors.accent}, ${p => p.theme.colors.accentHover});
  color: #fff;
  font-size: 11px; font-weight: 700;
  display: grid; place-items: center;
`;

const Topbar = ({ crumbs = [], transparent = false }) => {
  const { mode, toggle } = useThemeMode();
  const { user, logout } = useAuth();
  const nav = useNavigate();

  const handleLogout = () => { logout(); nav('/login'); };

  const menu = {
    items: [
      { key: 'profile', icon: <UserOutlined />, label: user?.username || 'Account', disabled: true },
      { type: 'divider' },
      { key: 'logout', icon: <LogoutOutlined />, label: '退出登录', danger: true, onClick: handleLogout },
    ],
  };

  return (
    <Bar $transparent={transparent}>
      <Crumbs style={transparent ? { visibility: 'hidden' } : undefined}>
        <Brand>HydraFS</Brand>
        {crumbs.map((c, i) => (
          <React.Fragment key={i}>
            <Sep>/</Sep>
            <span style={{ color: i === crumbs.length - 1 ? undefined : 'inherit' }}>{c}</span>
          </React.Fragment>
        ))}
      </Crumbs>
      <Right>
        <Tooltip title={mode === 'dark' ? '切换到浅色' : '切换到深色'}>
          <IconBtn onClick={toggle} aria-label="toggle theme">
            {mode === 'dark' ? <SunOutlined /> : <MoonOutlined />}
          </IconBtn>
        </Tooltip>
        {user && (
          <Dropdown menu={menu} placement="bottomRight" trigger={['click']}>
            <UserChip>
              <Avatar>{(user.username || 'U').slice(0, 1).toUpperCase()}</Avatar>
              <span>{user.username}</span>
            </UserChip>
          </Dropdown>
        )}
      </Right>
    </Bar>
  );
};

export default Topbar;
