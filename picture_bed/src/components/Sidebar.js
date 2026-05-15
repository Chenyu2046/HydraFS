import React from 'react';
import styled from '@emotion/styled';
import { NavLink, useLocation } from 'react-router-dom';
import {
  AppstoreOutlined, FolderOutlined, BookOutlined,
  ShareAltOutlined, NodeIndexOutlined,
} from '@ant-design/icons';
import { Tooltip } from 'antd';

const Rail = styled.aside`
  position: fixed;
  top: 0; left: 0; bottom: 0;
  width: 64px;
  background: ${p => p.theme.colors.panel};
  border-right: 1px solid ${p => p.theme.colors.border};
  display: flex;
  flex-direction: column;
  align-items: center;
  padding: 14px 0 16px;
  z-index: 50;
`;

const Brand = styled(NavLink)`
  width: 36px; height: 36px;
  border-radius: 10px;
  display: grid; place-items: center;
  background: linear-gradient(135deg, ${p => p.theme.colors.accent} 0%, ${p => p.theme.colors.accentHover} 100%);
  color: #fff;
  font-weight: 700;
  font-size: 14px;
  letter-spacing: -0.5px;
  text-decoration: none;
  margin-bottom: 18px;
  box-shadow: 0 4px 14px ${p => p.theme.colors.accentSoft};
  transition: transform ${p => p.theme.duration.base} ${p => p.theme.ease.out};
  &:hover { transform: translateY(-1px) scale(1.04); }
`;

const NavList = styled.nav`
  display: flex; flex-direction: column; gap: 4px; flex: 1;
`;

const Item = styled(NavLink)`
  width: 40px; height: 40px;
  display: grid; place-items: center;
  border-radius: 10px;
  color: ${p => p.theme.colors.text2};
  font-size: 18px;
  text-decoration: none;
  transition: all ${p => p.theme.duration.base} ${p => p.theme.ease.out};
  position: relative;

  &:hover {
    color: ${p => p.theme.colors.text};
    background: ${p => p.theme.colors.panelHover};
  }
  &.active {
    color: ${p => p.theme.colors.accent};
    background: ${p => p.theme.colors.accentSoft};
  }
  &.active::before {
    content: '';
    position: absolute;
    left: -14px; top: 50%; transform: translateY(-50%);
    width: 3px; height: 18px;
    background: ${p => p.theme.colors.accent};
    border-radius: 0 3px 3px 0;
  }
`;

const items = [
  { to: '/',           icon: <AppstoreOutlined />,  label: 'Overview' },
  { to: '/files',      icon: <FolderOutlined />,    label: 'Files' },
  { to: '/knowledge',  icon: <BookOutlined />,      label: 'Knowledge' },
  { to: '/graph',      icon: <NodeIndexOutlined />, label: 'Graph' },
  { to: '/shared',     icon: <ShareAltOutlined />,  label: 'Shared' },
];

const Sidebar = () => {
  const loc = useLocation();
  return (
    <Rail>
      <Tooltip title="HydraFS · Distributed Knowledge Cloud" placement="right">
        <Brand to="/">H</Brand>
      </Tooltip>
      <NavList>
        {items.map(it => (
          <Tooltip key={it.to} title={it.label} placement="right">
            <Item to={it.to} end={it.to === '/'}
              className={loc.pathname === it.to || (it.to !== '/' && loc.pathname.startsWith(it.to)) ? 'active' : ''}>
              {it.icon}
            </Item>
          </Tooltip>
        ))}
      </NavList>
    </Rail>
  );
};

export default Sidebar;
