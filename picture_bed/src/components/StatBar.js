import React from 'react';
import styled from '@emotion/styled';
import { Panel } from './primitives';

const Grid = styled.div`
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
  gap: 1px;
  background: ${p => p.theme.colors.border};
  border: 1px solid ${p => p.theme.colors.border};
  border-radius: 12px;
  overflow: hidden;
`;

const Cell = styled.div`
  padding: 16px 18px;
  background: ${p => p.theme.colors.panel};
  display: flex; flex-direction: column; gap: 6px;
  position: relative;
  transition: background ${p => p.theme.duration.base} ${p => p.theme.ease.out};
  &:hover { background: ${p => p.theme.colors.panelHover}; }

  .label {
    font-size: 11px;
    text-transform: uppercase;
    letter-spacing: 0.6px;
    color: ${p => p.theme.colors.text3};
    font-weight: 500;
  }
  .value {
    font-family: ${p => p.theme.fontFamily.mono};
    font-size: 24px;
    font-weight: 600;
    color: ${p => p.theme.colors.text};
    letter-spacing: -0.5px;
    line-height: 1.1;
  }
  .unit {
    font-size: 13px;
    color: ${p => p.theme.colors.text2};
    margin-left: 4px;
    font-weight: 500;
  }
  .delta {
    font-size: 11px;
    color: ${p => p.theme.colors.success};
    font-family: ${p => p.theme.fontFamily.mono};
  }
  .accent {
    position: absolute; top: 14px; right: 14px;
    width: 6px; height: 6px;
    border-radius: 999px;
    background: ${p => p.theme.colors.accent};
    box-shadow: 0 0 0 4px ${p => p.theme.colors.accentSoft};
  }
`;

const StatBar = ({ items = [] }) => (
  <Grid>
    {items.map((it, i) => (
      <Cell key={i}>
        {it.live && <span className="accent" />}
        <div className="label">{it.label}</div>
        <div className="value">
          {it.value}
          {it.unit && <span className="unit">{it.unit}</span>}
        </div>
        {it.delta && <div className="delta">{it.delta}</div>}
      </Cell>
    ))}
  </Grid>
);

export default StatBar;
