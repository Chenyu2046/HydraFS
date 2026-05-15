import React from 'react';
import styled from '@emotion/styled';
import { useNavigate } from 'react-router-dom';
import { ClockCircleOutlined } from '@ant-design/icons';

const Grid = styled.div`
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(240px, 1fr));
  gap: 1px;
  background: ${p => p.theme.colors.border};
  border-top: 1px solid ${p => p.theme.colors.border};
`;

const Card = styled.div`
  padding: 16px 18px 14px;
  background: ${p => p.theme.colors.panel};
  cursor: pointer;
  transition: background ${p => p.theme.duration.base} ${p => p.theme.ease.out};
  &:hover { background: ${p => p.theme.colors.panelHover}; }

  .title {
    color: ${p => p.theme.colors.text};
    font-size: 13.5px; font-weight: 600;
    letter-spacing: -0.1px;
    margin-bottom: 6px;
    white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
  }
  .summary {
    color: ${p => p.theme.colors.text2};
    font-size: 12.5px;
    line-height: 1.55;
    display: -webkit-box;
    -webkit-line-clamp: 2;
    -webkit-box-orient: vertical;
    overflow: hidden;
    margin-bottom: 10px;
    min-height: 38px;
  }
  .meta {
    display: flex; align-items: center; gap: 8px; flex-wrap: wrap;
    font-size: 11px;
    color: ${p => p.theme.colors.text3};
    font-family: ${p => p.theme.fontFamily.mono};
  }
  .tag {
    padding: 2px 7px;
    border-radius: 4px;
    background: ${p => p.theme.colors.panel2};
    color: ${p => p.theme.colors.text2};
    font-family: ${p => p.theme.fontFamily.mono};
    border: 1px solid ${p => p.theme.colors.border};
  }
  .time {
    margin-left: auto;
    display: inline-flex; align-items: center; gap: 4px;
  }
`;

const RecentNodes = ({ items = [] }) => {
  const nav = useNavigate();
  return (
    <Grid>
      {items.map((it, i) => (
        <Card key={it.md5 || i} onClick={() => it.md5 && !String(it.md5).startsWith('mock') && nav(`/wiki/${it.md5}`)}>
          <div className="title">{it.title}</div>
          <div className="summary">{it.summary}</div>
          <div className="meta">
            {(it.tags || []).slice(0, 3).map((t, k) => <span className="tag" key={k}>#{t}</span>)}
            <span className="time"><ClockCircleOutlined />{it.time}</span>
          </div>
        </Card>
      ))}
    </Grid>
  );
};

export default RecentNodes;
