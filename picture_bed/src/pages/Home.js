import React, { useEffect, useMemo, useState } from 'react';
import styled from '@emotion/styled';
import { Button, Input, Empty, Spin, message } from 'antd';
import {
  SearchOutlined, ArrowRightOutlined, FileOutlined,
  ThunderboltOutlined, ApiOutlined, NodeIndexOutlined,
  CheckCircleOutlined,
} from '@ant-design/icons';
import { useNavigate } from 'react-router-dom';
import { useAuth } from '../contexts/AuthContext';
import { fetchUserImages } from '../services/images';
import { aiSearch, fetchApiKey } from '../services/ai';

import { HeroCanvas, ProductWindow } from '../components/HeroCanvas';
import MetricStrip from '../components/MetricStrip';
import Bento from '../components/Bento';
import MiniGraph from '../components/MiniGraph';
import QuickUpload from '../components/QuickUpload';
import AIPipeline from '../components/AIPipeline';
import RecentNodes from '../components/RecentNodes';
import { Panel, PanelHeader, PanelBody, SectionTitle } from '../components/primitives';

import {
  MOCK_GRAPH, MOCK_STATS, MOCK_RECENT_NODES, MOCK_AI_PIPELINE,
  buildGraphFromFiles, classifyFileType,
} from '../mock/graph';

/* ============================================================
 * Hero (left column)
 * ============================================================ */
const HeroLeft = styled.div`
  display: flex; flex-direction: column; gap: 22px;

  .eyebrow {
    display: inline-flex; align-items: center; gap: 8px;
    width: fit-content;
    padding: 5px 11px;
    border-radius: 999px;
    background: ${p => p.theme.colors.chromeBg};
    border: 1px solid ${p => p.theme.colors.chromeBorder};
    backdrop-filter: blur(12px);
    color: ${p => p.theme.colors.text2};
    font-size: 11px;
    font-family: ${p => p.theme.fontFamily.mono};
    letter-spacing: 0.7px;
    text-transform: uppercase;
    i {
      width: 5px; height: 5px; border-radius: 999px;
      background: ${p => p.theme.colors.success};
      box-shadow: 0 0 0 4px ${p => p.theme.colors.success}22;
    }
  }

  h1 {
    margin: 0;
    /* token: fontSize.hero — 用户语言驱动的大标题，固定字号便于跨页心智一致 */
    font-size: ${p => p.theme.fontSize.hero};
    line-height: 1.06;
    letter-spacing: -1.4px;
    font-weight: 600;
    color: ${p => p.theme.colors.text};

    .accent {
      background: linear-gradient(115deg,
        ${p => p.theme.colors.accent} 0%,
        ${p => p.theme.colors.accentHover} 100%);
      -webkit-background-clip: text;
      background-clip: text;
      -webkit-text-fill-color: transparent;
      font-weight: 600;
    }
  }

  .sub {
    margin: 0;
    color: ${p => p.theme.colors.text2};
    font-size: 15px;
    line-height: 1.6;
    max-width: 520px;
  }
`;

/* ============================================================
 * Search input — 中心化核心入口
 * ============================================================ */
const SearchBlock = styled.div`
  display: flex; flex-direction: column; gap: 10px;
  margin-top: 4px;

  .row {
    display: flex; gap: 8px;
    padding: 6px 6px 6px 18px;
    border-radius: 16px;
    background: ${p => p.theme.colors.chromeBg};
    border: 1px solid ${p => p.theme.colors.chromeBorder};
    backdrop-filter: blur(12px);
    box-shadow: ${p => p.theme.shadow.md};
    align-items: center;

    .ant-input-affix-wrapper,
    .ant-input-affix-wrapper:focus,
    .ant-input-affix-wrapper-focused {
      background: transparent !important;
      border: none !important;
      box-shadow: none !important;
      padding-left: 0;
    }
    .ant-input { background: transparent !important; font-size: 14.5px; }
  }

  .chips {
    display: flex; gap: 6px; flex-wrap: wrap;
    margin-top: 4px;
    .chip {
      font-size: 11.5px;
      padding: 5px 11px;
      border-radius: 999px;
      background: ${p => p.theme.colors.chromeBg};
      border: 1px solid ${p => p.theme.colors.chromeBorder};
      backdrop-filter: blur(8px);
      color: ${p => p.theme.colors.text2};
      cursor: pointer;
      transition: all 160ms ease;
      &:hover {
        color: ${p => p.theme.colors.text};
        border-color: ${p => p.theme.colors.borderStrong};
      }
    }
  }
`;

