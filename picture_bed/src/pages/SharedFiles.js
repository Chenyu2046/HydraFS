import React, { useState, useEffect } from 'react';
import { Row, Col, Card, Table, Button, message, Tooltip, Space, Modal } from 'antd';
import {
  SaveOutlined, DownloadOutlined, FileOutlined,
  FilePdfOutlined, FileWordOutlined, FileExcelOutlined,
  FileZipOutlined, FileTextOutlined, EyeOutlined
} from '@ant-design/icons';
import styled from '@emotion/styled';
import { useAuth } from '../contexts/AuthContext';
import { fetchSharedFiles, saveSharedFile, pvSharedFile } from '../services/share';

const PageHeader = styled.div`
  margin-bottom: 24px;
  h1 { font-size: 22px; font-weight: 700; color: #0F172A; margin: 0 0 4px; letter-spacing: -0.3px; }
  p { font-size: 14px; color: #64748B; margin: 0; }
`;

const SectionCard = styled(Card)`
  border-radius: 14px;
  border: 1px solid #E2E8F0;
  box-shadow: 0 1px 2px rgba(0,0,0,0.03);
  margin-bottom: 24px;

  .ant-card-head {
    border-bottom: 1px solid #F1F5F9;
    padding: 16px 24px;
    .ant-card-head-title { font-size: 15px; font-weight: 600; color: #0F172A; }
  }
  .ant-card-body { padding: 20px 24px; }
`;

const ImageCard = styled(Card)`
  border-radius: 12px;
  border: 1px solid #E2E8F0;
  overflow: hidden;
  transition: box-shadow 0.2s ease;

  &:hover { box-shadow: 0 4px 16px rgba(0,0,0,0.08); }

  .ant-card-cover {
    height: 180px; background: #F8FAFC; display: flex;
    align-items: center; justify-content: center; cursor: pointer;
    img { max-width: 100%; max-height: 100%; object-fit: contain; transition: transform 0.2s; }
    &:hover img { transform: scale(1.05); }
  }
  .ant-card-body { padding: 14px 16px; }
  .ant-card-meta-title { font-size: 13.5px; font-weight: 600; color: #0F172A; }
  .ant-card-meta-description { font-size: 12px; color: #94A3B8; }
  .ant-card-actions {
    border-top: 1px solid #F1F5F9; background: #FAFBFC;
    > li > span { color: #64748B; cursor: pointer; transition: color 0.15s; &:hover { color: #2563EB; } }
  }
`;

const StyledTable = styled(Table)`
  .ant-table { border-radius: 14px; overflow: hidden; border: 1px solid #E2E8F0; }
  .ant-table-thead > tr > th {
    background: #F8FAFC; border-bottom: 1px solid #E2E8F0;
    font-size: 12.5px; font-weight: 600; color: #64748B; padding: 12px 16px;
  }
  .ant-table-tbody > tr > td { padding: 12px 16px; border-bottom: 1px solid #F1F5F9; font-size: 13.5px; color: #334155; }
  .ant-table-tbody > tr:hover > td { background: #F8FAFC; }
`;

const getFileIcon = (type) => {
  if (!type) return <FileOutlined style={{ color: '#94A3B8' }} />;
  const t = type.toLowerCase();
  const colors = { pdf: '#DC2626', doc: '#2563EB', docx: '#2563EB', xls: '#059669', xlsx: '#059669', zip: '#D97706', rar: '#D97706', '7z': '#D97706', tar: '#D97706', gz: '#D97706', txt: '#64748B', md: '#64748B', log: '#64748B' };
  const color = colors[t] || '#94A3B8';
  if (t === 'pdf') return <FilePdfOutlined style={{ color }} />;
  if (['doc', 'docx'].includes(t)) return <FileWordOutlined style={{ color }} />;
  if (['xls', 'xlsx'].includes(t)) return <FileExcelOutlined style={{ color }} />;
  if (['zip', 'rar', '7z', 'tar', 'gz'].includes(t)) return <FileZipOutlined style={{ color }} />;
  if (['txt', 'md', 'log'].includes(t)) return <FileTextOutlined style={{ color }} />;
  return <FileOutlined style={{ color }} />;
};

const isImageType = (type) => false || (type && ['png','jpg','jpeg','gif','bmp','webp','svg'].includes(type.toLowerCase()));
const formatSize = (b) => {
  if (!b || b === 0) return '-';
  if (b < 1024) return `${b} B`;
  if (b < 1024*1024) return `${(b/1024).toFixed(1)} KB`;
  return `${(b/1024/1024).toFixed(2)} MB`;
};

