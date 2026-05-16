import React from 'react';
import styled from '@emotion/styled';
import { Tooltip, Dropdown } from 'antd';
import { NavLink, useNavigate, useLocation } from 'react-router-dom';
import {
  SunOutlined, MoonOutlined, LogoutOutlined, UserOutlined,
  SearchOutlined,
} from '@ant-design/icons';
import { useAuth } from '../contexts/AuthContext';
import { useThemeMode } from '../contexts/ThemeContext';
import Logo from './Logo';

/* ============================================================
 * TopNav — HydraFS 顶部主导航（合并旧 Topbar）
 *  · 左：Logo + Wordmark
 *  · 中：主导航 5 项 + 当前路由高亮
 *  · 右：⌘K 搜索占位 + 主题切换 + 用户头像下拉
 *  · 透明模式（首页 Hero）：底边线隐去
 * ============================================================ */

const Bar = styled.header`
  position: sticky; top: 0; z-index: 40;
  height: 60px;
  display: flex; align-items: center;
  gap: 28px;
  padding: 0 28px;
  background: ${p => p.$transparent ? 'transparent' : p.theme.colors.bg + 'E6'};
  backdrop-filter: ${p => p.$transparent ? 'saturate(180%) blur(10px)' : 'saturate(160%) blur(12px)'};
  border-bottom: 1px solid ${p => p.$transparent ? 'transparent' : p.theme.colors.border};
  transition: background ${p => p.theme.duration.base} ${p => p.theme.ease.out},
              border-color ${p => p.theme.duration.base} ${p => p.theme.ease.out};

  @media (max-width: 768px) {
    height: 56px;
    padding: 0 16px;
    gap: 12px;
  }
`;

const BrandLink = styled.button`
  background: none; border: 0; padding: 0; cursor: pointer;
  display: inline-flex; align-items: center;
  border-radius: 8px;
  flex-shrink: 0;
  transition: opacity ${p => p.theme.duration.fast} ${p => p.theme.ease.out};
  &:hover { opacity: 0.82; }
  &:focus-visible { outline: 2px solid ${p => p.theme.colors.accent}; outline-offset: 4px; }
`;

const NavRow = styled.nav`
  display: flex; align-items: center; gap: 2px;
  height: 36px;
  padding: 4px;
  border-radius: 999px;
  background: ${p => p.theme.colors.chromeBg || p.theme.colors.panel};
  border: 1px solid ${p => p.theme.colors.chromeBorder || p.theme.colors.border};
  backdrop-filter: blur(10px);

  @media (max-width: 880px) { display: none; }
`;

const NavItem = styled(NavLink)`
  position: relative;
  display: inline-flex; align-items: center; gap: 6px;
  height: 28px;
  padding: 0 12px;
  border-radius: 999px;
  font-size: 13px;
  font-weight: 500;
  color: ${p => p.theme.colors.text2};
  text-decoration: none;
  transition: color ${p => p.theme.duration.fast} ${p => p.theme.ease.out},
              background ${p => p.theme.duration.fast} ${p => p.theme.ease.out};

  &:hover { color: ${p => p.theme.colors.text}; }
  &.active {
    color: ${p => p.theme.colors.text};
    background: ${p => p.theme.colors.panel2};
    box-shadow: inset 0 0 0 1px ${p => p.theme.colors.border};
  }
`;

const Right = styled.div`
  margin-left: auto;
  display: flex; align-items: center; gap: 8px;
  flex-shrink: 0;
`;

