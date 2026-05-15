import React, { useEffect, useMemo, useState } from 'react';
import styled from '@emotion/styled';
import { Button, Input, Empty, Spin, message, Tag } from 'antd';
import {
  CloudServerOutlined, BulbOutlined, NodeIndexOutlined,
  SearchOutlined, ArrowRightOutlined, FileOutlined,
} from '@ant-design/icons';
import { useNavigate } from 'react-router-dom';
import { useAuth } from '../contexts/AuthContext';
import { fetchUserImages } from '../services/images';
import { aiSearch, fetchApiKey } from '../services/ai';

import StatBar from '../components/StatBar';
import CapabilityCard from '../components/CapabilityCard';
import QuickUpload from '../components/QuickUpload';
import MiniGraph from '../components/MiniGraph';
import AIPipeline from '../components/AIPipeline';
import RecentNodes from '../components/RecentNodes';
import { Panel, PanelHeader, PanelBody, SectionTitle, Pill } from '../components/primitives';

import {
  MOCK_GRAPH, MOCK_STATS, MOCK_RECENT_NODES, MOCK_AI_PIPELINE,
  buildGraphFromFiles, classifyFileType,
} from '../mock/graph';

// ====== Hero ======
const Hero = styled.div`
  display: grid;
  grid-template-columns: 1.05fr 1fr;
  gap: 24px;
  margin-bottom: 28px;

  @media (max-width: 980px) { grid-template-columns: 1fr; }
`;

const HeroLeft = styled.div`
  display: flex; flex-direction: column; gap: 18px;
`;

const Eyebrow = styled.div`
  display: inline-flex; align-items: center; gap: 8px;
  padding: 4px 10px;
  width: fit-content;
  border-radius: 999px;
  background: ${p => p.theme.colors.panel2};
  border: 1px solid ${p => p.theme.colors.border};
  color: ${p => p.theme.colors.text2};
  font-size: 11.5px;
  font-family: ${p => p.theme.fontFamily.mono};
  letter-spacing: 0.6px;
  text-transform: uppercase;

  i {
    width: 6px; height: 6px; border-radius: 999px;
    background: ${p => p.theme.colors.accent};
    box-shadow: 0 0 0 4px ${p => p.theme.colors.accentSoft};
  }
`;

const Headline = styled.h1`
  font-size: 36px;
  line-height: 1.15;
  letter-spacing: -1px;
  font-weight: 700;
  color: ${p => p.theme.colors.text};
  margin: 0;

  span {
    background: linear-gradient(120deg, ${p => p.theme.colors.accent}, ${p => p.theme.colors.info});
    -webkit-background-clip: text;
    background-clip: text;
    -webkit-text-fill-color: transparent;
  }

  @media (max-width: 768px) { font-size: 28px; }
`;

const Sub = styled.p`
  margin: 0;
  color: ${p => p.theme.colors.text2};
  font-size: 14px;
  line-height: 1.65;
  max-width: 540px;
`;

const SearchWrap = styled.div`
  display: flex; gap: 8px;
  margin-top: 4px;

  .ant-input-affix-wrapper {
    border-radius: 10px;
    background: ${p => p.theme.colors.panel};
    border-color: ${p => p.theme.colors.border};
    height: 44px;
    font-size: 14px;
  }
  .ant-input-affix-wrapper:focus,
  .ant-input-affix-wrapper-focused {
    border-color: ${p => p.theme.colors.accent};
    box-shadow: 0 0 0 3px ${p => p.theme.colors.accentSoft};
  }
`;

const HeroRight = styled.div``;

// ====== Layout ======
const Cols = styled.div`
  display: grid;
  grid-template-columns: ${p => p.$cols || '2fr 1fr'};
  gap: 16px;

  @media (max-width: 980px) { grid-template-columns: 1fr; }
`;

const CapGrid = styled.div`
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 14px;

  @media (max-width: 880px) { grid-template-columns: 1fr; }
`;

