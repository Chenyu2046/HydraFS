import React, { useEffect, useMemo, useState } from 'react';
import styled from '@emotion/styled';
import { Input, Button, Tag, message, Skeleton, Empty, Tooltip } from 'antd';
import {
  SearchOutlined, KeyOutlined, ReloadOutlined, BookOutlined, ArrowRightOutlined,
} from '@ant-design/icons';
import { useNavigate } from 'react-router-dom';
import { useAuth } from '../contexts/AuthContext';
import { fetchUserImages } from '../services/images';
import { fetchApiKey, saveApiKey, describeFileByMd5, rebuildIndex } from '../services/ai';
import { Panel, PanelHeader, PanelBody, SectionTitle, Pill } from '../components/primitives';
import { copy } from '../lib/copy';

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

const KeyRow = styled.div`
  display: flex; gap: 8px; align-items: center; flex-wrap: wrap;
  .ant-input-affix-wrapper {
    background: ${p => p.theme.colors.panel2};
    border-color: ${p => p.theme.colors.border};
    border-radius: 8px;
    flex: 1; min-width: 280px;
  }
`;

const Hint = styled.div`
  font-size: 12px;
  color: ${p => p.theme.colors.text3};
  margin-top: 8px;
  font-family: ${p => p.theme.fontFamily.mono};
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
  const [apiKey, setApiKeyVal] = useState('');
  const [savedKey, setSavedKey] = useState('');
  const [savingKey, setSavingKey] = useState(false);
  const [rebuilding, setRebuilding] = useState(false);

  const load = async () => {
    setLoading(true);
    try {
      const data = await fetchUserImages(user, { count: 200 });
      setFiles(data || []);
    } catch (e) {
      if (e.tokenExpired) { message.error(copy.auth.expired); logout(); return; }
    } finally { setLoading(false); }
  };

  useEffect(() => {
    if (user?.token) {
      load();
      fetchApiKey(user).then(k => { setApiKeyVal(k || ''); setSavedKey(k || ''); }).catch(() => {});
    }
    // eslint-disable-next-line
  }, [user]);

  const wikiNodes = useMemo(() => {
    const arr = (files || []).filter(f => f.wiki_ready === 1);
    if (!keyword.trim()) return arr;
    const k = keyword.toLowerCase();
    return arr.filter(f => (f.file_name || f.name || '').toLowerCase().includes(k));
  }, [files, keyword]);

  const handleSaveKey = async () => {
    if (!apiKey.trim()) { message.warning(copy.provider.emptyKey); return; }
    setSavingKey(true);
    try {
      await saveApiKey(apiKey.trim(), user);
      setSavedKey(apiKey.trim());
      message.success(copy.provider.keySaved);
    } catch (e) {
      if (e.tokenExpired) { message.error(copy.auth.expired); logout(); return; }
      message.error(copy.provider.keyFail);
    } finally { setSavingKey(false); }
  };

  const handleClearKey = async () => {
    try {
      await saveApiKey('', user);
      setApiKeyVal(''); setSavedKey('');
      message.info(copy.provider.keyCleared);
    } catch (e) {
      message.error(copy.provider.keyFail);
    }
  };

  const handleRebuild = async () => {
    if (!savedKey) { message.info(copy.provider.emptyKey); return; }
    setRebuilding(true);
    try {
      let success = 0;
      for (const f of files) {
        try { await describeFileByMd5(f.md5, f.file_name || f.name, f.type, user, savedKey, true); success++; }
        catch {}
      }
      await rebuildIndex(user);
      message.success(copy.provider.rebuildDone(success, files.length));
      load();
    } catch (e) {
      message.error(copy.provider.rebuildFail);
    } finally { setRebuilding(false); }
  };

  return (
    <div>
      <PageHead>
        <h1>Knowledge</h1>
        <p>AI 已经为这些文件生成了摘要、标签与向量，它们构成你的知识节点。</p>
      </PageHead>

      <Panel>
        <PanelHeader>
          <h3>AI Provider</h3>
          <Pill>DASHSCOPE</Pill>
          <span className="right">
            <Button icon={<ReloadOutlined />} loading={rebuilding} onClick={handleRebuild} disabled={!savedKey}>
              重建 AI 描述
            </Button>
          </span>
        </PanelHeader>
        <PanelBody>
          <KeyRow>
            <Input.Password
              prefix={<KeyOutlined style={{ opacity: 0.5 }} />}
              placeholder="阿里百炼 API Key（sk-...）"
              value={apiKey}
              onChange={e => setApiKeyVal(e.target.value)}
            />
            <Button type="primary" loading={savingKey} onClick={handleSaveKey} disabled={apiKey === savedKey && !!savedKey}>
              {savedKey && apiKey === savedKey ? '已保存' : '保存'}
            </Button>
            {savedKey && <Button onClick={handleClearKey}>清除</Button>}
          </KeyRow>
          <Hint>API Key 仅保存到当前账号，用于生成文件摘要 / 标签 / 向量，并支撑语义搜索与图谱构建。</Hint>
        </PanelBody>
      </Panel>

      <SectionTitle>
        <h2>Knowledge Nodes</h2>
        <span>{wikiNodes.length} 个已生成的知识节点</span>
      </SectionTitle>

      <div style={{ marginBottom: 14 }}>
        <Input
          prefix={<SearchOutlined style={{ opacity: 0.5 }} />}
          placeholder={copy.search.placeholderNodes}
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
            <Empty description={copy.empty.knowledge}>
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
