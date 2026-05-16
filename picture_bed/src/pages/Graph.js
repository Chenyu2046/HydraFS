import React, { useEffect, useMemo, useRef, useState } from 'react';
import styled from '@emotion/styled';
import { useTheme } from '@emotion/react';
import { useLocation, useNavigate } from 'react-router-dom';
import ForceGraph2D from 'react-force-graph-2d';
import { Input, Tag, Button, Empty, Spin, message } from 'antd';
import { SearchOutlined, ExpandOutlined, ReloadOutlined, NodeIndexOutlined } from '@ant-design/icons';
import { useAuth } from '../contexts/AuthContext';
import { fetchUserImages } from '../services/images';
import { Panel, PanelHeader, Pill } from '../components/primitives';
import { MOCK_GRAPH, buildGraphFromFiles, classifyFileType } from '../mock/graph';
import { copy } from '../lib/copy';

const Wrap = styled.div`
  display: grid;
  grid-template-columns: 1fr 320px;
  gap: 16px;
  height: calc(100vh - 56px - 28px - 64px);
  min-height: 560px;

  @media (max-width: 980px) {
    grid-template-columns: 1fr;
    height: auto;
  }
`;

const Stage = styled(Panel)`
  position: relative;
  overflow: hidden;
  display: flex; flex-direction: column;
  background:
    radial-gradient(circle at 30% 25%, ${p => p.theme.colors.accentSoft} 0%, transparent 50%),
    ${p => p.theme.colors.panel};
`;

const StageHead = styled.div`
  display: flex; align-items: center; gap: 12px;
  padding: 12px 16px;
  border-bottom: 1px solid ${p => p.theme.colors.border};
  background: ${p => p.theme.colors.panel}CC;
  backdrop-filter: blur(10px);
  position: relative; z-index: 5;

  h3 { margin: 0; font-size: 14px; font-weight: 600; color: ${p => p.theme.colors.text}; }
  .meta {
    margin-left: auto;
    font-family: ${p => p.theme.fontFamily.mono};
    font-size: 11px;
    color: ${p => p.theme.colors.text2};
    letter-spacing: 0.4px;
  }
`;

const StageBody = styled.div`
  flex: 1;
  position: relative;
`;

const Hint = styled.div`
  position: absolute;
  left: 14px; bottom: 12px;
  display: flex; gap: 12px; flex-wrap: wrap;
  font-family: ${p => p.theme.fontFamily.mono};
  font-size: 11px;
  color: ${p => p.theme.colors.text2};
  z-index: 5;
  letter-spacing: 0.3px;
  span { display: inline-flex; align-items: center; gap: 5px; }
  i { width: 7px; height: 7px; border-radius: 999px; display: inline-block; }
`;

const Sidebar = styled.div`
  display: flex; flex-direction: column; gap: 14px;
  min-height: 0;
`;

const NodeList = styled.div`
  flex: 1;
  overflow: auto;
  display: flex; flex-direction: column;
`;
const NodeItem = styled.button`
  text-align: left;
  width: 100%;
  border: none;
  background: ${p => p.$active ? p.theme.colors.accentSoft : 'transparent'};
  color: ${p => p.$active ? p.theme.colors.accent : p.theme.colors.text};
  padding: 10px 14px;
  border-bottom: 1px solid ${p => p.theme.colors.border};
  cursor: pointer;
  font: inherit;
  display: flex; align-items: center; gap: 10px;
  transition: background ${p => p.theme.duration.base} ${p => p.theme.ease.out};

  &:hover { background: ${p => p.$active ? p.theme.colors.accentSoft : p.theme.colors.panelHover}; }

  i {
    width: 7px; height: 7px; border-radius: 999px;
    background: ${p => p.color};
    flex-shrink: 0;
    box-shadow: 0 0 0 3px ${p => p.theme.colors.panel};
  }
  .name {
    flex: 1; min-width: 0;
    font-size: 13px;
    white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
  }
`;

const TYPE_KEY = { doc:'graphDoc', image:'graphImage', code:'graphCode', archive:'graphArchive', other:'graphOther' };

