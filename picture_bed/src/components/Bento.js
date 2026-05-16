import React from 'react';
import styled from '@emotion/styled';
import { useNavigate } from 'react-router-dom';
import {
  ArrowRightOutlined, CheckCircleFilled, LoadingOutlined,
  FileOutlined, FilePdfOutlined, FileImageOutlined, FileTextOutlined,
  SearchOutlined, LinkOutlined,
} from '@ant-design/icons';
import { useInView } from '../lib/motion';

/* =================================================================
 * Bento Grid 布局 — 6 张能力卡，2 大 4 中小
 * 4 行 × 6 列：
 *   [ 大1 spans 4×2 ][ 中2 spans 2×1 ]
 *   [ 大1 (cont)    ][ 中3 spans 2×1 ]
 *   [ 大4 spans 3×2 ][ 小5 spans 3×1 ]
 *   [ 大4 (cont)    ][ 小6 spans 3×1 ]
 * 所有 mockup 都是 SVG / 简化 DOM，零额外依赖
 * ================================================================= */

const Grid = styled.div`
  display: grid;
  grid-template-columns: repeat(6, 1fr);
  grid-auto-rows: minmax(160px, auto);
  gap: 16px;
  margin-top: 16px;

  @media (max-width: 980px) {
    grid-template-columns: 1fr;
  }
`;

/* 让卡片随父级 data-inview 翻转触发 opacity 淡入；
   transform 不在这里碰，避免与 hover translateY 冲突 */
const RevealStyles = `
  opacity: 0;
  transition: opacity 480ms cubic-bezier(.16,1,.3,1);
  will-change: opacity;
`;
const RevealOn = `
  opacity: 1;
`;

const Card = styled.div`
  /* base */
  position: relative;
  display: flex; flex-direction: column;
  text-align: left;
  padding: 22px 22px 18px;
  border-radius: 20px;
  border: 1px solid ${p => p.theme.colors.border};
  background: ${p => p.theme.colors.panel};
  color: ${p => p.theme.colors.text};
  cursor: pointer;
  overflow: hidden;
  ${RevealStyles}
  transition: opacity 480ms ${p => p.theme.ease.out},
              transform 240ms ${p => p.theme.ease.out},
              box-shadow 240ms ${p => p.theme.ease.out},
              border-color 240ms ${p => p.theme.ease.out};

  /* scroll-in 错峰：父级 [data-inview="true"] 翻转后逐张亮起 */
  [data-bento][data-inview="true"] > & { ${RevealOn} }
  &:nth-of-type(1) { transition-delay: 0ms; }
  &:nth-of-type(2) { transition-delay: 70ms; }
  &:nth-of-type(3) { transition-delay: 140ms; }
  &:nth-of-type(4) { transition-delay: 210ms; }
  &:nth-of-type(5) { transition-delay: 280ms; }
  &:nth-of-type(6) { transition-delay: 350ms; }

  @media (prefers-reduced-motion: reduce) {
    opacity: 1 !important;
    transition: none !important;
  }

  &:hover {
    transform: translateY(-3px);
    box-shadow: ${p => p.theme.shadow.float};
    border-color: ${p => p.theme.colors.borderStrong};
  }
  &:focus-visible {
    outline: 2px solid ${p => p.theme.colors.accent};
    outline-offset: 2px;
  }

  /* spans */
  &.s-l1 { grid-column: span 4; grid-row: span 2; }
  &.s-m2 { grid-column: span 2; grid-row: span 1; }
  &.s-m3 { grid-column: span 2; grid-row: span 1; }
  &.s-l4 { grid-column: span 3; grid-row: span 2; }
  &.s-s5 { grid-column: span 3; grid-row: span 1; }
  &.s-s6 { grid-column: span 3; grid-row: span 1; }

  @media (max-width: 980px) {
    &.s-l1, &.s-m2, &.s-m3, &.s-l4, &.s-s5, &.s-s6 {
      grid-column: 1 / -1; grid-row: auto;
    }
  }

  .head {
    display: flex; align-items: center; gap: 10px;
    margin-bottom: 14px;
    .tag {
      font-size: 10.5px; font-weight: 700;
      letter-spacing: 0.6px;
      padding: 3px 8px;
      border-radius: 999px;
      background: ${p => p.theme.colors.panel2};
      color: ${p => p.theme.colors.text2};
      font-family: ${p => p.theme.fontFamily.mono};
    }
  }
  h3 {
    margin: 0 0 6px;
    font-size: 18px;
    font-weight: 600;
    letter-spacing: -0.3px;
    color: ${p => p.theme.colors.text};
  }
  .desc {
    margin: 0 0 14px;
    font-size: 13px;
    color: ${p => p.theme.colors.text2};
    line-height: 1.55;
    max-width: 420px;
  }
  .mock {
    margin-top: auto;
    padding-top: 8px;
  }
  .more {
    position: absolute; right: 18px; top: 18px;
    width: 28px; height: 28px;
    display: grid; place-items: center;
    border-radius: 999px;
    background: ${p => p.theme.colors.panel2};
    color: ${p => p.theme.colors.text2};
    font-size: 11px;
    transition: all 200ms ease;
  }
  &:hover .more {
    background: ${p => p.theme.colors.accent};
    color: #fff;
  }
`;

