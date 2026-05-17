import React, { useEffect, useMemo, useState } from 'react';
import { Drawer, Tag, Button, Tooltip, message, Spin, Empty } from 'antd';
import {
  CloseOutlined, ShareAltOutlined, DeleteOutlined, DownloadOutlined,
  CheckCircleOutlined, NodeIndexOutlined, BookOutlined, FileOutlined,
} from '@ant-design/icons';
import styled from '@emotion/styled';
import { useNavigate } from 'react-router-dom';
import { useAuth } from '../contexts/AuthContext';
import { describeFileByMd5, fetchWiki, fetchApiKey } from '../services/ai';
import { Pill } from './primitives';
import { classifyFileType } from '../mock/graph';

const Section = styled.div`
  border-top: 1px solid ${p => p.theme.colors.border};
  padding: 16px 0;
  &:first-of-type { border-top: none; padding-top: 0; }
  h4 {
    font-size: 11.5px;
    text-transform: uppercase;
    letter-spacing: 0.6px;
    color: ${p => p.theme.colors.text3};
    margin: 0 0 10px;
    font-weight: 600;
  }
`;

const Preview = styled.div`
  height: 180px;
  border-radius: 10px;
  background: ${p => p.theme.colors.panel2};
  border: 1px solid ${p => p.theme.colors.border};
  display: grid; place-items: center;
  overflow: hidden;
  margin-bottom: 14px;
  img { width: 100%; height: 100%; object-fit: cover; }
  .placeholder {
    color: ${p => p.theme.colors.text3};
    font-size: 32px;
  }
`;

const Title = styled.div`
  font-size: 16px;
  font-weight: 600;
  color: ${p => p.theme.colors.text};
  word-break: break-all;
  letter-spacing: -0.2px;
  margin-bottom: 6px;
`;

const Kv = styled.div`
  display: grid;
  grid-template-columns: 80px 1fr;
  gap: 6px 12px;
  font-size: 12.5px;
  .k { color: ${p => p.theme.colors.text3}; }
  .v { color: ${p => p.theme.colors.text}; font-family: ${p => p.theme.fontFamily.mono}; word-break: break-all; }
`;

const Summary = styled.div`
  background: ${p => p.theme.colors.panel2};
  border: 1px solid ${p => p.theme.colors.border};
  border-radius: 10px;
  padding: 12px 14px;
  font-size: 13px;
  line-height: 1.65;
  color: ${p => p.theme.colors.text};
  white-space: pre-wrap;
`;

const Actions = styled.div`
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 8px;
  margin-top: 14px;
`;

const isImg = (t) => t && ['png','jpg','jpeg','gif','bmp','webp','svg'].includes(String(t).toLowerCase());

const FileDrawer = ({ open, file, onClose, onShare, onCancelShare, onDelete, onDownload }) => {
  const { user } = useAuth();
  const nav = useNavigate();
  const [wiki, setWiki] = useState(null);
  const [loadingWiki, setLoadingWiki] = useState(false);
  const [generating, setGenerating] = useState(false);

  const tags = useMemo(() => {
    if (!wiki?.tags) return [];
    try { return JSON.parse(wiki.tags); } catch { return []; }
  }, [wiki]);

  useEffect(() => {
    if (!open || !file) { setWiki(null); return; }
    if (file.wiki_ready === 1) {
      setLoadingWiki(true);
      fetchWiki(file.md5, user)
        .then(setWiki)
        .catch(() => {})
        .finally(() => setLoadingWiki(false));
    } else {
      setWiki(null);
    }
  }, [open, file, user]);

  if (!file) return null;

  const handleGenerate = async () => {
    setGenerating(true);
    try {
      const apiKey = await fetchApiKey(user);
      if (!apiKey) { message.info('请先在 Knowledge 页设置 API Key'); nav('/knowledge'); return; }
      await describeFileByMd5(file.md5, file.file_name || file.name, file.type, user, apiKey);
      message.success('AI 分析任务已提交');
    } catch (e) {
      message.error('提交失败：' + (e.message || ''));
    } finally { setGenerating(false); }
  };

  return (
    <Drawer
      open={open}
      onClose={onClose}
      width={420}
      closable={false}
      title={null}
      headerStyle={{ display: 'none' }}
      bodyStyle={{ padding: '20px 22px 28px' }}
      maskClosable
    >
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 14 }}>
        <Pill>{(file.type || 'FILE').toUpperCase()} · {classifyFileType(file.type).toUpperCase()}</Pill>
        <Button type="text" size="small" icon={<CloseOutlined />} onClick={onClose} />
      </div>

      <Preview>
        {isImg(file.type) && file.url
          ? <img src={file.url} alt={file.file_name || file.name} />
          : <FileOutlined className="placeholder" />}
      </Preview>

      <Title>{file.file_name || file.name}</Title>

      <Kv>
        <span className="k">Size</span>      <span className="v">{file.size || 0} B</span>
        <span className="k">MD5</span>       <span className="v">{(file.md5 || '').slice(0, 16)}…</span>
        <span className="k">Uploaded</span>  <span className="v">{file.create_time}</span>
        <span className="k">Status</span>    <span className="v">
          {file.share_status === 1 ? <Tag color="blue" bordered={false}>已分享</Tag> : <Tag bordered={false}>私有</Tag>}
          {file.wiki_ready === 1 && <Tag color="purple" bordered={false}>Wiki</Tag>}
        </span>
      </Kv>

      <Section>
        <h4>AI Summary</h4>
        {loadingWiki ? (
          <Spin size="small" />
        ) : wiki ? (
          <>
            <Summary>{wiki.summary || '（暂无摘要）'}</Summary>
            {tags.length > 0 && (
              <div style={{ marginTop: 10, display: 'flex', flexWrap: 'wrap', gap: 6 }}>
                {tags.map((t, i) => <Tag key={i} bordered={false}>#{t}</Tag>)}
              </div>
            )}
          </>
        ) : (
          <Empty
            image={Empty.PRESENTED_IMAGE_SIMPLE}
            description={<span style={{ fontSize: 12 }}>该文件尚未生成知识节点</span>}
          >
            <Button size="small" loading={generating} onClick={handleGenerate}>生成 AI 摘要</Button>
          </Empty>
        )}
      </Section>

      <Section>
        <h4>Actions</h4>
        <Actions>
          <Button icon={<DownloadOutlined />} onClick={() => onDownload(file)}>下载</Button>
          {file.share_status === 1
            ? <Button icon={<CheckCircleOutlined />} onClick={() => onCancelShare(file)}>取消分享</Button>
            : <Button icon={<ShareAltOutlined />} onClick={() => onShare(file)}>分享</Button>}
          <Button
            icon={<NodeIndexOutlined />}
            onClick={() => nav('/graph?focus=' + encodeURIComponent(file.md5))}
            disabled={file.wiki_ready !== 1}
          >图谱中查看</Button>
          {file.wiki_ready === 1 && (
            <Button icon={<BookOutlined />} onClick={() => nav('/wiki/' + file.md5)}>打开 Wiki</Button>
          )}
          <Button danger icon={<DeleteOutlined />} onClick={() => onDelete(file)}>删除</Button>
        </Actions>
      </Section>
    </Drawer>
  );
};

export default FileDrawer;