/* ============================================================
 * 内容区下半部分 — Workspace + AI Pipeline
 * ============================================================ */
const ContentArea = styled.div`
  padding: 56px 36px 0;
  max-width: 1440px;
  margin: 0 auto;
  @media (max-width: 768px) { padding: 36px 16px 0; }
`;

const Cols = styled.div`
  display: grid;
  grid-template-columns: 2fr 1fr;
  gap: 16px;
  @media (max-width: 980px) { grid-template-columns: 1fr; }
`;

const SearchResults = styled.div`
  margin-top: 8px;
`;
const ResultRow = styled.div`
  display: flex; gap: 12px; align-items: flex-start;
  padding: 12px 0;
  border-bottom: 1px solid ${p => p.theme.colors.border};
  &:last-child { border-bottom: none; }
  .thumb {
    width: 44px; height: 44px;
    border-radius: 8px;
    background: ${p => p.theme.colors.panel2};
    display: grid; place-items: center;
    color: ${p => p.theme.colors.text2};
    overflow: hidden; flex-shrink: 0;
    img { width: 100%; height: 100%; object-fit: cover; }
  }
  .meta { flex: 1; min-width: 0; }
  .name {
    color: ${p => p.theme.colors.text};
    font-size: 13.5px; font-weight: 600;
    white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
  }
  .desc {
    color: ${p => p.theme.colors.text2};
    font-size: 12.5px; margin-top: 3px;
    display: -webkit-box; -webkit-line-clamp: 2; -webkit-box-orient: vertical; overflow: hidden;
  }
`;

const formatBytes = (b) => {
  if (!b || b === 0) return '0';
  if (b < 1024) return `${b}`;
  if (b < 1024 * 1024) return `${(b/1024).toFixed(1)}`;
  if (b < 1024 ** 3) return `${(b/1024/1024).toFixed(1)}`;
  return `${(b/1024/1024/1024).toFixed(2)}`;
};
const bytesUnit = (b) => {
  if (!b) return 'B';
  if (b < 1024) return 'B';
  if (b < 1024 * 1024) return 'KB';
  if (b < 1024 ** 3) return 'MB';
  return 'GB';
};

// Hero 副 chips：聚焦用户能直接得到的"动作 + 价值"，不暴露技术栈
const CHIPS = ['一键上传', '自动生成摘要', '自然语言搜索'];