/* ====== Card 1：Storage Pipeline (大) ====== */
const Pipeline = styled.div`
  display: grid;
  grid-template-columns: repeat(5, 1fr);
  gap: 8px;
  position: relative;

  .step {
    display: flex; flex-direction: column; gap: 8px;
    padding: 14px 12px;
    border-radius: 12px;
    background: ${p => p.theme.colors.panel2};
    border: 1px solid ${p => p.theme.colors.border};
    position: relative;

    .num {
      width: 22px; height: 22px;
      display: grid; place-items: center;
      border-radius: 7px;
      font-family: ${p => p.theme.fontFamily.mono};
      font-size: 11px; font-weight: 700;
      background: ${p => p.theme.colors.panel};
      color: ${p => p.theme.colors.text2};
      border: 1px solid ${p => p.theme.colors.border};
    }
    .label {
      font-size: 12px; font-weight: 600;
      color: ${p => p.theme.colors.text};
      letter-spacing: -0.1px;
    }
    .meta {
      font-size: 10.5px;
      color: ${p => p.theme.colors.text3};
      font-family: ${p => p.theme.fontFamily.mono};
    }
  }
  .step.done {
    background: ${p => p.theme.colors.accentSoft};
    border-color: ${p => p.theme.colors.accentBorder};
    .num {
      background: ${p => p.theme.colors.accent};
      color: #fff; border-color: transparent;
    }
  }
  .step.active {
    border-color: ${p => p.theme.colors.accent};
    .num { color: ${p => p.theme.colors.accent}; }
  }

  @media (max-width: 1180px) {
    grid-template-columns: repeat(3, 1fr);
  }
`;

/* ====== Card 2：AI Understanding (中) ====== */
const InsightPanel = styled.div`
  border: 1px solid ${p => p.theme.colors.border};
  border-radius: 12px;
  background: ${p => p.theme.colors.panel2};
  padding: 12px 14px;

  .file {
    display: flex; align-items: center; gap: 8px;
    margin-bottom: 8px;
    font-size: 12px; font-weight: 600;
    color: ${p => p.theme.colors.text};
    .ico { color: ${p => p.theme.colors.accent}; }
    .badge {
      margin-left: auto;
      font-size: 9.5px; font-weight: 700;
      padding: 2px 6px; border-radius: 999px;
      background: ${p => p.theme.colors.success}22;
      color: ${p => p.theme.colors.success};
      letter-spacing: 0.4px;
      font-family: ${p => p.theme.fontFamily.mono};
    }
  }
  .summary {
    font-size: 11.5px;
    color: ${p => p.theme.colors.text2};
    line-height: 1.5;
    margin-bottom: 10px;
  }
  .tags {
    display: flex; gap: 5px; flex-wrap: wrap;
    .t {
      font-size: 10.5px;
      padding: 2px 7px;
      border-radius: 5px;
      background: ${p => p.theme.colors.panel};
      color: ${p => p.theme.colors.text2};
      border: 1px solid ${p => p.theme.colors.border};
    }
  }
`;

