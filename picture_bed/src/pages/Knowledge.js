import React, { useEffect, useMemo, useState } from 'react';
import styled from '@emotion/styled';
import { Input, Button, Tag, message, Skeleton, Empty } from 'antd';
import {
  SearchOutlined, ReloadOutlined, BookOutlined, ArrowRightOutlined,
  ThunderboltOutlined,
} from '@ant-design/icons';
import { useNavigate } from 'react-router-dom';
import { useAuth } from '../contexts/AuthContext';
import { fetchUserImages } from '../services/images';
import { describeFileByMd5, rebuildIndex } from '../services/ai';
import { Panel, PanelHeader, PanelBody, SectionTitle, Pill } from '../components/primitives';

const PageHead = styled.div`
  margin-bottom: 18px;
  h1 {
    margin: 0 0 4px;
    font-size: 24px; font-weight: 700;
    letter-spacing: -0.4px;
    color: ${p => p.theme.colors.text};
  }
  p { margin: 0; font-size: 13px; color: ${p => p.theme.colors.text2}; }
`;

const Hint = styled.div`
  font-size: 12px;
  color: ${p => p.theme.colors.text3};
  font-family: ${p => p.theme.fontFamily.mono};
  display: flex; align-items: center; gap: 8px; flex-wrap: wrap;
  .ok { color: ${p => p.theme.colors.success}; }
`;

const NodeGrid = styled.div`
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 14px;
`;

const NodeCard = styled.div`
  background: ${p => p.theme.colors.panel};
  border: 1px solid ${p => p.theme.colors.border};
  border-radius: 12px;
  padding: 16px 18px 14px;
  cursor: pointer;
  transition: all ${p => p.theme.duration.base} ${p => p.theme.ease.out};
  display: flex; flex-direction: column; gap: 8px;

  &:hover {
    border-color: ${p => p.theme.colors.borderStrong};
    transform: translateY(-2px);
    box-shadow: ${p => p.theme.shadow.md};
  }

  .head {
    display: flex; align-items: flex-start; gap: 10px;
    .icon {
      width: 32px; height: 32px;
      border-radius: 8px;
      display: grid; place-items: center;
      background: ${p => p.theme.colors.accentSoft};
      color: ${p => p.theme.colors.accent};
      flex-shrink: 0;
    }
    .name {
      flex: 1; min-width: 0;
      color: ${p => p.theme.colors.text};
      font-size: 14px; font-weight: 600;
      letter-spacing: -0.1px;
      word-break: break-word;
      line-height: 1.35;
    }
  }
  .summary {
    color: ${p => p.theme.colors.text2};
    font-size: 12.5px; line-height: 1.6;
    display: -webkit-box; -webkit-line-clamp: 3; -webkit-box-orient: vertical; overflow: hidden;
    min-height: 60px;
  }
  .tags { display: flex; flex-wrap: wrap; gap: 4px; }
  .foot {
    display: flex; align-items: center;
    color: ${p => p.theme.colors.text3};
    font-size: 11px;
    font-family: ${p => p.theme.fontFamily.mono};
    .arrow { margin-left: auto; transition: transform 200ms; }
  }
  &:hover .arrow { transform: translateX(3px); color: ${p => p.theme.colors.accent}; }
`;

