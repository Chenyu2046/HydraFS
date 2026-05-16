import React, { useEffect, useMemo, useState } from 'react';
import styled from '@emotion/styled';
import { Segmented, Table, Button, Tooltip, message, Skeleton, Empty, Tag, Modal, Input } from 'antd';
import {
  DownloadOutlined, SaveOutlined, FileOutlined, FilePdfOutlined, FileWordOutlined,
  FileExcelOutlined, FileZipOutlined, FileTextOutlined, SearchOutlined,
  TrophyOutlined, TeamOutlined,
} from '@ant-design/icons';
import { useLocation, useNavigate } from 'react-router-dom';
import { useAuth } from '../contexts/AuthContext';
import { fetchSharedFiles, fetchSharedFilesRanking, saveSharedFile, pvSharedFile } from '../services/share';
import { Panel, PanelHeader, PanelBody, SectionTitle } from '../components/primitives';

const PageHead = styled.div`
  display: flex; align-items: flex-end; justify-content: space-between; gap: 16px;
  margin-bottom: 18px;
  .meta h1 {
    margin: 0 0 4px;
    font-size: 24px; font-weight: 700;
    letter-spacing: -0.4px;
    color: ${p => p.theme.colors.text};
  }
  .meta p { margin: 0; font-size: 13px; color: ${p => p.theme.colors.text2}; }
`;

const Toolbar = styled.div`
  display: flex; align-items: center; gap: 10px;
  margin-bottom: 12px;
  .search { max-width: 360px; }
  .ant-input-affix-wrapper {
    background: ${p => p.theme.colors.panel};
    border-color: ${p => p.theme.colors.border};
    border-radius: 8px;
  }
`;

const Rank = styled.span`
  display: inline-grid; place-items: center;
  width: 26px; height: 26px;
  border-radius: 7px;
  font-size: 12px; font-weight: 700;
  font-family: ${p => p.theme.fontFamily.mono};
  background: ${p => p.$rank === 0 ? p.theme.colors.accentSoft : p.theme.colors.panel2};
  color: ${p => p.$rank === 0 ? p.theme.colors.accent : p.theme.colors.text2};
  border: 1px solid ${p => p.$rank === 0 ? p.theme.colors.accentBorder : p.theme.colors.border};
`;

const ImgGrid = styled.div`
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(220px, 1fr));
  gap: 14px;
`;
const ImgCard = styled.div`
  background: ${p => p.theme.colors.panel};
  border: 1px solid ${p => p.theme.colors.border};
  border-radius: 12px;
  overflow: hidden;
  transition: all ${p => p.theme.duration.base} ${p => p.theme.ease.out};
  &:hover { border-color: ${p => p.theme.colors.borderStrong}; transform: translateY(-2px); }
  .cover {
    height: 150px;
    background: ${p => p.theme.colors.panel2};
    display: grid; place-items: center;
    cursor: pointer;
    overflow: hidden;
    img { width: 100%; height: 100%; object-fit: cover; transition: transform 220ms; }
    &:hover img { transform: scale(1.04); }
  }
  .body { padding: 10px 12px; display: flex; align-items: center; gap: 6px; }
  .name { flex: 1; min-width: 0; font-size: 12.5px; color: ${p => p.theme.colors.text}; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
`;

const TYPE_ICON = {
  pdf:  <FilePdfOutlined />, doc: <FileWordOutlined />, docx: <FileWordOutlined />,
  xls: <FileExcelOutlined />, xlsx: <FileExcelOutlined />,
  zip: <FileZipOutlined />, rar: <FileZipOutlined />, '7z': <FileZipOutlined />, tar: <FileZipOutlined />, gz: <FileZipOutlined />,
  txt: <FileTextOutlined />, md: <FileTextOutlined />, log: <FileTextOutlined />,
};
const isImg = (t) => t && ['png','jpg','jpeg','gif','bmp','webp','svg'].includes(String(t).toLowerCase());
const formatBytes = (b) => {
  if (!b) return '-';
  if (b < 1024) return b + ' B';
  if (b < 1024*1024) return (b/1024).toFixed(1) + ' KB';
  return (b/1024/1024).toFixed(2) + ' MB';
};

