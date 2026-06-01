import React from 'react';
import styled from '@emotion/styled';
import { Tooltip, Dropdown } from 'antd';
import { NavLink, useLocation, useNavigate } from 'react-router-dom';
import {
  SunOutlined, MoonOutlined, LogoutOutlined, UserOutlined,
  AppstoreOutlined, FolderOutlined, BookOutlined,
  NodeIndexOutlined, ShareAltOutlined,
} from '@ant-design/icons';
import { useAuth } from '../contexts/AuthContext';
import { useThemeMode } from '../contexts/ThemeContext';
import Logo from './Logo';

/* ===== 毛玻璃顶栏 ===== */
const Bar = styled.header`
  position: sticky; top: 0; z-index: 40;
  height: 60px;
  display: flex; align-items: center;
  padding: 0 24px;
  gap: 18px;

  background: ${p =>
    p.$transparent
      ? 'transparent'
      : p.theme.name === 'dark'
        ? 'rgba(14, 14, 20, 0.55)'
        : 'rgba(255, 255, 255, 0.55)'};
  backdrop-filter: ${p => (p.$transparent ? 'none' : 'blur(18px) saturate(180%)')};
  -webkit-backdrop-filter: ${p => (p.$transparent ? 'none' : 'blur(18px) saturate(180%)')};
  border-bottom: 1px solid ${p =>
    p.$transparent ? 'transparent' : (p.theme.colors.chromeBorder || p.theme.colors.border)};
  box-shadow: ${p =>
    p.$transparent
      ? 'none'
      : p.theme.name === 'dark'
        ? 'inset 0 1px 0 rgba(255,255,255,0.05)'
        : 'inset 0 1px 0 rgba(255,255,255,0.6)'};
  transition: background 220ms ease, border-color 220ms ease;
`;

const BrandLink = styled(NavLink)`
  display: inline-flex; align-items: center; gap: 10px;
  text-decoration: none;
  padding-right: 16px;
  border-right: 1px solid ${p => p.theme.colors.border};
  height: 38px;

  .name {
    font-size: 15px;
    font-weight: 700;
    letter-spacing: -0.3px;
    color: ${p => p.theme.colors.text};
    line-height: 1.1;
  }
  .sub {
    display: block;
    font-size: 10px;
    color: ${p => p.theme.colors.text3};
    letter-spacing: 0.5px;
    margin-top: 3px;
  }

  @media (max-width: 720px) {
    border-right: none;
    padding-right: 8px;
    .meta { display: none; }
  }
`;

const Nav = styled.nav`
  display: flex; align-items: center; gap: 2px;
  flex: 1;
  overflow-x: auto;
  scrollbar-width: none;
  &::-webkit-scrollbar { display: none; }
`;

const NavItem = styled(NavLink)`
  display: inline-flex; align-items: center; gap: 6px;
  padding: 7px 14px;
  border-radius: 10px;
  font-size: 13px;
  font-weight: 500;
  color: ${p => p.theme.colors.text2};
  text-decoration: none;
  white-space: nowrap;
  transition: all 160ms ${p => p.theme.ease.out};

  .icon { font-size: 14px; opacity: 0.85; display: inline-flex; }

  &:hover {
    color: ${p => p.theme.colors.text};
    background: ${p => p.theme.colors.panelHover};
  }
  &.active {
    color: ${p => p.theme.colors.accent};
    background: ${p => p.theme.colors.accentSoft};
  }
  &.active .icon { opacity: 1; }
`;

const NavAnchor = styled.a`
  display: inline-flex; align-items: center; gap: 6px;
  padding: 7px 14px;
  border-radius: 10px;
  font-size: 13px;
  font-weight: 500;
  color: ${p => p.theme.colors.text2};
  text-decoration: none;
  white-space: nowrap;
  transition: all 160ms ${p => p.theme.ease.out};

  .icon { font-size: 14px; opacity: 0.85; display: inline-flex; }

  &:hover {
    color: ${p => p.theme.colors.text};
    background: ${p => p.theme.colors.panelHover};
  }
`;

