import React from 'react';
import styled from '@emotion/styled';
import { useNavigate } from 'react-router-dom';
import {
  ArrowRightOutlined, FileOutlined, SearchOutlined, LinkOutlined,
} from '@ant-design/icons';

/* =================================================================
 * Bento Grid — 3 张能力卡（与已完成的后端能力一一对应）：
 *   1. Auto Summary  → 上传即 LLM 生成摘要 + 标签
 *   2. Auto Link     → 双向链接 + 语义近邻自动建联
 *   3. Semantic Search → 自然语言检索（向量召回）
 *
 * 设计原则：不堆"看起来很厉害"的能力，只展示已上线、用户可立刻验证的功能。
 * ================================================================= */

const Grid = styled.div`
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 16px;
  margin-top: 16px;

  @media (max-width: 980px) {
    grid-template-columns: 1fr;
  }
`;

const Card = styled.div`
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
  transition: transform 240ms ${p => p.theme.ease.out},
              box-shadow 240ms ${p => p.theme.ease.out},
              border-color 240ms ${p => p.theme.ease.out};

  &:hover {
    transform: translateY(-3px);
    box-shadow: ${p => p.theme.shadow.float};
    border-color: ${p => p.theme.colors.borderStrong};
  }
  &:focus-visible {
    outline: 2px solid ${p => p.theme.colors.accent};
    outline-offset: 2px;
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

const SummaryMock = styled.div`
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

const GraphMock = styled.div`
  position: relative;
  border-radius: 12px;
  border: 1px solid ${p => p.theme.colors.border};
  background:
    radial-gradient(circle at 50% 50%, ${p => p.theme.colors.panel2} 0%, ${p => p.theme.colors.panel} 100%);
  overflow: hidden;
  aspect-ratio: 16 / 9;

  svg { width: 100%; height: 100%; display: block; color: ${p => p.theme.colors.text}; }
  .hint {
    position: absolute;
    left: 12px; bottom: 8px;
    font-size: 10.5px;
    color: ${p => p.theme.colors.text3};
    font-family: ${p => p.theme.fontFamily.mono};
    display: inline-flex; align-items: center; gap: 6px;
    .arr { color: ${p => p.theme.colors.accent}; }
  }
`;

const SearchMock = styled.div`
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

const Bento = () => {
  const nav = useNavigate();
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
    <Grid>
      <Card {...goer('/knowledge')}>
        <span className="more"><ArrowRightOutlined /></span>
        <div className="head"><span className="tag">01 · AUTO SUMMARY</span></div>
        <h3>上传即得 AI 摘要</h3>
        <p className="desc">
          每次上传 LLM 自动生成 2~3 句中文摘要并提取语义标签，让你在列表里 3 秒看懂一个文件。
        </p>
        <div className="mock">
          <SummaryMock>
            <div className="file">
              <FileOutlined className="ico" /> distributed_systems.pdf
              <span className="badge">SUMMARIZED</span>
            </div>
            <div className="summary">
              本文综述分布式系统中一致性、容错与共识的核心模型，重点对比 Raft 与 Paxos 在工程落地上的差异。
            </div>
            <div className="tags">
              <span className="t">distributed</span>
              <span className="t">consensus</span>
              <span className="t">raft</span>
              <span className="t">+3</span>
            </div>
          </SummaryMock>
        </div>
      </Card>

      <Card {...goer('/graph')}>
        <span className="more"><ArrowRightOutlined /></span>
        <div className="head"><span className="tag">02 · AUTO LINK</span></div>
        <h3>自动建立双向链接</h3>
        <p className="desc">
          基于共享标签与向量相似度，相关文件之间自动反向引用，孤立文档聚合成知识网络。
        </p>
        <div className="mock">
          <GraphMock>
            <svg viewBox="0 0 360 180" preserveAspectRatio="xMidYMid meet" aria-hidden="true">
              <g stroke="currentColor" strokeOpacity="0.18" strokeWidth="1">
                <line x1="180" y1="95" x2="80"  y2="50"  />
                <line x1="180" y1="95" x2="290" y2="40"  />
                <line x1="180" y1="95" x2="60"  y2="150" />
                <line x1="180" y1="95" x2="300" y2="150" />
                <line x1="80"  y1="50"  x2="60"  y2="150" />
                <line x1="290" y1="40"  x2="300" y2="150" />
              </g>
              <g>
                <circle cx="180" cy="95" r="12" fill="currentColor" fillOpacity="0.85" />
                <circle cx="180" cy="95" r="20" fill="none" stroke="currentColor" strokeOpacity="0.25" />
                <circle cx="80"  cy="50"  r="7" fill="#7DD3FC" />
                <circle cx="290" cy="40"  r="7" fill="#C4B5FD" />
                <circle cx="60"  cy="150" r="7" fill="#FCA5A5" />
                <circle cx="300" cy="150" r="7" fill="#FBBF24" />
              </g>
            </svg>
            <span className="hint">
              <LinkOutlined className="arr" /> 7 backlinks auto-built
            </span>
          </GraphMock>
        </div>
      </Card>

      <Card {...goer('/knowledge')}>
        <span className="more"><ArrowRightOutlined /></span>
        <div className="head"><span className="tag">03 · SEMANTIC SEARCH</span></div>
        <h3>用自然语言找文件</h3>
        <p className="desc">
          不用记文件名，直接描述要找的内容 —— 向量召回 + 相似度评分，命中即可跳转。
        </p>
        <div className="mock">
          <SearchMock>
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
          </SearchMock>
        </div>
      </Card>
    </Grid>
  );
};

export default Bento;