const SharedHub = () => {
  const { user } = useAuth();
  const loc = useLocation();
  const nav = useNavigate();
  const initial = useMemo(() => {
    const sp = new URLSearchParams(loc.search);
    // 兼容旧 ?tab=top 链接
    return sp.get('sort') || sp.get('tab') || 'browse';
  }, [loc.search]);
  const [sort, setSort] = useState(initial);
  const [allFiles, setAllFiles] = useState([]);
  const [topFiles, setTopFiles] = useState([]);
  const [keyword, setKeyword] = useState('');
  const [loading, setLoading] = useState(true);
  const [preview, setPreview] = useState(null);

  useEffect(() => {
    setLoading(true);
    Promise.all([fetchSharedFiles(0, 100), fetchSharedFilesRanking(0, 50)])
      .then(([a, b]) => { setAllFiles(a.files || []); setTopFiles(b.files || []); })
      .catch(() => message.error('加载共享数据失败'))
      .finally(() => setLoading(false));
  }, []);

  const handleDownload = async (f) => {
    try { await pvSharedFile(f); } catch {}
    const link = document.createElement('a');
    link.href = f.url; link.download = f.file_name || f.name;
    document.body.appendChild(link); link.click(); document.body.removeChild(link);
  };
  const handleSave = async (f) => {
    if (!user?.token) { message.warning('请先登录'); return; }
    try { await saveSharedFile(f, user); message.success('转存成功'); }
    catch (e) { message[e.message === '文件已存在' ? 'warning' : 'error'](e.message === '文件已存在' ? '文件已存在' : '转存失败'); }
  };

  const filteredAll = useMemo(() => {
    if (!keyword.trim()) return allFiles;
    const k = keyword.toLowerCase();
    return allFiles.filter(f => (f.file_name || f.name || '').toLowerCase().includes(k));
  }, [allFiles, keyword]);

  const imgs = filteredAll.filter(f => isImg(f.type));
  const docs = filteredAll.filter(f => !isImg(f.type));

  const browseColumns = [
    { title: '文件名', dataIndex: 'file_name', key: 'name', render: (t, r) => (
      <span style={{ display: 'inline-flex', alignItems: 'center', gap: 8 }}>
        <span style={{ color: 'var(--accent)' }}>{TYPE_ICON[(r.type || '').toLowerCase()] || <FileOutlined />}</span>
        <span style={{ fontWeight: 500 }}>{t}</span>
      </span>
    ) },
    { title: '类型', dataIndex: 'type', width: 90, render: (t) => <span className="text-mono" style={{ opacity: 0.7 }}>{(t || '').toUpperCase()}</span> },
    { title: '大小', dataIndex: 'size', width: 110, render: (s) => <span className="text-mono">{formatBytes(s)}</span> },
    { title: '分享者', dataIndex: 'user', width: 130 },
    { title: '下载', dataIndex: 'pv', width: 80, render: (v) => <span className="text-mono">{v || 0}</span> },
    { title: '时间', dataIndex: 'create_time', width: 170 },
    { title: '操作', key: 'op', width: 110, align: 'right', render: (_, r) => (
      <span>
        <Tooltip title="转存"><Button type="text" size="small" icon={<SaveOutlined />} onClick={() => handleSave(r)} /></Tooltip>
        <Tooltip title="下载"><Button type="text" size="small" icon={<DownloadOutlined />} onClick={() => handleDownload(r)} /></Tooltip>
      </span>
    ) },
  ];

  const topColumns = [
    { title: '#', key: 'rank', width: 60, render: (_, __, i) => <Rank $rank={i}>{i + 1}</Rank> },
    { title: '文件名', dataIndex: 'file_name', render: (t, r) => (
      <span style={{ display: 'inline-flex', alignItems: 'center', gap: 8 }}>
        <span style={{ color: 'var(--accent)' }}>{TYPE_ICON[(r.type || '').toLowerCase()] || <FileOutlined />}</span>
        <span style={{ fontWeight: 500 }}>{t}</span>
      </span>
    ) },
    { title: '类型', dataIndex: 'type', width: 90, render: (t) => <Tag bordered={false} style={{ fontSize: 10 }}>{(t || '').toUpperCase()}</Tag> },
    { title: '大小', dataIndex: 'size', width: 110, render: (s) => <span className="text-mono">{formatBytes(s)}</span> },
    { title: '分享者', dataIndex: 'user', width: 130 },
    { title: '下载次数', dataIndex: 'pv', width: 110, render: (v) => <span style={{ fontWeight: 700, color: 'var(--accent)' }} className="text-mono">{v || 0}</span> },
    { title: '操作', key: 'op', width: 110, align: 'right', render: (_, r) => (
      <span>
        <Tooltip title="转存"><Button type="text" size="small" icon={<SaveOutlined />} onClick={() => handleSave(r)} /></Tooltip>
        <Tooltip title="下载"><Button type="text" size="small" icon={<DownloadOutlined />} onClick={() => handleDownload(r)} /></Tooltip>
      </span>
    ) },
  ];

  return (
    <div>
      <PageHead>
        <div className="meta">
          <h1>Shared</h1>
          <p>来自其他用户的公开文件 · 共 {allFiles.length} 个</p>
        </div>
        {/* 浏览 / 下载榜 用 Segmented 作为"排序/视角"开关，避免在内容页再造一层 Tab 导航 */}
        <Segmented
          value={sort}
          onChange={(v) => {
            setSort(v);
            nav('/shared' + (v === 'browse' ? '' : '?sort=' + v), { replace: true });
          }}
          options={[
            { value: 'browse', label: <span><TeamOutlined /> 浏览</span> },
            { value: 'top',    label: <span><TrophyOutlined /> 下载榜</span> },
          ]}
        />
      </PageHead>

      {sort === 'browse' && (
        <>
          <Toolbar>
            <Input
              className="search"
              prefix={<SearchOutlined style={{ opacity: 0.5 }} />}
              placeholder="按文件名搜索…"
              value={keyword}
              onChange={e => setKeyword(e.target.value)}
              allowClear
            />
          </Toolbar>

          {loading ? <Skeleton active paragraph={{ rows: 6 }} /> : (
            <>
              {imgs.length > 0 && (
                <>
                  <SectionTitle><h2>共享图片</h2><span>{imgs.length}</span></SectionTitle>
                  <ImgGrid>
                    {imgs.map(img => (
                      <ImgCard key={img.md5 + img.file_name}>
                        <div className="cover" onClick={() => setPreview(img)}>
                          <img src={img.url} alt={img.file_name} />
                        </div>
                        <div className="body">
                          <span className="name">{img.file_name}</span>
                          <Tooltip title="转存"><Button type="text" size="small" icon={<SaveOutlined />} onClick={() => handleSave(img)} /></Tooltip>
                          <Tooltip title="下载"><Button type="text" size="small" icon={<DownloadOutlined />} onClick={() => handleDownload(img)} /></Tooltip>
                        </div>
                      </ImgCard>
                    ))}
                  </ImgGrid>
                </>
              )}

              {docs.length > 0 && (
                <>
                  <SectionTitle><h2>共享文件</h2><span>{docs.length}</span></SectionTitle>
                  <Panel style={{ overflow: 'hidden' }}>
                    <Table
                      columns={browseColumns}
                      dataSource={docs}
                      rowKey={(r) => r.md5 + r.file_name}
                      pagination={{ pageSize: 10, size: 'small', showSizeChanger: false }}
                      size="middle"
                    />
                  </Panel>
                </>
              )}

              {imgs.length === 0 && docs.length === 0 && (
                <Panel><PanelBody $pad="48px"><Empty description={keyword ? '没有匹配的文件' : '暂无共享文件'} /></PanelBody></Panel>
              )}
            </>
          )}
        </>
      )}

      {sort === 'top' && (
        <Panel style={{ overflow: 'hidden', marginTop: 4 }}>
          <PanelHeader>
            <h3>Top Downloads</h3>
            <span className="subtitle">按累计下载量排序</span>
          </PanelHeader>
          {loading
            ? <PanelBody><Skeleton active paragraph={{ rows: 6 }} /></PanelBody>
            : <Table
                columns={topColumns}
                dataSource={topFiles}
                rowKey={(r) => r.md5 + r.file_name}
                pagination={{ pageSize: 10, size: 'small', showSizeChanger: false }}
                size="middle"
              />}
        </Panel>
      )}

      <Modal
        open={!!preview}
        title={preview?.file_name}
        footer={null}
        onCancel={() => setPreview(null)}
        width="auto"
        style={{ maxWidth: '90vw' }}
        centered
      >
        {preview && <img alt={preview.file_name} src={preview.url} style={{ maxWidth: '100%', maxHeight: '80vh', display: 'block', borderRadius: 8 }} />}
      </Modal>
    </div>
  );
};

export default SharedHub;