const Crumbs = styled.div`
  display: flex; align-items: center; gap: 8px;
  color: ${p => p.theme.colors.text2};
  font-size: 12px;
  padding-left: 12px;
  margin-left: 8px;
  border-left: 1px solid ${p => p.theme.colors.border};
  @media (max-width: 1080px) { display: none; }
`;
const Sep = styled.span`color: ${p => p.theme.colors.text3};`;

const Right = styled.div`
  margin-left: auto;
  display: flex; align-items: center; gap: 8px;
`;

const IconBtn = styled.button`
  width: 36px; height: 36px;
  display: grid; place-items: center;
  border-radius: 10px;
  border: 1px solid ${p => p.theme.colors.border};
  background: ${p => p.theme.colors.panel}99;
  color: ${p => p.theme.colors.text2};
  cursor: pointer;
  backdrop-filter: blur(6px);
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
  background: ${p => p.theme.colors.panel}99;
  color: ${p => p.theme.colors.text};
  font-size: 13px;
  cursor: pointer;
  backdrop-filter: blur(6px);
  transition: all ${p => p.theme.duration.base} ${p => p.theme.ease.out};
  &:hover { background: ${p => p.theme.colors.panelHover}; border-color: ${p => p.theme.colors.borderStrong}; }
`;

const Avatar = styled.span`
  width: 26px; height: 26px;
  border-radius: 999px;
  background: linear-gradient(135deg, ${p => p.theme.colors.accent}, ${p => p.theme.colors.accentHover});
  color: #fff;
  font-size: 11px; font-weight: 700;
  display: grid; place-items: center;
`;

const NAV_ITEMS = [
  { to: '/',          icon: <AppstoreOutlined />,  label: 'AI 搜索' },
  { to: '/files',     icon: <FolderOutlined />,    label: '文件管理' },
  { to: '/graph',     icon: <NodeIndexOutlined />, label: '知识图谱' },
  { to: '/knowledge', icon: <BookOutlined />,      label: 'AI Wiki' },
  { to: '/shared',    icon: <ShareAltOutlined />,  label: '分享管理' },
];

const Topbar = ({ crumbs = [], transparent = false }) => {
  const { mode, toggle } = useThemeMode();
  const { user, logout } = useAuth();
  const nav = useNavigate();
  const loc = useLocation();

  const handleLogout = () => { logout(); nav('/login'); };
  const handleStatusClick = (event) => {
    event.preventDefault();
    if (loc.pathname !== '/') {
      nav('/#system-status');
      return;
    }
    document.getElementById('system-status')?.scrollIntoView({ behavior: 'smooth', block: 'start' });
  };

  const menu = {
    items: [
      { key: 'profile', icon: <UserOutlined />, label: user?.username || 'Account', disabled: true },
      { type: 'divider' },
      { key: 'logout', icon: <LogoutOutlined />, label: '退出登录', danger: true, onClick: handleLogout },
    ],
  };

  return (
    <Bar $transparent={transparent}>
      <BrandLink to="/" aria-label="LinkCloud Home">
        <Logo size={30} />
        <span className="meta">
          <span className="name">LinkCloud</span>
          <span className="sub">万物可链 · 智在云端</span>
        </span>
      </BrandLink>

      <Nav aria-label="主导航">
        {NAV_ITEMS.map(it => {
          const active = it.to === '/'
            ? loc.pathname === '/'
            : loc.pathname === it.to || loc.pathname.startsWith(it.to + '/');
          return (
            <NavItem key={it.to} to={it.to} end={it.to === '/'} className={active ? 'active' : ''}>
              <span className="icon">{it.icon}</span>
              <span>{it.label}</span>
            </NavItem>
          );
        })}
        <NavAnchor href="/#system-status" onClick={handleStatusClick}>
          <span className="icon"><NodeIndexOutlined /></span>
          <span>系统状态</span>
        </NavAnchor>

        {crumbs.length > 0 && (
          <Crumbs>
            {crumbs.map((c, i) => (
              <React.Fragment key={i}>
                {i > 0 && <Sep>/</Sep>}
                <span>{c}</span>
              </React.Fragment>
            ))}
          </Crumbs>
        )}
      </Nav>

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
