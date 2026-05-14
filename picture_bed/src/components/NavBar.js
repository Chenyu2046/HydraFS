import React from 'react';
import { Layout } from 'antd';
import { Link, useLocation, useNavigate } from 'react-router-dom';
import { useAuth } from '../contexts/AuthContext';
import styled from '@emotion/styled';

const { Header } = Layout;

const StyledHeader = styled(Header)`
  position: sticky;
  top: 0;
  z-index: 100;
  background: rgba(255, 255, 255, 0.85);
  backdrop-filter: saturate(180%) blur(20px);
  -webkit-backdrop-filter: saturate(180%) blur(20px);
  border-bottom: 1px solid rgba(0, 0, 0, 0.06);
  display: flex;
  align-items: center;
  padding: 0 32px;
  height: 56px;
  line-height: 56px;
`;

const Logo = styled(Link)`
  font-size: 18px;
  font-weight: 700;
  color: #0F172A;
  margin-right: 40px;
  letter-spacing: -0.3px;
  text-decoration: none;
  &:hover {
    color: #2563EB;
  }
`;

const NavLinks = styled.nav`
  display: flex;
  gap: 4px;
  flex: 1;
`;

const NavItem = styled(Link)`
  padding: 6px 14px;
  border-radius: 8px;
  font-size: 13.5px;
  font-weight: 500;
  color: ${props => props.$active ? '#2563EB' : '#475569'};
  background: ${props => props.$active ? '#EFF6FF' : 'transparent'};
  text-decoration: none;
  transition: all 0.15s ease;

  &:hover {
    color: #2563EB;
    background: #EFF6FF;
  }
`;

const UserArea = styled.div`
  display: flex;
  align-items: center;
  gap: 12px;
  margin-left: auto;
`;

const UserName = styled.span`
  font-size: 13.5px;
  font-weight: 500;
  color: #334155;
`;

const LogoutBtn = styled.button`
  padding: 4px 12px;
  border: none;
  background: transparent;
  color: #94A3B8;
  font-size: 13px;
  font-weight: 500;
  cursor: pointer;
  border-radius: 6px;
  transition: all 0.15s ease;

  &:hover {
    color: #DC2626;
    background: #FEF2F2;
  }
`;

const LoginLink = styled(Link)`
  padding: 6px 16px;
  background: #2563EB;
  color: #fff;
  border-radius: 8px;
  font-size: 13.5px;
  font-weight: 500;
  text-decoration: none;
  transition: background 0.15s ease;

  &:hover {
    background: #1D4ED8;
    color: #fff;
  }
`;

const NavBar = () => {
  const { user, logout } = useAuth();
  const location = useLocation();
  const navigate = useNavigate();

  const handleLogout = () => {
    logout();
    navigate('/login');
  };

  if (!user) return null;

  const navItems = [
    { path: '/', label: '首页' },
    { path: '/images', label: '图片' },
    { path: '/files', label: '文件' },
    { path: '/shared', label: '共享' },
    { path: '/top-downloads', label: '下载榜' },
  ];

  return (
    <StyledHeader>
      <Logo to="/">CloudVault</Logo>
      <NavLinks>
        {navItems.map(item => (
          <NavItem
            key={item.path}
            to={item.path}
            $active={location.pathname === item.path}
          >
            {item.label}
          </NavItem>
        ))}
      </NavLinks>
      <UserArea>
        <UserName>{user.username}</UserName>
        <LogoutBtn onClick={handleLogout}>退出</LogoutBtn>
      </UserArea>
    </StyledHeader>
  );
};

export default NavBar;
