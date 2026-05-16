import React from 'react';
import styled from '@emotion/styled';
import Topbar from './Topbar';

/* ============================================================
 * AppShell — Phase 2 信息架构改造
 *  · 不再渲染全局左侧 Sidebar（顶导承载主功能）
 *  · 支持 aside slot（子页面按需注入上下文侧栏，可选）
 *  · flushContent：让 Hero 类页面贴边、占满（首页）
 *  · transparentTopbar：让 Topbar 与 Hero 渐变无缝
 * ============================================================ */

const Shell = styled.div`
  min-height: 100vh;
  background: ${p => p.theme.colors.bg};
  color: ${p => p.theme.colors.text};
  display: flex; flex-direction: column;
`;

const Main = styled.main`
  display: flex; flex-direction: column;
  min-height: 100vh;
`;

const Row = styled.div`
  display: grid;
  grid-template-columns: ${p => p.$hasAside ? 'minmax(0, 1fr) 300px' : '1fr'};
  gap: ${p => p.$hasAside ? '24px' : '0'};
  width: 100%;
  max-width: ${p => p.$flush ? 'none' : '1440px'};
  margin: 0 auto;
  padding: ${p => p.$flush
    ? '0 0 64px'
    : (p.$hasAside ? '24px 28px 64px' : '28px 36px 64px')};

  @media (max-width: 1100px) {
    grid-template-columns: 1fr;
    gap: 16px;
  }
  @media (max-width: 768px) {
    padding: ${p => p.$flush ? '0 0 48px' : '20px 16px 48px'};
  }
`;

const Aside = styled.aside`
  position: sticky;
  top: 80px;
  align-self: start;
  height: fit-content;
  max-height: calc(100vh - 96px);
  overflow: auto;
  display: flex; flex-direction: column; gap: 14px;

  @media (max-width: 1100px) {
    position: static;
    max-height: none;
  }
`;

const Content = styled.div`
  min-width: 0;
`;

/**
 * @param children          主内容
 * @param aside             可选右侧上下文面板
 * @param transparentTopbar 透明顶导（Hero 首页用）
 * @param flushContent      贴边内容（Hero 全宽用）
 * @param crumbs            legacy；新 IA 已用顶导主项替代面包屑
 */
const AppShell = ({
  children,
  aside = null,
  transparentTopbar = false,
  flushContent = false,
  // eslint-disable-next-line no-unused-vars
  crumbs,
}) => (
  <Shell>
    <Main>
      <Topbar transparent={transparentTopbar} />
      <Row className="hydra-enter" $flush={flushContent} $hasAside={!!aside && !flushContent}>
        <Content>{children}</Content>
        {aside && !flushContent && <Aside>{aside}</Aside>}
      </Row>
    </Main>
  </Shell>
);

export default AppShell;
