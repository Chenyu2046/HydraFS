import React, { useEffect, useMemo, useState } from 'react';
import { Input, Segmented, Button, Table, Tag, message, Space, Tooltip, Empty, Skeleton } from 'antd';
import {
  SearchOutlined, AppstoreOutlined, UnorderedListOutlined, ReloadOutlined,
  ShareAltOutlined, DeleteOutlined, DownloadOutlined, CheckCircleOutlined,
  FileOutlined, FilePdfOutlined, FileWordOutlined, FileExcelOutlined,
  FileZipOutlined, FileTextOutlined, FileImageOutlined, CodeOutlined,
} from '@ant-design/icons';
import styled from '@emotion/styled';
import { useLocation } from 'react-router-dom';
import { useAuth } from '../contexts/AuthContext';
import { fetchUserImages, uploadImage, deleteImage, shareFile, cancelShareFile, pvFile } from '../services/images';
import { describeFile } from '../services/ai';
import QuickUpload from '../components/QuickUpload';
import FileGrid from '../components/FileGrid';
import FileDrawer from '../components/FileDrawer';
import { Panel, PanelHeader, PanelBody, SectionTitle } from '../components/primitives';
import { classifyFileType } from '../mock/graph';

// ====== Styled ======
const PageHead = styled.div`
  display: flex; align-items: flex-end; justify-content: space-between;
  margin-bottom: 18px;
  h1 {
    margin: 0 0 4px;
    font-size: 24px; font-weight: 700;
    letter-spacing: -0.4px;
    color: ${p => p.theme.colors.text};
  }
  p {
    margin: 0; font-size: 13px;
    color: ${p => p.theme.colors.text2};
  }
`;

const Toolbar = styled.div`
  display: flex; align-items: center; gap: 10px;
  margin-bottom: 14px;
  flex-wrap: wrap;

  .search { flex: 1; min-width: 240px; max-width: 420px; }
  .ant-input-affix-wrapper {
    background: ${p => p.theme.colors.panel};
    border-color: ${p => p.theme.colors.border};
    border-radius: 8px;
  }
`;

const TYPE_ICON = {
  pdf:  <FilePdfOutlined />,   doc:  <FileWordOutlined />,  docx: <FileWordOutlined />,
  xls:  <FileExcelOutlined />, xlsx: <FileExcelOutlined />,
  zip:  <FileZipOutlined />,   rar:  <FileZipOutlined />,   '7z': <FileZipOutlined />,
  tar:  <FileZipOutlined />,   gz:   <FileZipOutlined />,
  txt:  <FileTextOutlined />,  md:   <FileTextOutlined />,  log: <FileTextOutlined />,
  png:  <FileImageOutlined />, jpg:  <FileImageOutlined />, jpeg: <FileImageOutlined />,
  gif:  <FileImageOutlined />, webp: <FileImageOutlined />, svg:  <FileImageOutlined />,
  c:    <CodeOutlined />,      cpp:  <CodeOutlined />,      h:    <CodeOutlined />,
  js:   <CodeOutlined />,      ts:   <CodeOutlined />,      py:   <CodeOutlined />,
};

const formatBytes = (b) => {
  if (!b) return '-';
  if (b < 1024) return b + ' B';
  if (b < 1024*1024) return (b/1024).toFixed(1) + ' KB';
  if (b < 1024**3) return (b/1024/1024).toFixed(2) + ' MB';
  return (b/1024**3).toFixed(2) + ' GB';
};

const FILTERS = [
  { value: 'all',     label: '全部' },
  { value: 'image',   label: '图片' },
  { value: 'doc',     label: '文档' },
  { value: 'code',    label: '代码' },
  { value: 'archive', label: '压缩' },
  { value: 'other',   label: '其他' },
];

