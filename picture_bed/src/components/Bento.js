import React from 'react';
import styled from '@emotion/styled';
import {
  ThunderboltOutlined,
  NodeIndexOutlined,
  ApiOutlined,
  CloudUploadOutlined,
  SafetyCertificateOutlined,
  RocketOutlined,
} from '@ant-design/icons';

/**
 * Bento —— LinkCloud 特性宫格
 * Obsidian-inspired 双链能力 + 云存储能力的可视化卡片
 */
const Grid = styled.div`
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  grid-auto-rows: minmax(150px, auto);
  gap: 14px;

  @media (max-width: 1100px) {
    grid-template-columns: repeat(2, 1fr);
  }
  @media (max-width: 720px) {
    grid-template-columns: 1fr;
  }
`;

const Cell = styled.div`
  position: relative;
  overflow: hidden;
  padding: 18px;
  border-radius: 16px;
  background: ${p => p.theme.colors.panel};
  border: 1px solid ${p => p.theme.colors.border};
  transition: transform .25s cubic-bezier(.16,1,.3,1),
              border-color .2s, background-color .2s;

  &:hover {
    transform: translateY(-2px);
    border-color: ${p => p.theme.colors.borderStrong};
  }

  .ic {
    display: inline-flex; align-items: center; justify-content: center;
    width: 36px; height: 36px;
    border-radius: 10px;
    background: ${p => p.theme.colors.accent}22;
    color: ${p => p.theme.colors.accent};
    font-size: 18px;
    margin-bottom: 12px;
  }
  h4 {
    margin: 0 0 6px;
    font-size: 14px;
    color: ${p => p.theme.colors.text};
    letter-spacing: -.2px;
  }
  p {
    margin: 0;
    font-size: 12.5px;
    line-height: 1.55;
    color: ${p => p.theme.colors.text2};
  }
  &.wide  { grid-column: span 2; }
  &.tall  { grid-row: span 2; }
`;

const Glow = styled.div`
  position: absolute; inset: -40% -20% auto auto;
  width: 220px; height: 220px;
  background: radial-gradient(closest-side,
    ${p => p.theme.colors.accent}55, transparent 70%);
  filter: blur(28px);
  pointer-events: none;
  opacity: .55;
`;

const FEATURES = [
  {
    icon: <NodeIndexOutlined />,
    title: '双链知识图谱',
    desc: '上传即解析，AI 自动建立文件之间的隐式链接，像 Obsidian 一样涌现知识网络。',
    cls: 'wide',
  },
  {
    icon: <ThunderboltOutlined />,
    title: '秒级语义检索',
    desc: 'FAISS 向量索引 + DashScope embedding，毫秒级返回相关片段。',
  },
  {
    icon: <CloudUploadOutlined />,
    title: '分片断点续传',
    desc: '基于 MD5 秒传，断网自动续传，超大文件无压力。',
  },
  {
    icon: <ApiOutlined />,
    title: 'Qwen-VL 多模态',
    desc: '图片 / 文档 / 表格统一向量化，跨模态联想。',
  },
  {
    icon: <SafetyCertificateOutlined />,
    title: '端到端鉴权',
    desc: '基于 token 的会话校验，敏感操作可二次确认。',
  },
  {
    icon: <RocketOutlined />,
    title: '一键扩缩容',
    desc: 'FastDFS 集群水平扩展，Redis + MySQL 元数据双写。',
  },
];

const Bento = () => (
  <Grid>
    {FEATURES.map((f, i) => (
      <Cell key={i} className={f.cls || ''}>
        {i === 0 && <Glow />}
        <div className="ic">{f.icon}</div>
        <h4>{f.title}</h4>
        <p>{f.desc}</p>
      </Cell>
    ))}
  </Grid>
);

export default Bento;
