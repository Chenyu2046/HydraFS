import React, { useState, useEffect } from 'react';
import { Upload, Card, Table, message, Button, Tooltip, Space, Progress, Tag } from 'antd';
import {
  UploadOutlined,
  ShareAltOutlined,
  DeleteOutlined,
  DownloadOutlined,
  FileOutlined,
  FilePdfOutlined,
  FileWordOutlined,
  FileExcelOutlined,
  FileZipOutlined,
  FileTextOutlined,
  CheckCircleOutlined,
  BookOutlined,
  LoadingOutlined,
  CloseCircleOutlined,
  MinusCircleOutlined,
} from '@ant-design/icons';
import styled from '@emotion/styled';
import { useAuth } from '../contexts/AuthContext';
import { useNavigate } from 'react-router-dom';
import { fetchUserImages, uploadImage, deleteImage, shareFile, cancelShareFile, pvFile } from '../services/images';
import { describeFile } from '../services/ai';

const PageHeader = styled.div`
  margin-bottom: 24px;
  h1 { font-size: 22px; font-weight: 700; color: #0F172A; margin: 0 0 4px; letter-spacing: -0.3px; }
  p { font-size: 14px; color: #64748B; margin: 0; }
`;

const UploadCard = styled(Card)`
  border-radius: 14px;
  border: 2px dashed #E2E8F0;
  background: #FAFBFC;
  transition: border-color 0.2s, background 0.2s;
  margin-bottom: 24px;

  &:hover {
    border-color: #2563EB;
    background: #F8FAFF;
  }

  .ant-card-body { padding: 20px; }
  .ant-upload-drag { border: none; background: transparent; height: 140px; }
  .ant-upload-drag-icon { font-size: 32px; color: #2563EB; margin-bottom: 8px; }
  .ant-upload-text { font-size: 14px; font-weight: 500; color: #475569; }
  .ant-upload-hint { font-size: 12px; color: #94A3B8; }
`;