const Graph = () => {
  const theme = useTheme();
  const { user, logout } = useAuth();
  const nav = useNavigate();
  const loc = useLocation();
  const fgRef = useRef(null);
  const stageRef = useRef(null);
  const [size, setSize] = useState({ w: 800, h: 560 });
  const [data, setData] = useState({ nodes: [], links: [] });
  const [loading, setLoading] = useState(true);
  const [usingMock, setUsingMock] = useState(false);
  const [selected, setSelected] = useState(null);
  const [hover, setHover] = useState(null);
  const [keyword, setKeyword] = useState('');

  const focusId = useMemo(() => new URLSearchParams(loc.search).get('focus'), [loc.search]);

  const load = async () => {
    setLoading(true);
    try {
      const files = await fetchUserImages(user, { count: 200 });
      const built = buildGraphFromFiles(files);
      if (built && built.nodes.length >= 4) {
        setData(built); setUsingMock(false);
      } else {
        setData(MOCK_GRAPH); setUsingMock(true);
      }
    } catch (e) {
      if (e.tokenExpired) { message.error(copy.auth.expired); logout(); return; }
      setData(MOCK_GRAPH); setUsingMock(true);
    } finally { setLoading(false); }
  };

  useEffect(() => { if (user?.token) load(); /* eslint-disable-next-line */ }, [user]);

  // 监听 stage 尺寸
  useEffect(() => {
    if (!stageRef.current) return;
    const ro = new ResizeObserver(entries => {
      for (const e of entries) {
        setSize({ w: Math.max(320, e.contentRect.width), h: Math.max(360, e.contentRect.height) });
      }
    });
    ro.observe(stageRef.current);
    return () => ro.disconnect();
  }, []);

  // focus from URL
  useEffect(() => {
    if (!focusId || !data.nodes.length || !fgRef.current) return;
    const node = data.nodes.find(n => n.id === focusId);
    if (node) {
      setSelected(node);
      setTimeout(() => {
        fgRef.current.centerAt(node.x || 0, node.y || 0, 600);
        fgRef.current.zoom(2.4, 600);
      }, 600);
    }
  }, [focusId, data]);

  const filteredList = useMemo(() => {
    if (!keyword.trim()) return data.nodes;
    const k = keyword.toLowerCase();
    return data.nodes.filter(n => (n.label || n.id || '').toLowerCase().includes(k));
  }, [data, keyword]);

  const neighbors = useMemo(() => {
    const id = selected?.id || hover?.id;
    if (!id) return null;
    const set = new Set([id]);
    for (const l of data.links) {
      const s = typeof l.source === 'object' ? l.source.id : l.source;
      const t = typeof l.target === 'object' ? l.target.id : l.target;
      if (s === id) set.add(t);
      if (t === id) set.add(s);
    }
    return set;
  }, [selected, hover, data.links]);

  const nodeColor = (n) => theme.colors[TYPE_KEY[n.type] || 'graphOther'];

  return (
    <div>
      <div style={{ display: 'flex', alignItems: 'flex-end', justifyContent: 'space-between', marginBottom: 16 }}>
        <div>
          <h1 style={{ margin: '0 0 4px', fontSize: 24, fontWeight: 700, letterSpacing: '-0.4px' }}>Knowledge Graph</h1>
          <p style={{ margin: 0, fontSize: 13, opacity: 0.7 }}>
            {copy.graph.hint}
          </p>
        </div>
        <div style={{ display: 'flex', gap: 8 }}>
          <Button icon={<ExpandOutlined />} onClick={() => fgRef.current?.zoomToFit(600, 60)}>适配视图</Button>
          <Button icon={<ReloadOutlined />} onClick={load}>刷新</Button>
        </div>
      </div>

      <Wrap>
        <Stage>
          <StageHead>
            <NodeIndexOutlined style={{ color: theme.colors.accent }} />
            <h3>Force-directed View</h3>
            {usingMock && <Pill>DEMO DATA</Pill>}
            {!usingMock && data.nodes.length > 0 && data.links.length === 0 && (
              <Pill>RELATIONS PENDING</Pill>
            )}
            <span className="meta">{data.nodes.length} NODES · {data.links.length} EDGES</span>
          </StageHead>
          <StageBody ref={stageRef}>
            {loading ? (
              <div style={{ position: 'absolute', inset: 0, display: 'grid', placeItems: 'center' }}><Spin /></div>
            ) : data.nodes.length === 0 ? (
              <div style={{ position: 'absolute', inset: 0, display: 'grid', placeItems: 'center', padding: 32, textAlign: 'center' }}>
                <Empty description={copy.empty.graph} />
              </div>
            ) : (
              <ForceGraph2D
                ref={fgRef}
                graphData={data}
                width={size.w}
                height={size.h}
                backgroundColor="rgba(0,0,0,0)"
                nodeRelSize={5}
                cooldownTicks={120}
                linkColor={(l) => {
                  if (!neighbors) return theme.colors.graphEdge;
                  const s = typeof l.source === 'object' ? l.source.id : l.source;
                  const t = typeof l.target === 'object' ? l.target.id : l.target;
                  return (neighbors.has(s) && neighbors.has(t)) ? theme.colors.graphEdgeHover : theme.colors.graphEdge;
                }}
                linkWidth={(l) => {
                  if (!neighbors) return 0.6;
                  const s = typeof l.source === 'object' ? l.source.id : l.source;
                  const t = typeof l.target === 'object' ? l.target.id : l.target;
                  return (neighbors.has(s) && neighbors.has(t)) ? 1.6 : 0.4;
                }}
                nodeCanvasObject={(node, ctx, scale) => {
                  const c = nodeColor(node);
                  const isFocus = (selected?.id === node.id) || (hover?.id === node.id);
                  const dim = neighbors && !neighbors.has(node.id);
                  ctx.globalAlpha = dim ? 0.25 : 1;
                  // halo
                  if (isFocus) {
                    ctx.beginPath();
                    ctx.arc(node.x, node.y, 12, 0, 2 * Math.PI);
                    ctx.fillStyle = c + '33';
                    ctx.fill();
                  }
                  ctx.beginPath();
                  ctx.arc(node.x, node.y, isFocus ? 6.5 : 4.5, 0, 2 * Math.PI);
                  ctx.fillStyle = c;
                  ctx.fill();
                  ctx.lineWidth = 1.2;
                  ctx.strokeStyle = theme.colors.bg;
                  ctx.stroke();

                  // label when zoomed in or focused
                  if (scale > 1.4 || isFocus) {
                    const label = node.label || node.id;
                    ctx.font = `${11}px ${theme.fontFamily.mono.split(',')[0].replace(/['"]/g,'')}, monospace`;
                    ctx.fillStyle = theme.colors.text2;
                    ctx.fillText(label.length > 28 ? label.slice(0, 27) + '…' : label, node.x + 9, node.y + 4);
                  }
                  ctx.globalAlpha = 1;
                }}
                onNodeHover={n => setHover(n || null)}
                onNodeClick={n => {
                  setSelected(n);
                  fgRef.current.centerAt(n.x, n.y, 500);
                  fgRef.current.zoom(2.2, 500);
                }}
                onBackgroundClick={() => setSelected(null)}
              />
            )}
            <Hint>
              <span><i style={{ background: theme.colors.graphDoc }} />doc</span>
              <span><i style={{ background: theme.colors.graphImage }} />image</span>
              <span><i style={{ background: theme.colors.graphCode }} />code</span>
              <span><i style={{ background: theme.colors.graphArchive }} />archive</span>
              <span><i style={{ background: theme.colors.graphOther }} />other</span>
            </Hint>
          </StageBody>
        </Stage>

        <Sidebar>
          <Panel>
            <PanelHeader>
              <h3>Selection</h3>
              {selected && <Pill>{(selected.type || 'other').toUpperCase()}</Pill>}
            </PanelHeader>
            <div style={{ padding: '14px 16px' }}>
              {!selected ? (
                <div style={{ fontSize: 12.5, color: theme.colors.text2, lineHeight: 1.6 }}>
                  {copy.graph.selectHint}
                </div>
              ) : (
                <>
                  <div style={{ fontSize: 14, fontWeight: 600, color: theme.colors.text, wordBreak: 'break-all', marginBottom: 8 }}>
                    {selected.label || selected.id}
                  </div>
                  <div style={{ display: 'flex', flexWrap: 'wrap', gap: 4, marginBottom: 12 }}>
                    {(selected.tags || []).map((t, i) => <Tag key={i} bordered={false}>#{t}</Tag>)}
                    <Tag bordered={false}>{classifyFileType(selected.type)}</Tag>
                  </div>
                  {!usingMock && !String(selected.id).startsWith('mock') && (
                    <Button type="primary" block onClick={() => nav('/wiki/' + selected.id)}>
                      打开 Wiki
                    </Button>
                  )}
                </>
              )}
            </div>
          </Panel>

          <Panel style={{ flex: 1, display: 'flex', flexDirection: 'column', minHeight: 0 }}>
            <PanelHeader>
              <h3>Nodes</h3>
              <span className="subtitle">{filteredList.length}/{data.nodes.length}</span>
            </PanelHeader>
            <div style={{ padding: '10px 14px' }}>
              <Input
                size="small"
                prefix={<SearchOutlined style={{ opacity: 0.5 }} />}
                placeholder={copy.search.placeholderNodes}
                value={keyword}
                onChange={e => setKeyword(e.target.value)}
                allowClear
              />
            </div>
            <NodeList>
              {filteredList.slice(0, 200).map(n => (
                <NodeItem
                  key={n.id}
                  color={nodeColor(n)}
                  $active={selected?.id === n.id}
                  onClick={() => {
                    setSelected(n);
                    if (n.x != null) {
                      fgRef.current?.centerAt(n.x, n.y, 500);
                      fgRef.current?.zoom(2.2, 500);
                    }
                  }}
                >
                  <i />
                  <span className="name">{n.label || n.id}</span>
                </NodeItem>
              ))}
            </NodeList>
          </Panel>
        </Sidebar>
      </Wrap>
    </div>
  );
};

export default Graph;
