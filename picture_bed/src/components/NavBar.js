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
  background: rgba(255, 255, 255, 0.72);
  backdrop-filter: saturate(180%) blur(20px);
  -webkit-backdrop-filter: saturate(180%) blur(20px);
  border-bottom: 1px solid rgba(0, 0, 0, 0.04);
  display: flex;
  align-items: center;
  padding: 0 40px;
  height: 56px;
  line-height: 56px;
`;

const ContentWrapper = styled.div`
  width: 100%;
  max-width: 1200px;
  margin: 0 auto;
  display: flex;
  align-items: center;
`;

const Logo = styled(Link)`
  font-size: 18px;
  font-weight: 700;
  color: #1D1D1F;
  margin-right: 48px;
  letter-spacing: -0.5px;
  text-decoration: none;
  transition: opacity 0.2s ease;
  &:hover {
    opacity: 0.8;
    color: #1D1D1F;
  }
`;

const NavLinks = styled.nav`
  display: flex;
  gap: 8px;
  flex: 1;
`;

const NavItem = styled(Link)`
  padding: 4px 12px;
  border-radius: 8px;
  font-size: 13.5px;
  font-weight: ${props => props.$active ? '600' : '400'};
  color: ${props => props.$active ? '#1D1D1F' : '#86868B'};
  background: ${props => props.$active ? 'rgba(0,0,0,0.04)' : 'transparent'};
  text-decoration: none;
  transition: all 0.2s ease;

  &:hover {
    color: #1D1D1F;
    background: rgba(0,0,0,0.04);
  }
`;

const UserArea = styled.div`
  display: flex;
  align-items: center;
  gap: 16px;
  margin-left: auto;
`;

const UserName = styled.span`
  font-size: 13px;
  font-weight: 500;
  color: #1D1D1F;
`;

const LogoutBtn = styled.button`
  padding: 4px 12px;
  border: none;
  background: transparent;
  color: #86868B;
  font-size: 13px;
  font-weight: 500;
  cursor: pointer;
  border-radius: 20px;
  transition: all 0.2s ease;

  &:hover {
    color: #FF3B30;
    background: rgba(255, 59, 48, 0.08);
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
      <ContentWrapper>
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
      </ContentWrapper>
    </StyledHeader>
  );
};

export default NavBar;