const StyledTable = styled(Table)`
  .ant-table {
    border-radius: 14px;
    overflow: hidden;
    border: 1px solid #E2E8F0;
  }

  .ant-table-thead > tr > th {
    background: #F8FAFC;
    border-bottom: 1px solid #E2E8F0;
    font-size: 12.5px;
    font-weight: 600;
    color: #64748B;
    text-transform: uppercase;
    letter-spacing: 0.3px;
    padding: 12px 16px;
  }

  .ant-table-tbody > tr > td {
    padding: 12px 16px;
    border-bottom: 1px solid #F1F5F9;
    font-size: 13.5px;
    color: #334155;
  }

  .ant-table-tbody > tr:hover > td {
    background: #F8FAFC;
  }
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

const FileList = () => {
  const [files, setFiles] = useState([]);
  const [uploading, setUploading] = useState(false);
  const [uploadProgress, setUploadProgress] = useState(0);
  const { user, logout } = useAuth();
  const navigate = useNavigate();
  const uploadingRef = React.useRef(false);

  const fetchFiles = async () => {
    try {
      const data = await fetchUserImages(user);
      const imageExts = ['png', 'jpg', 'jpeg', 'gif', 'bmp', 'webp', 'svg', 'ico'];
      setFiles(data.filter(f => !imageExts.includes((f.type || '').toLowerCase())));
    } catch (error) {
      console.error('获取文件列表错误：', error);
      if (error.tokenExpired) { message.error('登录已过期'); logout(); return; }
      message.error('获取文件列表失败');
    }
  };

  const handleUpload = async (file) => {
    if (uploadingRef.current) return;
    uploadingRef.current = true;
    try {
      setUploading(true); setUploadProgress(0);
      const result = await uploadImage(file, user, (progress) => setUploadProgress(progress));
      if (result.alreadyExists) message.warning('文件已存在');
      else if (result.instant) message.success('秒传成功！');
      else message.success('上传成功！');
      describeFile(file, user).catch(() => {});
      fetchFiles();
    } catch (error) {
      console.error('上传错误：', error);
      if (error.tokenExpired) { message.error('登录已过期'); logout(); return; }
      message.error('上传失败！');
    } finally { uploadingRef.current = false; setUploading(false); setUploadProgress(0); }
  };

  const handleDelete = async (record) => {
    try { await deleteImage(record, user); message.success('删除成功！'); fetchFiles(); }
    catch (error) { message.error('删除失败！'); }
  };

  const handleShare = async (record) => {
    try { await shareFile(record, user); message.success('分享成功！'); fetchFiles(); }
    catch (error) { message.error('分享失败！'); }
  };

  const handleCancelShare = async (record) => {
    try { await cancelShareFile(record, user); message.success('取消分享成功！'); fetchFiles(); }
    catch (error) { message.error('取消分享失败！'); }
  };

  const handleDownload = async (record) => {
    try { await pvFile(record, user); } catch (e) {}
    const link = document.createElement('a');
    link.href = record.url; link.download = record.name;
    document.body.appendChild(link); link.click(); document.body.removeChild(link);
  };

  useEffect(() => { if (user && user.token) { fetchFiles(); } }, [user]);

  const formatSize = (size) => {
    if (!size) return '-';
    if (size < 1024) return `${size} B`;
    if (size < 1024 * 1024) return `${(size / 1024).toFixed(1)} KB`;
    return `${(size / 1024 / 1024).toFixed(2)} MB`;
  };

  const columns = [
    {
      title: '文件名',
      dataIndex: 'file_name',
      key: 'file_name',
      render: (text, record) => (
        <Space>
          {getFileIcon(record.type)}
          <span style={{ fontWeight: 500 }}>{text}</span>
        </Space>
      ),
    },
    {
      title: '类型', dataIndex: 'type', key: 'type', width: 80,
      render: (t) => <Tag style={{ borderRadius: 6, background: '#F1F5F9', border: 'none', color: '#475569', fontWeight: 500, fontSize: 11 }}>{(t || '').toUpperCase()}</Tag>,
    },
    {
      title: '解析', dataIndex: 'parse_status', key: 'parse_status', width: 100,
      render: (status) => {
        const m = {
          pending:   { color: 'default', icon: <MinusCircleOutlined />, text: '待处理' },
          running:   { color: 'processing', icon: <LoadingOutlined />, text: '解析中' },
          success:   { color: 'success', icon: <CheckCircleOutlined />, text: '已解析' },
          failed:    { color: 'error', icon: <CloseCircleOutlined />, text: '失败' },
          skipped:   { color: 'warning', icon: <MinusCircleOutlined />, text: '跳过' },
        };
        const s = m[status] || m.pending;
        return <Tag color={s.color} icon={s.icon} style={{ borderRadius: 6 }}>{s.text}</Tag>;
      },
    },
    { title: '大小', dataIndex: 'size', key: 'size', width: 100, render: (s) => formatSize(s) },
    { title: '上传时间', dataIndex: 'create_time', key: 'create_time', width: 170 },
    { title: '下载', dataIndex: 'pv', key: 'pv', width: 70 },
    {
      title: '操作', key: 'action', width: 160,
      render: (_, record) => (
        <Space size={4}>
          {record.share_status === 1
            ? <Tooltip title="取消分享"><Button type="text" size="small" icon={<CheckCircleOutlined style={{ color: '#059669' }} />} onClick={() => handleCancelShare(record)} /></Tooltip>
            : <Tooltip title="分享"><Button type="text" size="small" icon={<ShareAltOutlined />} onClick={() => handleShare(record)} /></Tooltip>}
          <Tooltip title="下载"><Button type="text" size="small" icon={<DownloadOutlined />} onClick={() => handleDownload(record)} /></Tooltip>
          <Tooltip title="删除"><Button type="text" size="small" danger icon={<DeleteOutlined />} onClick={() => handleDelete(record)} /></Tooltip>
          {record.wiki_ready === 1 && (
            <Tooltip title="Wiki"><Button type="text" size="small" icon={<BookOutlined />} onClick={() => navigate(`/wiki/${record.md5}`)} /></Tooltip>
          )}
        </Space>
      ),
    },
  ];

  return (
    <div>
      <PageHeader>
        <h1>文件</h1>
        <p>上传与管理文档、压缩包等非图片文件</p>
      </PageHeader>

      <UploadCard>
        <Upload.Dragger showUploadList={false} disabled={uploading} beforeUpload={(file) => { handleUpload(file); return false; }}>
          <p className="ant-upload-drag-icon"><UploadOutlined /></p>
          <p className="ant-upload-text">点击或拖拽上传文件</p>
          <p className="ant-upload-hint">支持任意类型，大文件自动分片上传</p>
        </Upload.Dragger>
        {uploading && <Progress percent={uploadProgress} status="active" style={{ marginTop: 12 }} strokeColor="#2563EB" />}
      </UploadCard>

      <StyledTable columns={columns} dataSource={files} rowKey="md5" pagination={{ pageSize: 10 }} locale={{ emptyText: '暂无文件' }} />
    </div>
  );
};

export default FileList;
