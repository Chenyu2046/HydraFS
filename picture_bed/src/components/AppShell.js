import React from 'react';
import styled from '@emotion/styled';
import Sidebar from './Sidebar';
import Topbar from './Topbar';

const Shell = styled.div`
  min-height: 100vh;
  background: ${p => p.theme.colors.bg};
  color: ${p => p.theme.colors.text};
`;

const Main = styled.main`
  margin-left: 64px;
  display: flex; flex-direction: column;
  min-height: 100vh;
`;

const Content = styled.div`
  flex: 1;
  padding: 28px 36px 64px;
  max-width: 1440px;
  width: 100%;
  margin: 0 auto;

  @media (max-width: 768px) {
    padding: 20px 16px 48px;
  }
`;

const AppShell = ({ children, crumbs }) => (
  <Shell>
    <Sidebar />
    <Main>
      <Topbar crumbs={crumbs} />
      <Content className="hydra-enter">{children}</Content>
    </Main>
  </Shell>
);

export default AppShell;