/* ====== Card 3：Workspace (中) — recent file rows ====== */
const RecentList = styled.div`
  display: flex; flex-direction: column; gap: 6px;

  .row {
    display: flex; align-items: center; gap: 10px;
    padding: 8px 10px;
    border-radius: 8px;
    background: ${p => p.theme.colors.panel2};
    border: 1px solid ${p => p.theme.colors.border};
    .ico {
      width: 24px; height: 24px;
      border-radius: 6px;
      background: ${p => p.theme.colors.panel};
      display: grid; place-items: center;
      font-size: 12px;
      color: ${p => p.theme.colors.text2};
      flex-shrink: 0;
    }
    .name {
      flex: 1; min-width: 0;
      font-size: 12px;
      color: ${p => p.theme.colors.text};
      white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
    }
    .status {
      font-size: 10.5px;
      font-family: ${p => p.theme.fontFamily.mono};
      letter-spacing: 0.3px;
    }
    .status.ok { color: ${p => p.theme.colors.success}; }
    .status.run { color: ${p => p.theme.colors.accent}; }
  }
`;

/* ====== Card 4：Knowledge Graph (大) — pure SVG mini graph ====== */
const GraphCard = styled.div`
  position: relative;
  height: 100%;
  min-height: 220px;
  border-radius: 12px;
  border: 1px solid ${p => p.theme.colors.border};
  background:
    radial-gradient(circle at 50% 50%, ${p => p.theme.colors.panel2} 0%, ${p => p.theme.colors.panel} 100%);
  overflow: hidden;

  svg { width: 100%; height: 100%; display: block; }
  .legend {
    position: absolute;
    left: 12px; bottom: 10px;
    display: flex; gap: 12px;
    font-size: 10.5px;
    color: ${p => p.theme.colors.text3};
    font-family: ${p => p.theme.fontFamily.mono};
    span { display: inline-flex; align-items: center; gap: 5px; }
    i { width: 7px; height: 7px; border-radius: 999px; }
  }
`;

/* ====== Card 5: Backlinks (小) ====== */
const BackList = styled.div`
  display: flex; flex-direction: column; gap: 6px;
  .ln {
    display: flex; align-items: center; gap: 8px;
    padding: 6px 9px;
    border-radius: 7px;
    background: ${p => p.theme.colors.panel2};
    border: 1px solid ${p => p.theme.colors.border};
    font-size: 11.5px;
    color: ${p => p.theme.colors.text};
    .arr { color: ${p => p.theme.colors.text3}; font-size: 10px; }
    .src { color: ${p => p.theme.colors.text2}; }
    .acc { color: ${p => p.theme.colors.accent}; font-weight: 600; }
  }
`;

/* ====== Card 6: Semantic Search (小) ====== */
const SemSearch = styled.div`
  .bar {
    display: flex; align-items: center; gap: 8px;
    padding: 9px 12px;
    border-radius: 10px;
    background: ${p => p.theme.colors.panel2};
    border: 1px solid ${p => p.theme.colors.border};
    margin-bottom: 8px;
    color: ${p => p.theme.colors.text2};
    font-size: 12px;
    .q { color: ${p => p.theme.colors.text}; font-weight: 500; flex: 1; }
    .kbd {
      font-family: ${p => p.theme.fontFamily.mono};
      font-size: 10px;
      padding: 2px 5px;
      border-radius: 4px;
      background: ${p => p.theme.colors.panel};
      border: 1px solid ${p => p.theme.colors.border};
      color: ${p => p.theme.colors.text3};
    }
  }
  .result {
    display: flex; gap: 8px; align-items: center;
    padding: 7px 10px;
    border-radius: 7px;
    background: ${p => p.theme.colors.accentSoft};
    border: 1px solid ${p => p.theme.colors.accentBorder};
    font-size: 11.5px;
    color: ${p => p.theme.colors.text};
    .pct {
      margin-left: auto;
      font-family: ${p => p.theme.fontFamily.mono};
      color: ${p => p.theme.colors.accent};
      font-weight: 700;
    }
  }
`;

/* ============================== mock data (静态、不污染真实接口) ============================== */
const PIPELINE = [
  { num: '01', label: 'MD5 Fingerprint', meta: 'sha-md5',  state: 'done' },
  { num: '02', label: 'Instant Dedupe',  meta: 'check',    state: 'done' },
  { num: '03', label: 'Chunk Upload',    meta: '10MB ×',   state: 'done' },
  { num: '04', label: 'Cluster Merge',   meta: 'fastdfs',  state: 'active' },
  { num: '05', label: 'Stored',          meta: 'group01',  state: '' },
];