const SharedFiles = () => {
  const [imageFiles, setImageFiles] = useState([]);
  const [otherFiles, setOtherFiles] = useState([]);
  const [previewVisible, setPreviewVisible] = useState(false);
  const [previewImage, setPreviewImage] = useState('');
  const [previewTitle, setPreviewTitle] = useState('');
  const { user } = useAuth();

  const load = async () => {
    try {
      const result = await fetchSharedFiles(0, 100);
      setImageFiles(result.files.filter(f => isImageType(f.type)));
      setOtherFiles(result.files.filter(f => !isImageType(f.type)));
    } catch (e) { message.error('获取共享文件列表失败'); }
  };

  useEffect(() => { load(); }, []);

  const handleDownload = async (file) => {
    try { await pvSharedFile(file); } catch (e) {}
    const link = document.createElement('a'); link.href = file.url; link.download = file.name || file.file_name;
    document.body.appendChild(link); link.click(); document.body.removeChild(link);
    load();
  };

  const handleSave = async (file) => {
    if (!user?.token) { message.warning('请先登录'); return; }
    try { await saveSharedFile(file, user); message.success('转存成功！'); }
    catch (error) { message[error.message === '文件已存在' ? 'warning' : 'error'](error.message === '文件已存在' ? '文件已存在' : '转存失败！'); }
  };

  const columns = [
    { title: '文件名', dataIndex: 'file_name', key: 'file_name', render: (t, r) => <Space>{getFileIcon(r.type)}<span style={{fontWeight:500}}>{t}</span></Space> },
    { title: '分享者', dataIndex: 'user', key: 'user', width: 120 },
    { title: '类型', dataIndex: 'type', key: 'type', width: 80, render: (t) => (t||'').toUpperCase() },
    { title: '大小', dataIndex: 'size', key: 'size', width: 100, render: (s) => formatSize(s) },
    { title: '下载', dataIndex: 'pv', key: 'pv', width: 70 },
    { title: '时间', dataIndex: 'create_time', key: 'create_time', width: 170 },
    { title: '操作', key: 'action', width: 100, render: (_, r) => (
      <Space size={4}>
        <Tooltip title="转存"><Button type="text" size="small" icon={<SaveOutlined />} onClick={() => handleSave(r)} /></Tooltip>
        <Tooltip title="下载"><Button type="text" size="small" icon={<DownloadOutlined />} onClick={() => handleDownload(r)} /></Tooltip>
      </Space>
    )},
  ];

  return (
    <div>
      <PageHeader>
        <h1>共享文件</h1>
        <p>浏览其他用户分享的文件</p>
      </PageHeader>

      {imageFiles.length > 0 && (
        <SectionCard title="共享图片">
          <Row gutter={[16, 16]}>
            {imageFiles.map(img => (
              <Col xs={24} sm={12} md={8} lg={6} key={img.md5 + img.file_name}>
                <ImageCard
                  cover={<img alt={img.name} src={img.url} onClick={() => { setPreviewImage(img.url); setPreviewTitle(img.name||img.file_name); setPreviewVisible(true); }} />}
                  actions={[
                    <Tooltip title="转存"><SaveOutlined onClick={() => handleSave(img)} /></Tooltip>,
                    <Tooltip title="下载"><DownloadOutlined onClick={() => handleDownload(img)} /></Tooltip>,
                    <Tooltip title="预览"><EyeOutlined onClick={() => { setPreviewImage(img.url); setPreviewTitle(img.name||img.file_name); setPreviewVisible(true); }} /></Tooltip>,
                  ]}
                >
                  <Card.Meta title={img.name || img.file_name} description={`${img.user} · ${img.pv||0}次下载`} />
                </ImageCard>
              </Col>
            ))}
          </Row>
        </SectionCard>
      )}

      {otherFiles.length > 0 && (
        <SectionCard title="共享文件">
          <StyledTable columns={columns} dataSource={otherFiles} rowKey={(r) => r.md5 + r.file_name} pagination={{ pageSize: 10 }} locale={{ emptyText: '暂无' }} />
        </SectionCard>
      )}

      {imageFiles.length === 0 && otherFiles.length === 0 && (
        <SectionCard><div style={{ textAlign: 'center', padding: '40px 0', color: '#94A3B8' }}>暂无共享文件</div></SectionCard>
      )}

      <Modal open={previewVisible} title={previewTitle} footer={null} onCancel={() => setPreviewVisible(false)} width="auto" style={{ maxWidth: '90vw' }}>
        <img alt={previewTitle} style={{ maxWidth: '100%', maxHeight: '80vh' }} src={previewImage} />
      </Modal>
    </div>
  );
};

export default SharedFiles;