const Knowledge = () => {
  const { user, logout } = useAuth();
  const nav = useNavigate();
  const [files, setFiles] = useState([]);
  const [loading, setLoading] = useState(true);
  const [keyword, setKeyword] = useState('');
  const [rebuilding, setRebuilding] = useState(false);

  const load = async () => {
    setLoading(true);
    try {
      const data = await fetchUserImages(user, { count: 200 });
      setFiles(data || []);
    } catch (e) {
      if (e.tokenExpired) { message.error('登录已过期'); logout(); return; }
    } finally { setLoading(false); }
  };

  useEffect(() => {
    if (user?.token) load();
    // eslint-disable-next-line
  }, [user]);

  const wikiNodes = useMemo(() => {
    const arr = (files || []).filter(f => f.wiki_ready === 1);
    if (!keyword.trim()) return arr;
    const k = keyword.toLowerCase();
    return arr.filter(f => (f.file_name || f.name || '').toLowerCase().includes(k));
  }, [files, keyword]);

  const handleRebuild = async () => {
    setRebuilding(true);
    try {
      let success = 0;
      for (const f of files) {
        try { await describeFileByMd5(f.md5, f.file_name || f.name, f.type, user, null, true); success++; }
        catch {}
      }
      await rebuildIndex(user);
      message.success(`AI 描述重建完成：${success}/${files.length}`);
      load();
    } catch (e) {
      message.error('重建失败：' + (e.message || ''));
    } finally { setRebuilding(false); }
  };

  return (
    <div>
      <PageHead>
        <h1>Knowledge</h1>
        <p>每个上传的文件都会被自动分析、嵌入向量，并基于相似度与其他文件建立双向连接，形成知识网络。</p>
      </PageHead>

      <Panel>
        <PanelHeader>
          <h3>AI Pipeline</h3>
          <Pill>DASHSCOPE</Pill>
          <span className="right">
            <Button icon={<ReloadOutlined />} loading={rebuilding} onClick={handleRebuild}>
              重建 AI 描述
            </Button>
          </span>
        </PanelHeader>
        <PanelBody>
          <Hint>
            <ThunderboltOutlined className="ok" />
            <span className="ok">AI 服务已就绪</span>
            <span>·</span>
            <span>上传任意文件即自动触发：文本 → embedding，图片 → Qwen-VL 描述</span>
            <span>·</span>
            <span>向量相似度 ≥ 0.55 自动建立双向链接</span>
          </Hint>
        </PanelBody>
      </Panel>

      <SectionTitle>
        <h2>Knowledge Nodes</h2>
        <span>{wikiNodes.length} 个已生成的知识节点</span>
      </SectionTitle>

      <div style={{ marginBottom: 14 }}>
        <Input
          prefix={<SearchOutlined style={{ opacity: 0.5 }} />}
          placeholder="搜索节点名…"
          value={keyword}
          onChange={e => setKeyword(e.target.value)}
          allowClear
          style={{ maxWidth: 360 }}
        />
      </div>

      {loading ? (
        <Skeleton active paragraph={{ rows: 6 }} />
      ) : wikiNodes.length === 0 ? (
        <Panel>
          <PanelBody $pad="48px">
            <Empty description="还没有任何知识节点。上传文件后将自动生成 AI 摘要 + 双链">
              <Button type="primary" onClick={() => nav('/files')}>去上传文件</Button>
            </Empty>
          </PanelBody>
        </Panel>
      ) : (
        <NodeGrid>
          {wikiNodes.map(f => (
            <NodeCard key={f.md5} onClick={() => nav('/wiki/' + f.md5)}>
              <div className="head">
                <div className="icon"><BookOutlined /></div>
                <div className="name">{(f.file_name || f.name || '').replace(/\.[^.]+$/, '')}</div>
              </div>
              <div className="summary">该节点已被 AI 索引。点击查看完整摘要、关键概念以及与其他文件的反向链接。</div>
              <div className="tags">
                <Tag bordered={false} style={{ fontSize: 10 }}>{(f.type || '').toUpperCase()}</Tag>
                <Tag bordered={false} color="purple" style={{ fontSize: 10 }}>WIKI</Tag>
                {f.share_status === 1 && <Tag bordered={false} color="blue" style={{ fontSize: 10 }}>SHARED</Tag>}
              </div>
              <div className="foot">
                <span>{f.create_time}</span>
                <ArrowRightOutlined className="arrow" />
              </div>
            </NodeCard>
          ))}
        </NodeGrid>
      )}
    </div>
  );
};

export default Knowledge;
