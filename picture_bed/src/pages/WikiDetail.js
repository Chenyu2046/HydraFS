import React, { useEffect, useState, useMemo } from 'react';
import { useParams, useNavigate } from 'react-router-dom';
import styled from '@emotion/styled';
import { Tag, Button, Spin, Empty, message } from 'antd';
import {
  ArrowLeftOutlined, FileOutlined, NodeIndexOutlined, LinkOutlined,
  BookOutlined,
} from '@ant-design/icons';
import { useAuth } from '../contexts/AuthContext';
import { fetchWiki, fetchBacklinks } from '../services/ai';
import { Panel, PanelHeader, PanelBody, Pill } from '../components/primitives';

const Layout = styled.div`
  display: grid;
  grid-template-columns: 1fr 320px;
  gap: 18px;

  @media (max-width: 980px) { grid-template-columns: 1fr; }
`;

const Toolbar = styled.div`
  display: flex; align-items: center; gap: 8px;
  margin-bottom: 16px;
`;

const Title = styled.h1`
  margin: 0 0 8px;
  font-size: 28px; font-weight: 700;
  letter-spacing: -0.5px;
  color: ${p => p.theme.colors.text};
`;

const Meta = styled.div`
  display: flex; gap: 8px; flex-wrap: wrap;
  font-family: ${p => p.theme.fontFamily.mono};
  font-size: 11.5px;
  color: ${p => p.theme.colors.text2};
  margin-bottom: 18px;
  span.kv {
    padding: 2px 8px;
    border: 1px solid ${p => p.theme.colors.border};
    border-radius: 999px;
    background: ${p => p.theme.colors.panel2};
  }
`;

const Block = styled.section`
  margin-bottom: 18px;
  h3 {
    font-size: 11.5px;
    text-transform: uppercase;
    letter-spacing: 0.6px;
    color: ${p => p.theme.colors.text3};
    margin: 0 0 10px;
    font-weight: 600;
    display: flex; align-items: center; gap: 6px;
  }
`;

const Summary = styled.div`
  background: ${p => p.theme.colors.panel};
  border: 1px solid ${p => p.theme.colors.border};
  border-radius: 12px;
  padding: 18px 22px;
  font-size: 14.5px;
  line-height: 1.75;
  color: ${p => p.theme.colors.text};
  white-space: pre-wrap;
`;

const Outline = styled.ol`
  margin: 0;
  padding: 0;
  list-style: none;
  counter-reset: oo;
  li {
    counter-increment: oo;
    padding: 10px 0 10px 30px;
    border-bottom: 1px dashed ${p => p.theme.colors.border};
    color: ${p => p.theme.colors.text};
    font-size: 13.5px;
    position: relative;
    line-height: 1.55;
    &::before {
      content: counter(oo, decimal-leading-zero);
      position: absolute; left: 0; top: 10px;
      font-family: ${p => p.theme.fontFamily.mono};
      font-size: 11px;
      color: ${p => p.theme.colors.text3};
      letter-spacing: 0.4px;
    }
    &:last-child { border-bottom: none; }
  }
`;

const Backlink = styled.button`
  border: 1px solid ${p => p.theme.colors.border};
  background: ${p => p.theme.colors.panel};
  border-radius: 10px;
  padding: 12px 14px;
  text-align: left;
  width: 100%;
  cursor: pointer;
  margin-bottom: 8px;
  color: inherit; font: inherit;
  transition: all ${p => p.theme.duration.base} ${p => p.theme.ease.out};

  &:hover {
    border-color: ${p => p.theme.colors.accentBorder};
    background: ${p => p.theme.colors.accentSoft};
    transform: translateY(-1px);
  }
  .concept {
    font-size: 11.5px;
    color: ${p => p.theme.colors.accent};
    font-family: ${p => p.theme.fontFamily.mono};
    text-transform: uppercase;
    letter-spacing: 0.4px;
  }
  .ref {
    font-size: 13px;
    color: ${p => p.theme.colors.text};
    margin-top: 4px;
    display: flex; align-items: center; gap: 8px;
    word-break: break-all;
  }
`;

