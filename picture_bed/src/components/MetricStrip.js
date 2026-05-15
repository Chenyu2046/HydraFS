import React from 'react';
import styled from '@emotion/styled';

/**
 * MetricStrip
 * Hero canvas 底部的轻量 trust metrics
 * - 不像 StatBar 那样横一条带 border
 * - 字号层级靠上下叠加（22px 数字 + 11px label）
 * - demo 数据用更淡的颜色 + ".demo" 角标，不糊脸
 */

const Strip = styled.div`
  display: flex; flex-wrap: wrap;
  gap: 36px 48px;
  padding-top: 28px;
  margin-top: 36px;
  border-top: 1px dashed ${p => p.theme.colors.border};
`;

const Cell = styled.div`
  display: flex; flex-direction: column; gap: 4px;
  position: relative;

  .num {
    font-family: ${p => p.theme.fontFamily.mono};
    font-size: 26px;
    font-weight: 700;
    letter-spacing: -0.5px;
    color: ${p => p.$demo ? p.theme.colors.text2 : p.theme.colors.text};
    line-height: 1;
    .unit {
      font-size: 14px;
      font-weight: 500;
      margin-left: 2px;
      opacity: 0.6;
    }
  }
  .label {
    font-size: 11.5px;
    text-transform: uppercase;
    letter-spacing: 0.6px;
    color: ${p => p.theme.colors.text3};
    font-family: ${p => p.theme.fontFamily.mono};
  }
  .demo {
    position: absolute; top: -4px; right: -8px;
    font-size: 9px; font-weight: 700;
    letter-spacing: 0.5px;
    padding: 1px 5px;
    border-radius: 4px;
    /* 用 warn 色让 dark 主题也能看清，light 主题也保持低饱和但可识别 */
    background: ${p => p.theme.colors.warn}26;
    color: ${p => p.theme.colors.warn};
    border: 1px solid ${p => p.theme.colors.warn}55;
  }
`;

const MetricStrip = ({ items = [] }) => (
  <Strip>
    {items.map((it, i) => (
      <Cell key={i} $demo={it.demo}>
        <div className="num">
          {it.value}
          {it.unit && <span className="unit">{it.unit}</span>}
        </div>
        <div className="label">{it.label}</div>
        {it.demo && <span className="demo">DEMO</span>}
      </Cell>
    ))}
  </Strip>
);

export default MetricStrip;