const WORKSPACE = [
  { ico: <FilePdfOutlined />,  name: 'distributed_systems.pdf',  status: 'INDEXED', cls: 'ok'  },
  { ico: <FileImageOutlined />, name: 'cluster_diagram.png',     status: 'EMBEDDING', cls: 'run' },
  { ico: <FileTextOutlined />, name: 'pipeline_design.md',       status: 'INDEXED', cls: 'ok'  },
];

/* ============================== Bento ============================== */
const Bento = () => {
  const nav = useNavigate();
  const { ref, inView } = useInView({ threshold: 0.05, once: true });

  // 让 div Card 既能点又能键盘操作（Enter / Space），保留 a11y
  const goer = (to) => ({
    role: 'button',
    tabIndex: 0,
    onClick: () => nav(to),
    onKeyDown: (e) => {
      if (e.key === 'Enter' || e.key === ' ') {
        e.preventDefault();
        nav(to);
      }
    },
  });

  return (
    <Grid ref={ref} data-bento data-inview={inView}>
      {/* === Card 1 — Storage Pipeline (大) === */}
      <Card className="s-l1" {...goer('/files')}>
        <span className="more"><ArrowRightOutlined /></span>
        <div className="head">
          <span className="tag">LAYER 01 · STORAGE</span>
        </div>
        <h3>Distributed storage pipeline</h3>
        <p className="desc">
          MD5 指纹比对实现秒传；超过 10MB 的文件自动切片并行上传，断点续传后由 FastDFS 集群完成落盘合并。
        </p>
        <div className="mock">
          <Pipeline>
            {PIPELINE.map((s, i) => (
              <div key={i} className={`step ${s.state}`}>
                <span className="num">
                  {s.state === 'done' ? <CheckCircleFilled style={{ fontSize: 13 }} /> :
                   s.state === 'active' ? <LoadingOutlined /> : s.num}
                </span>
                <span className="label">{s.label}</span>
                <span className="meta">{s.meta}</span>
              </div>
            ))}
          </Pipeline>
        </div>
      </Card>

      {/* === Card 2 — AI Understanding (中) === */}
      <Card className="s-m2" {...goer('/knowledge')}>
        <span className="more"><ArrowRightOutlined /></span>
        <div className="head">
          <span className="tag">LAYER 02 · AI</span>
        </div>
        <h3>AI file understanding</h3>
        <p className="desc">摘要 · 关键词 · 向量化 · 语义索引。</p>
        <div className="mock">
          <InsightPanel>
            <div className="file">
              <FileOutlined className="ico" /> distributed_systems.pdf
              <span className="badge">INDEXED</span>
            </div>
            <div className="summary">本文综述分布式系统中一致性、容错与共识的核心模型，重点介绍 Raft 与 Paxos…</div>
            <div className="tags">
              <span className="t">distributed</span>
              <span className="t">consensus</span>
              <span className="t">raft</span>
              <span className="t">+4</span>
            </div>
          </InsightPanel>
        </div>
      </Card>

      {/* === Card 3 — Workspace (中) === */}
      <Card className="s-m3" {...goer('/files')}>
        <span className="more"><ArrowRightOutlined /></span>
        <div className="head">
          <span className="tag">WORKSPACE</span>
        </div>
        <h3>Live processing</h3>
        <p className="desc">上传即进入 AI 流水线，状态实时回流。</p>
        <div className="mock">
          <RecentList>
            {WORKSPACE.map((r, i) => (
              <div key={i} className="row">
                <span className="ico">{r.ico}</span>
                <span className="name">{r.name}</span>
                <span className={`status ${r.cls}`}>{r.status}</span>
              </div>
            ))}
          </RecentList>
        </div>
      </Card>

      {/* === Card 4 — Knowledge Graph (大) === */}
      <Card className="s-l4" {...goer('/graph')}>
        <span className="more"><ArrowRightOutlined /></span>
        <div className="head">
          <span className="tag">LAYER 03 · GRAPH</span>
        </div>
        <h3>Bi-directional knowledge graph</h3>
        <p className="desc">
          文件之间通过共享概念自动建立双向链接，孤立文件聚合为可探索的知识网络。
        </p>
        <div className="mock" style={{ flex: 1 }}>
          <GraphCard>
            <svg viewBox="0 0 360 200" preserveAspectRatio="xMidYMid meet">
              {/* edges */}
              <g stroke="currentColor" strokeOpacity="0.18" strokeWidth="1">
                <line x1="180" y1="100" x2="80"  y2="50"  />
                <line x1="180" y1="100" x2="290" y2="40"  />
                <line x1="180" y1="100" x2="60"  y2="160" />
                <line x1="180" y1="100" x2="300" y2="160" />
                <line x1="180" y1="100" x2="180" y2="170" />
                <line x1="80"  y1="50"  x2="60"  y2="160" />
                <line x1="290" y1="40"  x2="300" y2="160" />
              </g>
              {/* nodes */}
              <g>
                <circle cx="180" cy="100" r="12" fill="currentColor" fillOpacity="0.85" />
                <circle cx="180" cy="100" r="20" fill="none" stroke="currentColor" strokeOpacity="0.25" />
                <circle cx="80"  cy="50"  r="7" fill="#7DD3FC" />
                <circle cx="290" cy="40"  r="7" fill="#C4B5FD" />
                <circle cx="60"  cy="160" r="7" fill="#FCA5A5" />
                <circle cx="300" cy="160" r="7" fill="#FBBF24" />
                <circle cx="180" cy="170" r="6" fill="#94A3B8" />
              </g>
              {/* labels */}
              <g fontSize="9" fontFamily="JetBrains Mono, monospace" fill="currentColor" fillOpacity="0.6">
                <text x="180" y="135" textAnchor="middle">current</text>
                <text x="80"  y="40"  textAnchor="middle">doc.pdf</text>
                <text x="290" y="30"  textAnchor="middle">image</text>
                <text x="60"  y="180" textAnchor="middle">code.py</text>
                <text x="300" y="180" textAnchor="middle">notes.md</text>
              </g>
            </svg>
            <div className="legend">
              <span><i style={{ background: '#7DD3FC' }} />doc</span>
              <span><i style={{ background: '#C4B5FD' }} />image</span>
              <span><i style={{ background: '#FCA5A5' }} />code</span>
              <span><i style={{ background: '#FBBF24' }} />archive</span>
            </div>
          </GraphCard>
        </div>
      </Card>

      {/* === Card 5 — Backlinks (小) === */}
      <Card className="s-s5" {...goer('/knowledge')}>
        <span className="more"><ArrowRightOutlined /></span>
        <div className="head">
          <span className="tag">BACKLINKS</span>
        </div>
        <h3>Linked from…</h3>
        <p className="desc">每个文件都能看到反向引用与共享标签。</p>
        <div className="mock">
          <BackList>
            <div className="ln">
              <LinkOutlined className="arr" />
              <span className="src">cluster_diagram.png</span>
              <ArrowRightOutlined className="arr" />
              <span className="acc">distributed_systems.pdf</span>
            </div>
            <div className="ln">
              <LinkOutlined className="arr" />
              <span className="src">pipeline_design.md</span>
              <ArrowRightOutlined className="arr" />
              <span className="acc">distributed_systems.pdf</span>
            </div>
          </BackList>
        </div>
      </Card>

      {/* === Card 6 — Semantic Search (小) === */}
      {/* 语义搜索入口走 /knowledge（API key + 重建索引在那一页），避免点了原地不动 */}
      <Card className="s-s6" {...goer('/knowledge')}>
        <span className="more"><ArrowRightOutlined /></span>
        <div className="head">
          <span className="tag">SEMANTIC SEARCH</span>
        </div>
        <h3>Search by meaning</h3>
        <p className="desc">自然语言检索摘要、标签与节点关系。</p>
        <div className="mock">
          <SemSearch>
            <div className="bar">
              <SearchOutlined />
              <span className="q">"raft 算法相关的笔记"</span>
              <span className="kbd">⏎</span>
            </div>
            <div className="result">
              <FileOutlined />
              <span>distributed_systems.pdf</span>
              <span className="pct">0.92</span>
            </div>
          </SemSearch>
        </div>
      </Card>
    </Grid>
  );
};

export default Bento;