const WikiDetail = () => {
  const { md5 } = useParams();
  const nav = useNavigate();
  const { user, logout } = useAuth();
  const [wiki, setWiki] = useState(null);
  const [backlinks, setBacklinks] = useState([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);

  useEffect(() => {
    if (!user || !md5) return;
    setLoading(true); setError(null);
    Promise.all([fetchWiki(md5, user), fetchBacklinks(md5, user)])
      .then(([w, b]) => { setWiki(w); setBacklinks(b || []); })
      .catch(e => {
        if (e.tokenExpired) { message.error('登录已过期'); logout(); return; }
        setError(e.message || '加载失败');
      })
      .finally(() => setLoading(false));
  }, [md5, user, logout]);

  const tags = useMemo(() => { try { return wiki?.tags ? JSON.parse(wiki.tags) : []; } catch { return []; } }, [wiki]);
  const outline = useMemo(() => { try { return wiki?.outline ? JSON.parse(wiki.outline) : []; } catch { return []; } }, [wiki]);
  const links = wiki?.links || [];

  if (loading) return <div style={{ padding: 80, textAlign: 'center' }}><Spin /></div>;
  if (error)   return <Empty description={error} />;
  if (!wiki)   return <Empty description="该 Wiki 节点不存在" />;

  return (
    <div>
      <Toolbar>
        <Button icon={<ArrowLeftOutlined />} onClick={() => nav(-1)}>返回</Button>
        <Button icon={<FileOutlined />} onClick={() => nav('/files')}>所有文件</Button>
        <Button icon={<NodeIndexOutlined />} onClick={() => nav('/graph?focus=' + encodeURIComponent(md5))}>
          在图谱中查看
        </Button>
        <span style={{ flex: 1 }} />
        <Pill><BookOutlined /> WIKI</Pill>
      </Toolbar>

      <Layout>
        <div>
          <Title>{wiki.title || '未命名 Wiki'}</Title>
          <Meta>
            <span className="kv">SOURCE · {wiki.source?.filename || '-'}</span>
            <span className="kv">TYPE · {(wiki.source?.type || '').toUpperCase() || '-'}</span>
            <span className="kv">MD5 · {(md5 || '').slice(0, 12)}…</span>
          </Meta>

          {tags.length > 0 && (
            <Block>
              <h3>Tags</h3>
              <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6 }}>
                {tags.map((t, i) => <Tag key={i} color="purple" bordered={false}>#{t}</Tag>)}
              </div>
            </Block>
          )}

          {wiki.summary && (
            <Block>
              <h3>Summary</h3>
              <Summary>{wiki.summary}</Summary>
            </Block>
          )}

          {outline.length > 0 && (
            <Block>
              <h3>Outline</h3>
              <Panel><PanelBody $pad="6px 18px">
                <Outline>{outline.map((it, i) => <li key={i}>{it}</li>)}</Outline>
              </PanelBody></Panel>
            </Block>
          )}

          {links.length > 0 && (
            <Block>
              <h3><LinkOutlined /> Concept Links</h3>
              <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6 }}>
                {links.map((l, i) => <Tag key={i} bordered={false} color="blue">{l}</Tag>)}
              </div>
            </Block>
          )}
        </div>

        <aside>
          <Panel>
            <PanelHeader>
              <h3>Backlinks</h3>
              <span className="subtitle">{backlinks.length}</span>
            </PanelHeader>
            <PanelBody $pad="14px">
              {backlinks.length === 0 ? (
                <Empty image={Empty.PRESENTED_IMAGE_SIMPLE} description={<span style={{ fontSize: 12 }}>暂无反向链接</span>} />
              ) : (
                backlinks.map((bl, i) => (
                  <div key={i} style={{ marginBottom: 14 }}>
                    {(bl.referenced_by || []).map((ref, j) => (
                      <Backlink key={j} onClick={() => nav('/wiki/' + ref.md5)}>
                        <div className="concept">#{bl.concept}</div>
                        <div className="ref"><FileOutlined />{ref.filename}</div>
                      </Backlink>
                    ))}
                  </div>
                ))
              )}
            </PanelBody>
          </Panel>
        </aside>
      </Layout>
    </div>
  );
};

export default WikiDetail;
