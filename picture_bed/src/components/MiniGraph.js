import React, { useEffect, useMemo, useRef, useState } from 'react';
import styled from '@emotion/styled';
import { useTheme } from '@emotion/react';

/**
 * 轻量纯 SVG 力导向图（首页 Hero 内嵌，零依赖）
 * 真正的交互式 Graph 页才会动态加载 react-force-graph-2d
 */

const Wrap = styled.div`
  width: 100%;
  height: ${p => p.$h || 280}px;
  position: relative;
  overflow: hidden;
  border-radius: 12px;
  background:
    radial-gradient(circle at 30% 20%, ${p => p.theme.colors.accentSoft} 0%, transparent 55%),
    ${p => p.theme.colors.panel2};
`;

const Grid = styled.svg`
  position: absolute; inset: 0;
  width: 100%; height: 100%;
  pointer-events: none;
`;

const Svg = styled.svg`
  position: absolute; inset: 0;
  width: 100%; height: 100%;
`;

const Legend = styled.div`
  position: absolute;
  left: 14px; bottom: 12px;
  display: flex; gap: 12px;
  font-size: 11px;
  color: ${p => p.theme.colors.text2};
  font-family: ${p => p.theme.fontFamily.mono};
  letter-spacing: 0.3px;
  z-index: 2;
  span { display: inline-flex; align-items: center; gap: 5px; }
  i { width: 7px; height: 7px; border-radius: 999px; display: inline-block; }
`;

const Stat = styled.div`
  position: absolute;
  right: 14px; top: 12px;
  font-size: 11px;
  color: ${p => p.theme.colors.text2};
  font-family: ${p => p.theme.fontFamily.mono};
  letter-spacing: 0.3px;
  z-index: 2;
`;

const TYPE_KEY = { doc: 'graphDoc', image: 'graphImage', code: 'graphCode', archive: 'graphArchive', other: 'graphOther' };

/**
 * 极简 Fruchterman-Reingold 力导向（无依赖）
 */
const layout = (nodes, links, w, h, iter = 220) => {
  const N = nodes.length;
  if (N === 0) return [];
  const area = w * h;
  const k = Math.sqrt(area / N) * 0.65;
  const pos = nodes.map(() => ({
    x: w / 2 + (Math.random() - 0.5) * w * 0.6,
    y: h / 2 + (Math.random() - 0.5) * h * 0.6,
  }));
  const idx = new Map(nodes.map((n, i) => [n.id, i]));
  let t = w / 8;
  for (let it = 0; it < iter; it++) {
    const disp = pos.map(() => ({ x: 0, y: 0 }));
    // repulsive
    for (let i = 0; i < N; i++) {
      for (let j = i + 1; j < N; j++) {
        let dx = pos[i].x - pos[j].x;
        let dy = pos[i].y - pos[j].y;
        let d2 = dx * dx + dy * dy;
        if (d2 < 0.01) { dx = Math.random(); dy = Math.random(); d2 = dx*dx+dy*dy; }
        const f = (k * k) / d2;
        const fx = dx * f, fy = dy * f;
        disp[i].x += fx; disp[i].y += fy;
        disp[j].x -= fx; disp[j].y -= fy;
      }
    }
    // attractive
    for (const l of links) {
      const a = idx.get(l.source), b = idx.get(l.target);
      if (a == null || b == null) continue;
      const dx = pos[a].x - pos[b].x;
      const dy = pos[a].y - pos[b].y;
      const d = Math.sqrt(dx * dx + dy * dy) + 0.01;
      const f = (d * d) / k;
      const fx = (dx / d) * f, fy = (dy / d) * f;
      disp[a].x -= fx; disp[a].y -= fy;
      disp[b].x += fx; disp[b].y += fy;
    }
    // limit
    for (let i = 0; i < N; i++) {
      const d = Math.sqrt(disp[i].x * disp[i].x + disp[i].y * disp[i].y) + 0.01;
      pos[i].x += (disp[i].x / d) * Math.min(d, t);
      pos[i].y += (disp[i].y / d) * Math.min(d, t);
      // bounding
      pos[i].x = Math.max(20, Math.min(w - 20, pos[i].x));
      pos[i].y = Math.max(20, Math.min(h - 20, pos[i].y));
    }
    t *= 0.96;
  }
  return pos;
};

