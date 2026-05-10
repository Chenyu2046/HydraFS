import React, { useState, useEffect } from 'react';
import { Row, Col, Card, Table, Button, message, Tooltip, Space, Avatar, Modal } from 'antd';
import {
  SaveOutlined,
  DownloadOutlined,
  FileOutlined,
  FilePdfOutlined,
  FileWordOutlined,
  FileExcelOutlined,
  FileZipOutlined,
  FileTextOutlined,
  EyeOutlined,
} from '@ant-design/icons';
import styled from '@emotion/styled';
import { useAuth } from '../contexts/AuthContext';
import { fetchSharedFiles, saveSharedFile, pvSharedFile } from '../services/share';

const StyledCard = styled(Card)`
  margin-bottom: 24px;
`;

const ImagePreviewGrid = styled(Row)`
  margin-bottom: 24px;
`;

const ImageCard = styled(Card)`
  margin-bottom: 16px;
  .ant-card-cover {
    height: 200px;
    overflow: hidden;
    display: flex;
    align-items: center;
    justify-content: center;
    img {
      max-width: 100%;
      max-height: 100%;
      object-fit: contain;
    }
  }
`;

const getFileIcon = (type) => {
  if (!type) return <FileOutlined />;
  const t = type.toLowerCase();
  if (t === 'pdf') return <FilePdfOutlined style={{ color: '#e53935' }} />;
  if (['doc', 'docx'].includes(t)) return <FileWordOutlined style={{ color: '#1976d2' }} />;
  if (['xls', 'xlsx'].includes(t)) return <FileExcelOutlined style={{ color: '#2e7d32' }} />;
  if (['zip', 'rar', '7z', 'tar', 'gz'].includes(t)) return <FileZipOutlined style={{ color: '#f57c00' }} />;
  if (['txt', 'md', 'log'].includes(t)) return <FileTextOutlined style={{ color: '#546e7a' }} />;
  return <FileOutlined />;
};

const isImageType = (type) => {
  if (!type) return false;
  return ['png', 'jpg', 'jpeg', 'gif', 'bmp', 'webp', 'svg'].includes(type.toLowerCase());
};

const formatSize = (bytes) => {
  if (!bytes || bytes === 0) return '-';
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / 1024 / 1024).toFixed(2)} MB`;
};

const SharedFiles = () => {
  const [imageFiles, setImageFiles] = useState([]);
  const [otherFiles, setOtherFiles] = useState([]);
  const [previewVisible, setPreviewVisible] = useState(false);
  const [previewImage, setPreviewImage] = useState('');
  const [previewTitle, setPreviewTitle] = useState('');
  const { user } = useAuth();

  const loadSharedFiles = async () => {
    try {
      const result = await fetchSharedFiles(0, 100);
      const images = result.files.filter(f => isImageType(f.type));
      const others = result.files.filter(f => !isImageType(f.type));
      setImageFiles(images);
      setOtherFiles(others);
    } catch (error) {
      console.error('获取共享文件失败：', error);
      message.error('获取共享文件列表失败');
    }
  };

  useEffect(() => {
    loadSharedFiles();
  }, []);

  const handleDownload = async (file) => {
    try {
      await pvSharedFile(file);
    } catch (e) {
      // pv更新失败不影响下载
    }
    const link = document.createElement('a');
    link.href = file.url;
    link.download = file.name || file.file_name;
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    loadSharedFiles();
  };

  const handleSave = async (file) => {
    if (!user || !user.token) {
      message.warning('请先登录');
      return;
    }
    try {
      await saveSharedFile(file, user);
      message.success('转存成功！');
    } catch (error) {
      if (error.message === '文件已存在') {
        message.warning('文件已存在于您的文件列表中');
      } else {
        message.error('转存失败！');
      }
    }
  };

  const handlePreview = (file) => {
    setPreviewImage(file.url);
    setPreviewTitle(file.name || file.file_name);
    setPreviewVisible(true);
  };

  const columns = [
    {
      title: '文件名',
      dataIndex: 'file_name',
      key: 'file_name',
      render: (text, record) => (
        <Space>
          {getFileIcon(record.type)}
          <span>{text}</span>
        </Space>
      ),
    },
    {
      title: '分享者',
      dataIndex: 'user',
      key: 'user',
      width: 120,
    },
    {
      title: '类型',
      dataIndex: 'type',
      key: 'type',
      width: 80,
      render: (t) => (t || '').toUpperCase(),
    },
    {
      title: '大小',
      dataIndex: 'size',
      key: 'size',
      width: 100,
      render: (size) => formatSize(size),
    },
    {
      title: '下载次数',
      dataIndex: 'pv',
      key: 'pv',
      width: 90,
    },
    {
      title: '分享时间',
      dataIndex: 'create_time',
      key: 'create_time',
      width: 180,
    },
    {
      title: '操作',
      key: 'action',
      width: 120,
      render: (_, record) => (
        <Space>
          <Tooltip title="转存">
            <Button type="text" icon={<SaveOutlined />} onClick={() => handleSave(record)} />
          </Tooltip>
          <Tooltip title="下载">
            <Button type="text" icon={<DownloadOutlined />} onClick={() => handleDownload(record)} />
          </Tooltip>
        </Space>
      ),
    },
  ];

  return (
    <div>
      {imageFiles.length > 0 && (
        <StyledCard title="共享图片">
          <ImagePreviewGrid gutter={[16, 16]}>
            {imageFiles.map(image => (
              <Col xs={24} sm={12} md={8} lg={6} key={image.md5 + image.file_name}>
                <ImageCard
                  cover={<img alt={image.name} src={image.url} onClick={() => handlePreview(image)} style={{ cursor: 'pointer' }} />}
                  actions={[
                    <Tooltip title="转存">
                      <SaveOutlined onClick={() => handleSave(image)} />
                    </Tooltip>,
                    <Tooltip title="下载">
                      <DownloadOutlined onClick={() => handleDownload(image)} />
                    </Tooltip>,
                    <Tooltip title="预览">
                      <EyeOutlined onClick={() => handlePreview(image)} />
                    </Tooltip>,
                  ]}
                >
                  <Card.Meta
                    title={image.name || image.file_name}
                    description={`分享者：${image.user} | 下载：${image.pv || 0}次`}
                  />
                </ImageCard>
              </Col>
            ))}
          </ImagePreviewGrid>
        </StyledCard>
      )}

      {otherFiles.length > 0 && (
        <StyledCard title="共享文件">
          <Table
            columns={columns}
            dataSource={otherFiles}
            rowKey={(record) => record.md5 + record.file_name}
            pagination={{ pageSize: 10 }}
            locale={{ emptyText: '暂无共享文件' }}
          />
        </StyledCard>
      )}

      {imageFiles.length === 0 && otherFiles.length === 0 && (
        <StyledCard>
          <div style={{ textAlign: 'center', padding: '40px 0', color: '#999' }}>
            暂无共享文件
          </div>
        </StyledCard>
      )}

      <Modal
        open={previewVisible}
        title={previewTitle}
        footer={null}
        onCancel={() => setPreviewVisible(false)}
      >
        <img alt={previewTitle} style={{ width: '100%' }} src={previewImage} />
      </Modal>
    </div>
  );
};

export default SharedFiles;
