import React from 'react';
import styled from '@emotion/styled';
import { CheckCircleFilled, LoadingOutlined, ClockCircleOutlined, MinusCircleOutlined } from '@ant-design/icons';

const List = styled.div`
  display: flex; flex-direction: column;
`;

const Row = styled.div`
  display: flex; align-items: center; gap: 12px;
  padding: 11px 18px;
  border-top: 1px solid ${p => p.theme.colors.border};
  &:first-of-type { border-top: none; }
  transition: background ${p => p.theme.duration.base} ${p => p.theme.ease.out};
  &:hover { background: ${p => p.theme.colors.panelHover}; }

  .ext {
    width: 32px; height: 32px;
    border-radius: 7px;
    background: ${p => p.theme.colors.panel2};
    display: grid; place-items: center;
    font-size: 10px; font-weight: 600;
    color: ${p => p.theme.colors.text2};
    font-family: ${p => p.theme.fontFamily.mono};
    text-transform: uppercase;
    flex-shrink: 0;
  }
  .name {
    flex: 1; min-width: 0;
    color: ${p => p.theme.colors.text};
    font-size: 13px;
    white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
  }
`;

const Badge = styled.span`
  display: inline-flex; align-items: center; gap: 6px;
  font-size: 11px;
  font-family: ${p => p.theme.fontFamily.mono};
  letter-spacing: 0.3px;
  padding: 3px 9px;
  border-radius: 999px;
  border: 1px solid ${p => p.theme.colors.border};
  background: ${p => p.theme.colors.panel2};
  color: ${p =>
    p.$kind === 'done' ? p.theme.colors.success :
    p.$kind === 'embedding' ? p.theme.colors.accent :
    p.$kind === 'pending' ? p.theme.colors.text3 :
    p.theme.colors.text2};
`;

// 状态语义对齐后端能力：
//   done      → 已生成 wiki / 已被 AI 索引（来自 wiki_ready === 1）
//   pending   → 已上传但尚未索引（真实账号唯一 fallback 状态）
//   embedding → 仅出现在 demo 数据中，用于展示后续可扩展的中间态
//   queued    → 同上
const STATUS = {
  done:      { icon: <CheckCircleFilled />,    label: 'INDEXED' },
  embedding: { icon: <LoadingOutlined spin />, label: 'EMBEDDING' },
  queued:    { icon: <ClockCircleOutlined />,  label: 'QUEUED' },
  pending:   { icon: <MinusCircleOutlined />,  label: 'NOT INDEXED' },
};

const AIPipeline = ({ items = [] }) => (
  <List>
    {items.map((it, i) => {
      const s = STATUS[it.status] || STATUS.pending;
      return (
        <Row key={i}>
          <span className="ext">{(it.ext || '').slice(0, 4)}</span>
          <span className="name">{it.name}</span>
          <Badge $kind={it.status}>{s.icon}{s.label}</Badge>
        </Row>
      );
    })}
  </List>
);

export default AIPipeline;