const MiniGraph = ({ data, height = 280, animated = true }) => {
  const theme = useTheme();
  const wrapRef = useRef(null);
  const [size, setSize] = useState({ w: 600, h: height });
  const [hover, setHover] = useState(null);

  useEffect(() => {
    if (!wrapRef.current) return;
    const ro = new ResizeObserver(entries => {
      for (const e of entries) {
        setSize({ w: Math.max(320, e.contentRect.width), h: height });
      }
    });
    ro.observe(wrapRef.current);
    return () => ro.disconnect();
  }, [height]);

  const positions = useMemo(
    () => layout(data.nodes, data.links, size.w, size.h),
    [data, size.w, size.h]
  );

  const nodeIdxById = useMemo(() => {
    const m = new Map(); data.nodes.forEach((n, i) => m.set(n.id, i)); return m;
  }, [data]);

  const neighborSet = useMemo(() => {
    if (hover == null) return null;
    const s = new Set([hover]);
    for (const l of data.links) {
      if (l.source === hover) s.add(l.target);
      if (l.target === hover) s.add(l.source);
    }
    return s;
  }, [hover, data]);

  return (
    <Wrap ref={wrapRef} $h={height}>
      <Grid viewBox={`0 0 ${size.w} ${size.h}`}>
        <defs>
          <pattern id="hgrid" width="32" height="32" patternUnits="userSpaceOnUse">
            <path d="M 32 0 L 0 0 0 32" fill="none" stroke={theme.colors.grid} strokeWidth="1" />
          </pattern>
        </defs>
        <rect width="100%" height="100%" fill="url(#hgrid)" />
      </Grid>

      <Svg viewBox={`0 0 ${size.w} ${size.h}`}>
        {/* edges */}
        {data.links.map((l, i) => {
          const a = positions[nodeIdxById.get(l.source)];
          const b = positions[nodeIdxById.get(l.target)];
          if (!a || !b) return null;
          const active = neighborSet && (neighborSet.has(l.source) && neighborSet.has(l.target));
          const dim = neighborSet && !active;
          return (
            <line key={i}
              x1={a.x} y1={a.y} x2={b.x} y2={b.y}
              stroke={active ? theme.colors.graphEdgeHover : theme.colors.graphEdge}
              strokeWidth={active ? 1.2 : 0.8}
              opacity={dim ? 0.25 : 1}
              style={{ transition: 'all 220ms cubic-bezier(0.16,1,0.3,1)' }}
            />
          );
        })}
        {/* nodes */}
        {data.nodes.map((n, i) => {
          const p = positions[i]; if (!p) return null;
          const c = theme.colors[TYPE_KEY[n.type] || 'graphOther'];
          const isHover = hover === n.id;
          const dim = neighborSet && !neighborSet.has(n.id);
          const r = isHover ? 7 : 4.5;
          return (
            <g key={n.id}
               onMouseEnter={() => setHover(n.id)}
               onMouseLeave={() => setHover(null)}
               style={{ cursor: 'pointer' }}
               opacity={dim ? 0.3 : 1}>
              {isHover && (
                <circle cx={p.x} cy={p.y} r={14} fill={c} opacity={0.18} />
              )}
              <circle cx={p.x} cy={p.y} r={r} fill={c}
                      stroke={theme.colors.bg} strokeWidth={1.2}
                      style={{ transition: 'r 200ms cubic-bezier(0.16,1,0.3,1)' }} />
              {(isHover || (animated && i % 7 === 0)) && (
                <text x={p.x + 9} y={p.y + 3}
                      fill={theme.colors.text2}
                      fontSize="10"
                      fontFamily={theme.fontFamily.mono}
                      style={{ pointerEvents: 'none' }}>
                  {n.label.length > 22 ? n.label.slice(0, 21) + '…' : n.label}
                </text>
              )}
            </g>
          );
        })}
      </Svg>

      <Stat>{data.nodes.length} NODES · {data.links.length} EDGES</Stat>
      <Legend>
        <span><i style={{ background: theme.colors.graphDoc }} />doc</span>
        <span><i style={{ background: theme.colors.graphImage }} />image</span>
        <span><i style={{ background: theme.colors.graphCode }} />code</span>
        <span><i style={{ background: theme.colors.graphArchive }} />archive</span>
      </Legend>
    </Wrap>
  );
};

export default MiniGraph;
