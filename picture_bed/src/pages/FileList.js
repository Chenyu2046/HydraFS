import React, { useState, useEffect } from 'react';
import { Upload, Card, Table, message, Button, Tooltip, Space, Progress, Tag } from 'antd';
import {
  UploadOutlined,
  DeleteOutlined,
  DownloadOutlined,
  ShareAltOutlined,
  CheckCircleOutlined,
  FileOutlined,
  FileTextOutlined,
  FileImageOutlined,
  FilePdfOutlined,
  FileZipOutlined,
  FileWordOutlined,
  FileExcelOutlined,
  FilePptOutlined,
  PlaySquareOutlined,
  CustomerServiceOutlined,
  SyncOutlined
} from '@ant-design/icons';
import styled from '@emotion/styled';
import { useAuth } from '../contexts/AuthContext';
import { useNavigate } from 'react-router-dom';
import { fetchUserImages, uploadImage, deleteImage, shareFile, cancelShareFile, pvFile } from '../services/images';
import { describeFile } from '../services/ai';

const PageHeader = styled.div`
  margin-bottom: 24px;
  display: flex;
  justify-content: space-between;
  align-items: flex-end;
  
  .title-area {
    h1 { font-size: 28px; font-weight: 700; color: #1D1D1F; margin: 0 0 6px; letter-spacing: -0.5px; }                                                                        p { font-size: 15px; color: #86868B; margin: 0; }
  }
`;

const Toolbar = styled.div`
  display: flex;
  gap: 12px;
  margin-bottom: 24px;
  background: #ffffff;
  padding: 16px 20px;
  border-radius: 16px;
  box-shadow: 0 4px 20px rgba(0,0,0,0.02);
`;

const StyledTable = styled(Table)`
  .ant-table-wrapper {
    background: #ffffff;
    border-radius: 20px;
    box-shadow: 0 4px 24px rgba(0,0,0,0.04);
    overflow: hidden;
  }
  
  .ant-table {
    background: transparent;
  }

  .ant-table-thead > tr > th {
    background: #F5F5F7;
    color: #86868B;
    font-weight: 500;
    border-bottom: 1px solid rgba(0,0,0,0.04);
    padding: 16px 24px;
    
    &::before {
      display: none !important;
    }
  }

  .ant-table-tbody > tr > td {
    padding: 16px 24px;
    border-bottom: 1px solid #F5F5F7;
    transition: background 0.2s ease;
  }

  .ant-table-tbody > tr:hover > td {
    background: #FAFAFC;
  }
  
  .ant-table-pagination {
    margin: 16px 24px !important;
  }
`;

const ActionButton = styled(Button)`
  border: none;
  background: transparent;
  color: #86868B;
  box-shadow: none;
  
  &:hover {
    color: #007AFF;
    background: rgba(0, 122, 255, 0.08);
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
      dataIndex: 'name',
      key: 'name',
      render: (text, record) => (
        <Space size="middle">
          {getFileIcon(record)}
          <span style={{ fontWeight: 500, color: '#1D1D1F' }}>{text}</span>
          {record.share_status === 1 && <Tag color="blue" bordered={false}>已分享</Tag>}
          {record.wiki_ready === 1 && <Tag color="cyan" bordered={false} style={{ cursor: 'pointer' }} onClick={() => navigate(`/wiki/${record.md5}`)}>Wiki</Tag>}                  </Space>
      ),
    },
    {
      title: '大小',
      dataIndex: 'size',
      key: 'size',
      render: (size) => <span style={{ color: '#86868B' }}>{formatSize(size)}</span>,
    },
    {
      title: '上传时间',
      dataIndex: 'uploadTime',
      key: 'uploadTime',
      render: (text) => <span style={{ color: '#86868B' }}>{text}</span>,
    },
    {
      title: '操作',
      key: 'action',
      render: (_, record) => (
        <Space size="small">
          {record.share_status === 1 ? (
            <Tooltip title="取消分享">
              <ActionButton icon={<CheckCircleOutlined style={{ color: '#34C759' }} />} onClick={() => handleCancelShare(record)} />                                                  </Tooltip>
          ) : (
            <Tooltip title="分享">
              <ActionButton icon={<ShareAltOutlined />} onClick={() => handleShare(record)} />                                                                                        </Tooltip>
          )}
          <Tooltip title="下载">
            <ActionButton icon={<DownloadOutlined />} onClick={() => handleDownload(record)} />                                                                                     </Tooltip>
          <Tooltip title="删除">
            <ActionButton icon={<DeleteOutlined />} onClick={() => handleDelete(record)} style={{ color: '#FF3B30' }} className="delete-btn" />                                     </Tooltip>
        </Space>
      ),
    },
  ];

  return (
    <div>
      <PageHeader>
        <div className="title-area">
          <h1>文件管理</h1>
          <p>管理您的所有文件</p>
        </div>
      </PageHeader>

      <Toolbar>
        <Upload
          customRequest={handleUpload}
          showUploadList={false}
          disabled={uploading}
        >
          <Button type="primary" icon={<UploadOutlined />} loading={uploading} size="large" style={{ borderRadius: '12px' }}>                                                           上传文件
          </Button>
        </Upload>
        <Button icon={<SyncOutlined />} onClick={fetchFiles} size="large" style={{ borderRadius: '12px' }}>刷新</Button>                                                              </Toolbar>

      {uploading && (
        <Card style={{ marginBottom: 20, borderRadius: 16, border: 'none', boxShadow: 
'0 4px 20px rgba(0,0,0,0.02)' }}>                                                               <div>正在上传...</div>
          <Progress percent={uploadProgress} strokeColor="#007AFF" />
        </Card>
      )}

      <StyledTable columns={columns} dataSource={files} rowKey="md5" pagination={{ pageSize: 10 }} locale={{ emptyText: '暂无文件' }} />                                          </div>
  );
};

export default FileList;
