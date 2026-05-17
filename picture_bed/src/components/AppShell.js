import React from 'react';
import styled from '@emotion/styled';
import Topbar from './Topbar';

const Shell = styled.div`
  min-height: 100vh;
  background: ${p => p.theme.colors.bg};
  color: ${p => p.theme.colors.text};
`;

const Main = styled.main`
  display: flex; flex-direction: column;
  min-height: 100vh;
`;

const Content = styled.div`
  flex: 1;
  padding: ${p => p.$flush ? '0 0 64px' : '28px 36px 64px'};
  max-width: ${p => p.$flush ? 'none' : '1440px'};
  width: 100%;
  margin: 0 auto;

  @media (max-width: 768px) {
    padding: ${p => p.$flush ? '0 0 48px' : '20px 16px 48px'};
  }
`;

const AppShell = ({ children, crumbs, transparentTopbar = false, flushContent = false }) => (
  <Shell>
    <Main>
      <Topbar crumbs={crumbs} transparent={transparentTopbar} />
      <Content className="hydra-enter" $flush={flushContent}>{children}</Content>
    </Main>
  </Shell>
);

export default AppShell;
