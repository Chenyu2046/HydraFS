import React, { useCallback, useEffect, useMemo, useState } from 'react';
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
import { aiSearch } from '../services/ai';

import { HeroCanvas, ProductWindow } from '../components/HeroCanvas';
import MetricStrip from '../components/MetricStrip';
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
    font-size: clamp(40px, 5.6vw, 68px);
    line-height: 1.04;
    letter-spacing: -2px;
    font-weight: 600;
    color: ${p => p.theme.colors.text};

    .accent {
      background: linear-gradient(115deg,
        ${p => p.theme.colors.accent} 0%,
        ${p => p.theme.colors.warn} 62%,
        ${p => p.theme.colors.graphArchive} 100%);
      -webkit-background-clip: text;
      background-clip: text;
      -webkit-text-fill-color: transparent;
      font-style: italic;
      font-weight: 500;
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

const ProductGrid = styled.div`
  display: grid;
  grid-template-columns: 1.1fr 0.9fr;
  gap: 16px;
  @media (max-width: 980px) { grid-template-columns: 1fr; }
`;

const EntryCard = styled.div`
  position: relative;
  overflow: hidden;
  min-height: 220px;
  border-radius: 16px;
  border: 1px solid ${p => p.theme.colors.border};
  background: ${p => p.theme.colors.panel};
  padding: 22px;
  display: flex;
  flex-direction: column;
  gap: 16px;

  &::after {
    content: '';
    position: absolute;
    width: 220px; height: 220px;
    right: -70px; top: -80px;
    border-radius: 50%;
    background: radial-gradient(circle, ${p => p.theme.colors.accentSoft} 0%, transparent 68%);
    pointer-events: none;
  }

  .kicker {
    width: fit-content;
    padding: 4px 9px;
    border-radius: 999px;
    font-family: ${p => p.theme.fontFamily.mono};
    font-size: 11px;
    color: ${p => p.theme.colors.accent};
    background: ${p => p.theme.colors.accentSoft};
    border: 1px solid ${p => p.theme.colors.accentBorder};
  }
  h3 {
    margin: 0;
    max-width: 520px;
    color: ${p => p.theme.colors.text};
    font-family: ${p => p.theme.fontFamily.heading};
    font-size: 24px;
    line-height: 1.2;
    letter-spacing: -0.6px;
  }
  p {
    margin: 0;
    max-width: 560px;
    color: ${p => p.theme.colors.text2};
    line-height: 1.65;
  }
  .actions { margin-top: auto; display: flex; flex-wrap: wrap; gap: 10px; }
`;

const StatusCard = styled(Panel)`
  padding: 18px;
  display: flex;
  flex-direction: column;
  gap: 14px;

  .row {
    display: grid;
    grid-template-columns: 1fr auto;
    gap: 12px;
    align-items: center;
    padding: 12px 0;
    border-top: 1px solid ${p => p.theme.colors.border};
  }
  .row:first-of-type { border-top: none; padding-top: 0; }
  .label {
    color: ${p => p.theme.colors.text};
    font-weight: 600;
  }
  .hint {
    color: ${p => p.theme.colors.text3};
    font-size: 12px;
    margin-top: 2px;
  }
  .value {
    font-family: ${p => p.theme.fontFamily.mono};
    color: ${p => p.theme.colors.warn};
    font-weight: 700;
  }
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

const CHIPS = ['分片上传', '秒传检测', 'AI 摘要', '语义标签', '反向链接', '知识图谱'];

const Home = () => {
  const { user, logout } = useAuth();
  const nav = useNavigate();
  const [files, setFiles] = useState([]);
  const [loading, setLoading] = useState(true);
  const [q, setQ] = useState('');
  const [searching, setSearching] = useState(false);
  const [results, setResults] = useState(null);

  const load = useCallback(async () => {
    setLoading(true);
    try {
      const f = await fetchUserImages(user, { count: 50 });
      setFiles(f || []);
    } catch (e) {
      if (e.tokenExpired) { message.error('登录已过期'); logout(); return; }
    } finally { setLoading(false); }
  }, [logout, user]);

  useEffect(() => {
    if (user?.token) {
      load();
    }
  }, [user, load]);

  /* ====== 数据派生 ====== */
  const isEmptyAccount = !files || files.length === 0;

  const metrics = useMemo(() => {
    if (isEmptyAccount) {
      return [
        { label: '已索引文件', value: MOCK_STATS.files,                 demo: true },
        { label: '存储容量',   value: MOCK_STATS.storageGB, unit: 'GB', demo: true },
        { label: '知识节点',   value: MOCK_STATS.nodes,                 demo: true },
        { label: '双链关系',   value: MOCK_STATS.edges,                 demo: true },
        { label: '待处理索引', value: MOCK_STATS.aiTasks,               demo: true },
        { label: '共享空间',   value: MOCK_STATS.sharedFiles,           demo: true },
      ];
    }
    const totalSize = files.reduce((s, f) => s + (f.size || 0), 0);
    const shared = files.filter(f => f.share_status === 1).length;
    const wikiReady = files.filter(f => f.wiki_ready === 1).length;
    return [
      { label: '已索引文件', value: files.length },
      { label: '存储容量',   value: formatBytes(totalSize), unit: bytesUnit(totalSize) },
      { label: '知识节点',   value: wikiReady },
      { label: '双链关系',   value: wikiReady > 0 ? '-' : 0 },
      { label: '待处理索引', value: files.length - wikiReady },
      { label: '共享空间',   value: shared },
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
    setSearching(true);
    try {
      const data = await aiSearch(q, user);
      setResults(data.files || []);
    } catch (e) {
      if (e.tokenExpired) { message.error('登录已过期'); logout(); return; }
      message.error('搜索失败：' + (e.message || ''));
    } finally { setSearching(false); }
  };

  return (
    <div>
      {/* ============== Hero Canvas ============== */}
      <HeroCanvas>
        <HeroLeft>
          <span className="eyebrow"><i />分布式双链知识云 · v1.0</span>
          <h1>
            分布式云存储，<br />
            <span className="accent">长出双链知识网络</span>
          </h1>
          <p className="sub">
            文件进入分片存储后自动生成 AI 摘要、语义标签与反向链接。
            这里不是普通网盘，而是可检索、可关联、可追溯的知识云。
          </p>

          <SearchBlock>
            <div className="row">
              <Input
                size="large"
                variant="borderless"
                prefix={<SearchOutlined style={{ color: 'var(--text2)', opacity: 0.6 }} />}
                placeholder="搜索文件名、AI 摘要、标签或双链关系…"
                value={q}
                onChange={e => setQ(e.target.value)}
                onPressEnter={handleSearch}
                allowClear
              />
              <Button type="primary" size="large" loading={searching}
                onClick={handleSearch}
                style={{ borderRadius: 12, paddingInline: 20, height: 44 }}
                icon={<ThunderboltOutlined />}>
                AI 搜索
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
                <h3>语义搜索结果</h3>
                <span className="subtitle">{results.length} 个匹配</span>
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
          <ProductWindow url="linkcloud.local/knowledge-graph" live>
            <MiniGraph data={graphData} height={320} />
          </ProductWindow>
        </div>

        {/* MetricStrip 占满整个 Hero canvas 底部 */}
        <div style={{ gridColumn: '1 / -1' }}>
          <MetricStrip items={metrics} />
        </div>
      </HeroCanvas>

        {/* ============== Product Entrances ============== */}
        <ContentArea>
          <SectionTitle>
          <h2>Knowledge Cloud</h2>
          <span>核心入口只保留真实业务链路：存储、理解、检索、双链</span>
          </SectionTitle>
        <ProductGrid>
          <EntryCard>
            <span className="kicker">AI WIKI / BI-LINK</span>
            <h3>从最近文件进入 Wiki，把孤立文件变成可回溯节点。</h3>
            <p>
              AI 摘要、标签、概念链接与反向链接统一沉淀到 Wiki 详情页。
              图谱页用于查看跨文件关系，文件页用于补齐上传、分享、下载等操作。
            </p>
            <div className="actions">
              <Button type="primary" icon={<NodeIndexOutlined />} onClick={() => nav('/graph')}>打开知识图谱</Button>
              <Button icon={<ApiOutlined />} onClick={() => nav('/knowledge')}>查看 AI Wiki</Button>
            </div>
          </EntryCard>

          <StatusCard id="system-status">
            <PanelHeader style={{ padding: 0, borderBottom: 'none' }}>
              <h3>系统状态</h3>
              <span className="subtitle">存储节点与索引状态</span>
            </PanelHeader>
            <div className="row">
              <div><div className="label">存储节点</div><div className="hint">FastDFS group · metadata ready</div></div>
              <div className="value">ONLINE</div>
            </div>
            <div className="row">
              <div><div className="label">AI 索引</div><div className="hint">已索引 / 待处理文件</div></div>
              <div className="value">{metrics[2]?.value}/{metrics[4]?.value}</div>
            </div>
            <div className="row">
              <div><div className="label">双链图谱</div><div className="hint">节点与边来自文件解析结果</div></div>
              <div className="value">{graphData.nodes.length}/{graphData.links.length}</div>
            </div>
          </StatusCard>
        </ProductGrid>

        {/* ============== Workspace ============== */}
        <SectionTitle style={{ marginTop: 48 }}>
          <h2>Workspace</h2>
          <span>上传 / 最近节点 / AI 处理队列是首页的主要工作区</span>
        </SectionTitle>
        <Cols>
          <div>
            <QuickUpload onDone={load} />
            <div style={{ height: 16 }} />
            <Panel>
              <PanelHeader>
              <h3>最近知识节点</h3>
                <span className="right">
                  <Button type="text" size="small" onClick={() => nav('/knowledge')}>
                    查看全部 <ArrowRightOutlined />
                  </Button>
                </span>
              </PanelHeader>
              <RecentNodes items={recentNodes} />
            </Panel>
          </div>

          <Panel>
            <PanelHeader>
              <h3>AI 处理流水线</h3>
              <span className="subtitle">最近的处理状态</span>
            </PanelHeader>
            <AIPipeline items={aiQueue} />
          </Panel>
        </Cols>

        {/* ============== Trust footer ============== */}
        <div style={{
          marginTop: 56, padding: '24px 0',
          borderTop: '1px solid var(--border)',
          display: 'flex', alignItems: 'center', justifyContent: 'space-between',
          flexWrap: 'wrap', gap: 16,
          color: 'var(--text3)', fontSize: 12,
        }}>
          <span style={{ display: 'inline-flex', alignItems: 'center', gap: 16, flexWrap: 'wrap' }}>
            <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6 }}>
              <CheckCircleOutlined style={{ color: 'var(--success)' }} /> FastDFS · MySQL · Redis
            </span>
            <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6 }}>
              <ApiOutlined /> DashScope embeddings
            </span>
            <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6 }}>
              <NodeIndexOutlined /> FAISS index
            </span>
          </span>
          <span style={{ fontFamily: 'JetBrains Mono, monospace' }}>LinkCloud v1.0</span>
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