const Home = () => {
  const { user, logout } = useAuth();
  const nav = useNavigate();
  const [files, setFiles] = useState([]);
  const [loading, setLoading] = useState(true);
  const [apiKey, setApiKey] = useState('');
  const [q, setQ] = useState('');
  const [searching, setSearching] = useState(false);
  const [results, setResults] = useState(null);

  const load = async () => {
    setLoading(true);
    try {
      const f = await fetchUserImages(user, { count: 50 });
      setFiles(f || []);
    } catch (e) {
      if (e.tokenExpired) { message.error('登录已过期'); logout(); return; }
    } finally { setLoading(false); }
  };

  useEffect(() => {
    if (user?.token) {
      load();
      fetchApiKey(user).then(k => setApiKey(k || '')).catch(() => {});
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [user]);

  /* ====== 数据派生 ====== */
  const isEmptyAccount = !files || files.length === 0;

  const metrics = useMemo(() => {
    if (isEmptyAccount) {
      return [
        { label: 'Files indexed',    value: MOCK_STATS.files,                 demo: true },
        { label: 'Stored',           value: MOCK_STATS.storageGB, unit: 'GB', demo: true },
        { label: 'Knowledge nodes',  value: MOCK_STATS.nodes,                 demo: true },
        { label: 'Backlinks',        value: MOCK_STATS.edges,                 demo: true },
        { label: 'Pending AI index', value: MOCK_STATS.aiTasks,               demo: true },
        { label: 'Shared spaces',    value: MOCK_STATS.sharedFiles,           demo: true },
      ];
    }
    const totalSize = files.reduce((s, f) => s + (f.size || 0), 0);
    const shared = files.filter(f => f.share_status === 1).length;
    const wikiReady = files.filter(f => f.wiki_ready === 1).length;
    return [
      { label: 'Files indexed',    value: files.length },
      { label: 'Stored',           value: formatBytes(totalSize), unit: bytesUnit(totalSize) },
      { label: 'Knowledge nodes',  value: wikiReady },
      { label: 'Backlinks',        value: wikiReady > 0 ? '—' : 0 },
      { label: 'Pending AI index', value: files.length - wikiReady },
      { label: 'Shared spaces',    value: shared },
    ];
  }, [files, isEmptyAccount]);

  const graphData = useMemo(() => {
    if (isEmptyAccount) return MOCK_GRAPH;
    const real = buildGraphFromFiles(files);
    return real || MOCK_GRAPH;
  }, [files, isEmptyAccount]);

  const recentNodes = useMemo(() => {
    if (isEmptyAccount) return MOCK_RECENT_NODES;
    const wikiFiles = (files || []).filter(f => f.wiki_ready === 1).slice(0, 4);
    return wikiFiles.map(f => ({
      md5: f.md5,
      title: (f.file_name || f.name || '').replace(/\.[^.]+$/, ''),
      summary: '点击打开 Wiki 查看 AI 摘要、标签与反向链接。',
      tags: [classifyFileType(f.type)],
      time: f.create_time || '',
    }));
  }, [files, isEmptyAccount]);

  const aiQueue = useMemo(() => {
    if (isEmptyAccount) return MOCK_AI_PIPELINE;
    const recent = (files || []).slice(-4).reverse();
    return recent.map(f => ({
      name: f.file_name || f.name,
      ext: f.type,
      status: f.wiki_ready === 1 ? 'done' : 'pending',
    }));
  }, [files, isEmptyAccount]);

  /* ====== AI 搜索 ====== */
  const handleSearch = async () => {
    if (!q.trim()) { message.warning('请输入搜索内容'); return; }
    if (!apiKey) {
      message.info('请先在 Knowledge 页设置 API Key');
      nav('/knowledge');
      return;
    }
    setSearching(true);
    try {
      const data = await aiSearch(q, user, apiKey);
      setResults(data.files || []);
    } catch (e) {
      if (e.tokenExpired) { message.error('登录已过期'); logout(); return; }
      if (e.apiKeyInvalid) { /* 全局弹窗已提示 */ return; }
      message.error('搜索失败：' + (e.message || ''));
    } finally { setSearching(false); }
  };

  return (
    <div>
      {/* ============== Hero Canvas ============== */}
      <HeroCanvas>
        <HeroLeft>
          <span className="eyebrow"><i />Distributed Knowledge Cloud · v1.0</span>
          <h1>
            Files in.<br />
            <span className="accent">Knowledge out.</span>
          </h1>
          <p className="sub">
            上传文件，沉淀知识网络 —— AI 自动生成摘要、提取标签、建立反向链接，
            让你的资料从"存起来"变成"用得上"。
          </p>

          <SearchBlock>
            <div className="row">
              <Input
                size="large"
                variant="borderless"
                prefix={<SearchOutlined style={{ color: 'var(--text2)', opacity: 0.6 }} />}
                placeholder="上传文件，或搜索摘要、标签与知识关系…"
                value={q}
                onChange={e => setQ(e.target.value)}
                onPressEnter={handleSearch}
                allowClear
              />
              <Button type="primary" size="large" loading={searching}
                onClick={handleSearch}
                style={{ borderRadius: 12, paddingInline: 20, height: 44 }}
                icon={<ThunderboltOutlined />}>
                AI Search
              </Button>
            </div>
            <div className="chips">
              {CHIPS.map(c => <span key={c} className="chip">{c}</span>)}
            </div>
          </SearchBlock>

          {searching && (
            <div style={{ padding: '8px 0' }}>
              <Spin size="small" /> <span style={{ marginLeft: 8, color: 'var(--text2)' }}>语义检索中…</span>
            </div>
          )}
          {results && !searching && (
            <Panel style={{ marginTop: 4 }}>
              <PanelHeader>
                <h3>Search Results</h3>
                <span className="subtitle">{results.length} matched</span>
              </PanelHeader>
              <PanelBody $pad="0 18px 12px">
                {results.length === 0 ? (
                  <Empty description="没有匹配结果" image={Empty.PRESENTED_IMAGE_SIMPLE} />
                ) : (
                  <SearchResults>
                    {results.slice(0, 5).map(r => (
                      <ResultRow key={r.md5}>
                        <div className="thumb">
                          {['png','jpg','jpeg','gif','webp'].includes((r.type||'').toLowerCase()) && r.url
                            ? <img src={r.url} alt={r.filename} />
                            : <FileOutlined />}
                        </div>
                        <div className="meta">
                          <div className="name">{r.filename}</div>
                          <div className="desc">{r.description || r.reason}</div>
                        </div>
                      </ResultRow>
                    ))}
                  </SearchResults>
                )}
              </PanelBody>
            </Panel>
          )}
        </HeroLeft>

        <div>
          <ProductWindow url="hydrafs.app/graph" live>
            <MiniGraph data={graphData} height={320} />
          </ProductWindow>
        </div>

        {/* MetricStrip 占满整个 Hero canvas 底部 */}
        <div style={{ gridColumn: '1 / -1' }}>
          <MetricStrip items={metrics} />
        </div>
      </HeroCanvas>

      {/* ============== Bento Grid (Capabilities) ============== */}
      <ContentArea>
        <SectionTitle>
          <h2>Files in. Knowledge out.</h2>
          <span>三项 AI 能力，让每一次上传都产生新连接</span>
        </SectionTitle>
        <Bento />

        {/* ============== Workspace ============== */}
        <SectionTitle style={{ marginTop: 48 }}>
          <h2>Workspace</h2>
          <span>上传文件即进入 AI 流水线，状态实时回流</span>
        </SectionTitle>
        <Cols>
          <div>
            <QuickUpload onDone={load} />
            <div style={{ height: 16 }} />
            <Panel>
              <PanelHeader>
                <h3>Recent Knowledge Nodes</h3>
                <span className="right">
                  <Button type="text" size="small" onClick={() => nav('/knowledge')}>
                    View all <ArrowRightOutlined />
                  </Button>
                </span>
              </PanelHeader>
              <RecentNodes items={recentNodes} />
            </Panel>
          </div>

          <Panel>
            <PanelHeader>
              <h3>AI Pipeline</h3>
              <span className="subtitle">最近的处理状态</span>
            </PanelHeader>
            <AIPipeline items={aiQueue} />
          </Panel>
        </Cols>

        {/* ============== Trust footer ============== */}
        {/* 不再列具体技术栈（FastDFS / MySQL / DashScope / FAISS），改为用户视角的承诺 */}
        <div style={{
          marginTop: 56, padding: '24px 0',
          borderTop: '1px solid var(--border)',
          display: 'flex', alignItems: 'center', justifyContent: 'space-between',
          flexWrap: 'wrap', gap: 16,
          color: 'var(--text3)', fontSize: 12,
        }}>
          <span style={{ display: 'inline-flex', alignItems: 'center', gap: 16, flexWrap: 'wrap' }}>
            <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6 }}>
              <CheckCircleOutlined style={{ color: 'var(--success)' }} /> 私有云部署，数据自托管
            </span>
            <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6 }}>
              <ApiOutlined /> 可替换的 AI 提供方
            </span>
            <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6 }}>
              <NodeIndexOutlined /> 知识可导出，永不锁定
            </span>
          </span>
          <span style={{ fontFamily: 'JetBrains Mono, monospace' }}>HydraFS v1.0</span>
        </div>
      </ContentArea>

      {loading && files.length === 0 && (
        <div style={{ position: 'fixed', right: 24, bottom: 24, opacity: 0.7 }}>
          <Spin size="small" /> <span style={{ marginLeft: 8, fontSize: 12 }}>Loading…</span>
        </div>
      )}
    </div>
  );
};

export default Home;