const SearchResults = styled.div`
  margin-top: 16px;
  border-top: 1px solid ${p => p.theme.colors.border};
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
    overflow: hidden;
    flex-shrink: 0;
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
  if (b < 1024) return `${b}B`;
  if (b < 1024 * 1024) return `${(b/1024).toFixed(1)}K`;
  if (b < 1024 ** 3) return `${(b/1024/1024).toFixed(1)}M`;
  return `${(b/1024/1024/1024).toFixed(2)}G`;
};

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
      // Overview 仅展示 MiniGraph + Recent + AI Pipeline 头部，拉 50 足够
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

  // ====== 数据派生 ======
  // 是否为空账号：所有图表/统计是否处于 demo 兜底模式
  const isEmptyAccount = !files || files.length === 0;

  const stats = useMemo(() => {
    if (isEmptyAccount) {
      // 全空账号：用 demo 数据撑视觉，每一项都显式标 demo（StatBar 角标）
      return [
        { label: 'Files',           value: MOCK_STATS.files,       demo: true },
        { label: 'Storage',         value: MOCK_STATS.storageGB,   unit: 'GB', demo: true },
        { label: 'Knowledge Nodes', value: MOCK_STATS.nodes,       demo: true },
        { label: 'Backlinks',       value: MOCK_STATS.edges,       demo: true },
        { label: 'Pending Index',   value: MOCK_STATS.aiTasks,     demo: true },
        { label: 'Shared',          value: MOCK_STATS.sharedFiles, demo: true },
      ];
    }
    // 真实账号：所有数字必须为真实派生值；零就是零，不再用 mock 兜底
    const totalSize = files.reduce((s, f) => s + (f.size || 0), 0);
    const shared = files.filter(f => f.share_status === 1).length;
    const wikiReady = files.filter(f => f.wiki_ready === 1).length;
    return [
      { label: 'Files',           value: files.length, live: true },
      { label: 'Storage',         value: formatBytes(totalSize) },
      { label: 'Knowledge Nodes', value: wikiReady },
      // 后端目前没有"边数"接口，前端不再编造数字；wiki 数 > 0 才显示 ~ 表示存在反向链接
      { label: 'Backlinks',       value: wikiReady > 0 ? '—' : 0 },
      { label: 'Pending Index',   value: files.length - wikiReady },
      { label: 'Shared',          value: shared },
    ];
  }, [files, isEmptyAccount]);

  // 图谱数据：空账号 → MOCK_GRAPH（demo）；有真实文件 → 仅生成节点（无伪造的边）
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
      // 不编造摘要；显示通用且诚实的提示语，点击进入 wiki 才看真实摘要
      summary: '点击打开 Wiki 查看 AI 摘要、标签与反向链接。',
      tags: [classifyFileType(f.type)],
      time: f.create_time || '',
    }));
  }, [files, isEmptyAccount]);

  const aiQueue = useMemo(() => {
    if (isEmptyAccount) return MOCK_AI_PIPELINE;
    const recent = (files || []).slice(-4).reverse();
    // 后端无任务队列状态，前端只做二值映射：已生成 wiki = INDEXED；未生成 = NOT INDEXED
    return recent.map(f => ({
      name: f.file_name || f.name,
      ext: f.type,
      status: f.wiki_ready === 1 ? 'done' : 'pending',
    }));
  }, [files, isEmptyAccount]);

  // ====== AI 搜索 ======
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
      message.error('搜索失败：' + (e.message || ''));
    } finally { setSearching(false); }
  };

  return (
    <div>
      {/* ====== Hero ====== */}
      <Hero>
        <HeroLeft>
          <Eyebrow><i /> Distributed Knowledge Cloud · v1.0</Eyebrow>
          <Headline>
            把每一个文件，<br />沉淀为可<span>双向链接</span>的知识。
          </Headline>
          <Sub>
            HydraFS 在分布式存储底座上叠加 AI 文件理解与双链知识图谱：
            分片上传 / 秒传保证规模，向量检索与摘要让文件可被语义搜索，节点与边把孤立文件织成你的个人知识网络。
          </Sub>
          <SearchWrap>
            <Input
              size="large"
              prefix={<SearchOutlined style={{ color: 'inherit', opacity: 0.6 }} />}
              placeholder='用自然语言搜索：例如 "Faiss 向量索引相关的笔记"'
              value={q}
              onChange={e => setQ(e.target.value)}
              onPressEnter={handleSearch}
              allowClear
            />
            <Button type="primary" size="large" loading={searching} onClick={handleSearch}>
              AI Search
            </Button>
          </SearchWrap>

          {searching && <div style={{ padding: 12 }}><Spin size="small" /> <span style={{ marginLeft: 8, color: 'var(--text2)' }}>语义检索中…</span></div>}

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

        <HeroRight>
          <Panel style={{ overflow: 'hidden' }}>
            <PanelHeader>
              <h3>Knowledge Graph</h3>
              <Pill>LIVE</Pill>
              <span className="right">
                <Button type="text" size="small" onClick={() => nav('/graph')}>
                  Open Graph <ArrowRightOutlined />
                </Button>
              </span>
            </PanelHeader>
            <MiniGraph data={graphData} height={300} />
          </Panel>
        </HeroRight>
      </Hero>

      {/* ====== Stat Bar ====== */}
      <StatBar items={stats} />

      {/* ====== Capability Cards ====== */}
      <SectionTitle>
        <h2>Core Capabilities</h2>
        <span>三层能力栈构成 HydraFS 的产品骨架</span>
      </SectionTitle>
      <CapGrid>
        <CapabilityCard
          icon={<CloudServerOutlined />} tag="LAYER 01"
          title="Distributed Storage"
          desc="基于 FastDFS 集群的分布式底座，10MB+ 文件自动切片并行上传，相同内容秒传，支持横向扩展。"
          to="/files"
          footer="分片 / 秒传 / 集群"
        />
        <CapabilityCard
          icon={<BulbOutlined />} tag="LAYER 02"
          title="AI File Understanding"
          desc="文件入库即触发 AI 摘要、关键词与向量化，构建可被自然语言检索的语义索引。"
          to="/knowledge"
          footer="摘要 / 标签 / 向量"
        />
        <CapabilityCard
          icon={<NodeIndexOutlined />} tag="LAYER 03"
          title="Bi-directional Knowledge Graph"
          desc="文件与文件之间通过共享概念自动建立双向链接，孤立文件聚合为可探索的知识网络。"
          to="/graph"
          footer="节点 / 边 / 反向链接"
        />
      </CapGrid>

      {/* ====== Quick Upload + AI Pipeline ====== */}
      <SectionTitle>
        <h2>Workspace</h2>
        <span>上传文件即进入 AI 处理流水线</span>
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

      {loading && files.length === 0 && (
        <div style={{ position: 'fixed', right: 24, bottom: 24, opacity: 0.7 }}>
          <Spin size="small" /> <span style={{ marginLeft: 8, fontSize: 12 }}>Loading…</span>
        </div>
      )}
    </div>
  );
};

export default Home;