const FileList = () => {
  const { user, logout } = useAuth();
  const loc = useLocation();
  const initialFilter = useMemo(() => {
    const sp = new URLSearchParams(loc.search);
    return sp.get('type') || 'all';
  }, [loc.search]);

  const [files, setFiles] = useState([]);
  const [loading, setLoading] = useState(true);
  const [view, setView] = useState('grid');
  const [filter, setFilter] = useState(initialFilter);
  const [keyword, setKeyword] = useState('');
  const [drawerFile, setDrawerFile] = useState(null);

  const load = async () => {
    setLoading(true);
    try {
      // 文件主页：先拉 100 兜底，后续可接前端分页/虚拟滚动
      const data = await fetchUserImages(user, { count: 100 });
      setFiles(data || []);
    } catch (e) {
      if (e.tokenExpired) { message.error('登录已过期'); logout(); return; }
      message.error('获取文件列表失败');
    } finally { setLoading(false); }
  };

  useEffect(() => { if (user?.token) load(); /* eslint-disable-next-line */ }, [user]);

  const filtered = useMemo(() => {
    let arr = files;
    if (filter !== 'all') arr = arr.filter(f => classifyFileType(f.type) === filter);
    if (keyword.trim()) {
      const k = keyword.toLowerCase();
      arr = arr.filter(f => (f.file_name || f.name || '').toLowerCase().includes(k));
    }
    return arr;
  }, [files, filter, keyword]);

  const handleShare       = async (r) => { try { await shareFile(r, user); message.success('分享成功'); load(); if (drawerFile?.md5 === r.md5) setDrawerFile({ ...r, share_status: 1 }); } catch { message.error('分享失败'); } };
  const handleCancelShare = async (r) => { try { await cancelShareFile(r, user); message.success('取消分享成功'); load(); if (drawerFile?.md5 === r.md5) setDrawerFile({ ...r, share_status: 0 }); } catch { message.error('取消失败'); } };
  const handleDelete      = async (r) => { try { await deleteImage(r, user); message.success('删除成功'); setDrawerFile(null); load(); } catch { message.error('删除失败'); } };
  const handleDownload    = async (r) => {
    try { await pvFile(r, user); } catch {}
    const link = document.createElement('a'); link.href = r.url; link.download = r.file_name || r.name;
    document.body.appendChild(link); link.click(); document.body.removeChild(link);
  };

  const columns = [
    {
      title: '文件名', dataIndex: 'file_name', key: 'name',
      render: (t, r) => (
        <Space size={10}>
          <span style={{ fontSize: 16, color: 'var(--accent)' }}>{TYPE_ICON[(r.type||'').toLowerCase()] || <FileOutlined />}</span>
          <span style={{ fontWeight: 500 }}>{t || r.name}</span>
          {r.share_status === 1 && <Tag color="blue" bordered={false}>已分享</Tag>}
          {r.wiki_ready === 1 && <Tag color="purple" bordered={false}>Wiki</Tag>}
        </Space>
      ),
    },
    { title: '类型', dataIndex: 'type', key: 'type', width: 90, render: (t) => <span className="text-mono" style={{ opacity: 0.75 }}>{(t || '').toUpperCase()}</span> },
    { title: '大小', dataIndex: 'size', key: 'size', width: 110, render: (s) => <span className="text-mono">{formatBytes(s)}</span> },
    { title: '上传时间', dataIndex: 'create_time', key: 'time', width: 180 },
    {
      title: '操作', key: 'action', width: 160, align: 'right',
      render: (_, r) => (
        <Space size={2}>
          <Tooltip title="下载"><Button type="text" size="small" icon={<DownloadOutlined />} onClick={(e) => { e.stopPropagation(); handleDownload(r); }} /></Tooltip>
          {r.share_status === 1
            ? <Tooltip title="取消分享"><Button type="text" size="small" icon={<CheckCircleOutlined />} onClick={(e) => { e.stopPropagation(); handleCancelShare(r); }} /></Tooltip>
            : <Tooltip title="分享"><Button type="text" size="small" icon={<ShareAltOutlined />} onClick={(e) => { e.stopPropagation(); handleShare(r); }} /></Tooltip>}
          <Tooltip title="删除"><Button type="text" size="small" danger icon={<DeleteOutlined />} onClick={(e) => { e.stopPropagation(); handleDelete(r); }} /></Tooltip>
        </Space>
      ),
    },
  ];

  return (
    <div>
      <PageHead>
        <div>
          <h1>Files</h1>
          <p>统一管理你云端的所有文件 · {files.length} 个 · {formatBytes(files.reduce((s, f) => s + (f.size || 0), 0))}</p>
        </div>
      </PageHead>

      <QuickUpload onDone={load} />

      <SectionTitle>
        <h2>All Files</h2>
        <span>支持搜索 / 类型筛选 / 网格与表格双视图</span>
        <span className="right">
          <Segmented
            options={[
              { value: 'grid', icon: <AppstoreOutlined />, label: 'Grid' },
              { value: 'list', icon: <UnorderedListOutlined />, label: 'List' },
            ]}
            value={view}
            onChange={setView}
            size="small"
          />
          <Tooltip title="刷新"><Button size="small" icon={<ReloadOutlined />} onClick={load} /></Tooltip>
        </span>
      </SectionTitle>

      <Toolbar>
        <Input
          className="search"
          prefix={<SearchOutlined style={{ opacity: 0.5 }} />}
          placeholder="按文件名搜索…"
          value={keyword}
          onChange={e => setKeyword(e.target.value)}
          allowClear
        />
        <Segmented
          options={FILTERS}
          value={filter}
          onChange={setFilter}
        />
      </Toolbar>

      {loading && files.length === 0 ? (
        <Skeleton active paragraph={{ rows: 6 }} />
      ) : filtered.length === 0 ? (
        <Panel>
          <PanelBody $pad="48px">
            <Empty description={keyword ? `没有匹配 "${keyword}" 的文件` : '暂无文件，先上传一个试试'} />
          </PanelBody>
        </Panel>
      ) : view === 'grid' ? (
        <FileGrid files={filtered} onPick={setDrawerFile} />
      ) : (
        <Panel style={{ overflow: 'hidden' }}>
          <Table
            columns={columns}
            dataSource={filtered}
            rowKey="md5"
            pagination={{ pageSize: 12, showSizeChanger: false, size: 'small' }}
            onRow={(r) => ({ onClick: () => setDrawerFile(r), style: { cursor: 'pointer' } })}
            size="middle"
          />
        </Panel>
      )}

      <FileDrawer
        open={!!drawerFile}
        file={drawerFile}
        onClose={() => setDrawerFile(null)}
        onShare={handleShare}
        onCancelShare={handleCancelShare}
        onDelete={handleDelete}
        onDownload={handleDownload}
      />
    </div>
  );
};

export default FileList;