const SearchPill = styled.button`
  display: inline-flex; align-items: center; gap: 8px;
  height: 36px;
  padding: 0 14px 0 12px;
  border-radius: 999px;
  background: ${p => p.theme.colors.panel};
  border: 1px solid ${p => p.theme.colors.border};
  color: ${p => p.theme.colors.text3};
  font-size: 12.5px;
  cursor: pointer;
  transition: all ${p => p.theme.duration.base} ${p => p.theme.ease.out};
  font-family: inherit;

  kbd {
    display: inline-grid; place-items: center;
    min-width: 18px; height: 18px;
    padding: 0 4px;
    border-radius: 4px;
    background: ${p => p.theme.colors.panel2};
    border: 1px solid ${p => p.theme.colors.border};
    color: ${p => p.theme.colors.text2};
    font-size: 10.5px;
    font-family: ${p => p.theme.fontFamily?.mono || 'monospace'};
    letter-spacing: 0;
  }

  &:hover {
    color: ${p => p.theme.colors.text2};
    border-color: ${p => p.theme.colors.borderStrong};
    background: ${p => p.theme.colors.panelHover};
  }

  @media (max-width: 640px) { display: none; }
`;

const IconBtn = styled.button`
  width: 36px; height: 36px;
  display: grid; place-items: center;
  border-radius: 999px;
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
  padding: 5px 12px 5px 5px;
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
  width: 26px; height: 26px;
  border-radius: 999px;
  background: linear-gradient(135deg, #3730A3 0%, #6366F1 100%);
  color: #fff;
  font-size: 11px; font-weight: 700;
  display: grid; place-items: center;
`;

const NAV = [
  { to: '/', label: 'Overview' },
  { to: '/files', label: 'Files' },
  { to: '/knowledge', label: 'Knowledge' },
  { to: '/graph', label: 'Graph' },
  { to: '/shared', label: 'Shared' },
];

// `crumbs` 在新 IA 下不再需要，但保留 prop 兼容签名
const Topbar = ({ transparent = false /* , crumbs */ }) => {
  const { mode, toggle } = useThemeMode();
  const { user, logout } = useAuth();
  const nav = useNavigate();
  const loc = useLocation();

  const handleLogout = () => { logout(); nav('/login'); };

  const menu = {
    items: [
      { key: 'profile', icon: <UserOutlined />, label: user?.username || 'Account', disabled: true },
      { type: 'divider' },
      { key: 'logout', icon: <LogoutOutlined />, label: '退出登录', danger: true, onClick: handleLogout },
    ],
  };

  // 让 / 仅在精确匹配时高亮
  const isActive = (to) => {
    if (to === '/') return loc.pathname === '/';
    return loc.pathname === to || loc.pathname.startsWith(to + '/');
  };

  return (
    <Bar $transparent={transparent}>
      <BrandLink onClick={() => nav('/')} aria-label="HydraFS Home">
        <Logo size={22} withWordmark wordmarkSize={15} />
      </BrandLink>

      <NavRow>
        {NAV.map(it => (
          <NavItem
            key={it.to}
            to={it.to}
            end={it.to === '/'}
            className={isActive(it.to) ? 'active' : ''}
          >
            {it.label}
          </NavItem>
        ))}
      </NavRow>

      <Right>
        <Tooltip title="全局搜索（即将上线）">
          <SearchPill onClick={() => { /* TODO: open ⌘K palette */ }}>
            <SearchOutlined />
            <span>搜索任何内容</span>
            <kbd>⌘</kbd><kbd>K</kbd>
          </SearchPill>
        </Tooltip>

        <Tooltip title={mode === 'dark' ? '切换到浅色' : '切换到深色'}>
          <IconBtn onClick={toggle} aria-label="toggle theme">
            {mode === 'dark' ? <SunOutlined /> : <MoonOutlined />}
          </IconBtn>
        </Tooltip>

        {user && (
          <Dropdown menu={menu} placement="bottomRight" trigger={['click']}>
            <UserChip>
              <Avatar>{(user.username || 'U').slice(0, 1).toUpperCase()}</Avatar>
              <span style={{ maxWidth: 90, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                {user.username}
              </span>
            </UserChip>
          </Dropdown>
        )}
      </Right>
    </Bar>
  );
};

export default Topbar;
