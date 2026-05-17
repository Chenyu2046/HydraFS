import React, { useEffect, useMemo, useRef, useState } from 'react';
import { Drawer, Tag, Button, message, Spin, Empty, Alert } from 'antd';
import {
  CloseOutlined, ShareAltOutlined, DeleteOutlined, DownloadOutlined,
  CheckCircleOutlined, NodeIndexOutlined, BookOutlined, FileOutlined,
} from '@ant-design/icons';
import styled from '@emotion/styled';
import { useNavigate } from 'react-router-dom';
import { useAuth } from '../contexts/AuthContext';
import { describeFileByMd5, fetchFileCard, fetchWiki } from '../services/ai';
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

const normalizeReady = (value) => value === 1 || value === true || value === '1' || value === 'true';
const isTaskSuccess = (status) => ['success', 'done', 'completed'].includes(String(status || '').toLowerCase());
const isTaskFailed = (status) => ['failed', 'error'].includes(String(status || '').toLowerCase());

const FileDrawer = ({ open, file, onClose, onShare, onCancelShare, onDelete, onDownload }) => {
  const { user } = useAuth();
  const nav = useNavigate();
  const [wiki, setWiki] = useState(null);
  const [fileCard, setFileCard] = useState(null);
  const [loadingWiki, setLoadingWiki] = useState(false);
  const [aiTask, setAiTask] = useState({ status: 'idle', text: '' });
  const pollTimerRef = useRef(null);

  const tags = useMemo(() => {
    if (!wiki?.tags) return [];
    try { return JSON.parse(wiki.tags); } catch { return []; }
  }, [wiki]);

  useEffect(() => {
    if (pollTimerRef.current) {
      clearTimeout(pollTimerRef.current);
      pollTimerRef.current = null;
    }
    setAiTask({ status: 'idle', text: '' });
    setFileCard(null);
    if (!open || !file) { setWiki(null); return; }
    if (normalizeReady(file.wiki_ready)) {
      setLoadingWiki(true);
      fetchWiki(file.md5, user)
        .then(setWiki)
        .catch(() => {})
        .finally(() => setLoadingWiki(false));
    } else {
      setWiki(null);
    }

    return () => {
      if (pollTimerRef.current) {
        clearTimeout(pollTimerRef.current);
        pollTimerRef.current = null;
      }
    };
  }, [open, file, user]);

  if (!file) return null;

  const handleGenerate = async () => {
    setAiTask({ status: 'submitting', text: '正在提交 AI 摘要任务…' });
    try {
      const submitted = await describeFileByMd5(file.md5, file.file_name || file.name, file.type, user, null);
      message.success('AI 分析任务已提交');
      const taskId = submitted?.data?.task_id || submitted?.task_id;
      setAiTask({ status: 'running', text: `任务已提交${taskId ? ` #${taskId}` : ''}，正在等待摘要结果…` });
      pollWikiResult(0);
    } catch (e) {
      setAiTask({ status: 'failed', text: '提交失败：' + (e.message || '未知错误') });
      message.error('提交失败：' + (e.message || ''));
    }
  };

  const pollWikiResult = (attempt) => {
    const maxAttempts = 10;
    pollTimerRef.current = setTimeout(async () => {
      try {
        const nextCard = await fetchFileCard(file.md5, user);
        setFileCard(nextCard || null);
        const taskStatus = nextCard?.task_status || nextCard?.parse_status;
        const errorMsg = nextCard?.error_msg || '';

        if (isTaskFailed(taskStatus)) {
          setAiTask({
            status: 'failed',
            text: 'AI 摘要生成失败：' + (errorMsg || '后端任务失败'),
          });
          return;
        }

        if (normalizeReady(nextCard?.wiki_ready) || isTaskSuccess(taskStatus)) {
          fetchWiki(file.md5, user)
            .then(nextWiki => { if (nextWiki) setWiki(nextWiki); })
            .catch(() => {});
          setAiTask({ status: 'success', text: 'AI 摘要已生成' });
          return;
        }
      } catch (e) {
        if (attempt >= maxAttempts - 1) {
          setAiTask({
            status: 'failed',
            text: '暂未查询到摘要结果，任务可能仍在后台处理中。请稍后刷新文件详情。',
          });
          return;
        }
      }

      setAiTask({
        status: 'running',
        text: `任务处理中，正在第 ${attempt + 2}/${maxAttempts} 次查询结果…`,
      });
      pollWikiResult(attempt + 1);
    }, attempt === 0 ? 1200 : 3000);
  };

  const aiAlertType = aiTask.status === 'failed' ? 'error' : aiTask.status === 'success' ? 'success' : 'info';
  const aiBusy = aiTask.status === 'submitting' || aiTask.status === 'running';
  const wikiReady = normalizeReady(file.wiki_ready) || normalizeReady(fileCard?.wiki_ready) || !!wiki || aiTask.status === 'success';
  const summary = wiki?.summary || fileCard?.summary;
  const displayTags = tags.length > 0 ? tags : (() => {
    const rawTags = fileCard?.tags || fileCard?.tags_json;
    if (Array.isArray(rawTags)) return rawTags;
    try { return rawTags ? JSON.parse(rawTags) : []; } catch { return []; }
  })();

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
          {wikiReady && <Tag color="purple" bordered={false}>Wiki</Tag>}
        </span>
      </Kv>

      <Section>
        <h4>AI Summary</h4>
        {aiTask.status !== 'idle' && (
          <Alert
            type={aiAlertType}
            showIcon
            message={aiTask.text}
            style={{ marginBottom: 10 }}
          />
        )}
        {loadingWiki ? (
          <Spin size="small" />
        ) : summary ? (
          <>
            <Summary>{summary || '（暂无摘要）'}</Summary>
            {displayTags.length > 0 && (
              <div style={{ marginTop: 10, display: 'flex', flexWrap: 'wrap', gap: 6 }}>
                {displayTags.map((t, i) => <Tag key={i} bordered={false}>#{t}</Tag>)}
              </div>
            )}
          </>
        ) : (
          <Empty
            image={Empty.PRESENTED_IMAGE_SIMPLE}
            description={<span style={{ fontSize: 12 }}>该文件尚未生成知识节点</span>}
          >
            <Button size="small" loading={aiBusy} onClick={handleGenerate}>生成 AI 摘要</Button>
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
            disabled={!wikiReady}
          >图谱中查看</Button>
          {wikiReady && (
            <Button icon={<BookOutlined />} onClick={() => nav('/wiki/' + file.md5)}>打开 Wiki</Button>
          )}
          <Button danger icon={<DeleteOutlined />} onClick={() => onDelete(file)}>删除</Button>
        </Actions>
      </Section>
    </Drawer>
  );
};

export default FileDrawer;
