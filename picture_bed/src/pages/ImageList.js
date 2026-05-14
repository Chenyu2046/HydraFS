import React, { useState, useEffect } from 'react';
import { Upload, Card, Row, Col, Modal, message, Tooltip, Progress } from 'antd';
import { PlusOutlined, ShareAltOutlined, DeleteOutlined, DownloadOutlined, CheckCircleOutlined } from '@ant-design/icons';
import styled from '@emotion/styled';
import { useAuth } from '../contexts/AuthContext';
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

  &:hover {
    border-color: #2563EB;
    background: #F8FAFF;
  }

  .ant-card-body {
    padding: 20px;
  }

  .ant-upload-drag {
    border: none;
    background: transparent;
    height: 200px;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
  }

  .ant-upload-drag-icon {
    font-size: 32px;
    color: #2563EB;
    margin-bottom: 8px;
  }

  .ant-upload-text {
    font-size: 14px;
    font-weight: 500;
    color: #475569;
  }
`;

const ImageCard = styled(Card)`
  border-radius: 12px;
  border: 1px solid #E2E8F0;
  overflow: hidden;
  transition: box-shadow 0.2s ease;

  &:hover {
    box-shadow: 0 4px 16px rgba(0,0,0,0.08);
  }

  .ant-card-cover {
    height: 180px;
    background: #F8FAFC;
    display: flex;
    align-items: center;
    justify-content: center;
    cursor: pointer;

    img {
      max-width: 100%;
      max-height: 100%;
      object-fit: contain;
      transition: transform 0.2s ease;
    }

    &:hover img {
      transform: scale(1.05);
    }
  }

  .ant-card-body {
    padding: 14px 16px;
  }

  .ant-card-meta-title {
    font-size: 13.5px;
    font-weight: 600;
    color: #0F172A;
    margin-bottom: 2px !important;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }

  .ant-card-meta-description {
    font-size: 12px;
    color: #94A3B8;
  }

  .ant-card-actions {
    border-top: 1px solid #F1F5F9;
    background: #FAFBFC;

    > li > span {
      color: #64748B;
      font-size: 16px;
      cursor: pointer;
      transition: color 0.15s;

      &:hover { color: #2563EB; }
    }
  }
`;

const ImageList = () => {
  const [images, setImages] = useState([]);
  const [previewVisible, setPreviewVisible] = useState(false);
  const [previewImage, setPreviewImage] = useState('');
  const [previewTitle, setPreviewTitle] = useState('');
  const [uploading, setUploading] = useState(false);
  const [uploadProgress, setUploadProgress] = useState(0);
  const { user, logout } = useAuth();
  const uploadingRef = React.useRef(false);

  const isImageFile = (file) => {
    const imageTypes = ['png', 'jpg', 'jpeg', 'gif', 'bmp', 'webp', 'svg'];
    return imageTypes.includes((file.type || '').toLowerCase());
  };

  const fetchImages = async () => {
    try {
      const filesWithUrls = await fetchUserImages(user);
      setImages(filesWithUrls.filter(isImageFile));
    } catch (error) {
      console.error('获取图片列表错误：', error);
      if (error.tokenExpired) { message.error('登录已过期'); logout(); return; }
      message.error('获取图片列表失败');
    }
  };

  const handleUpload = async (file) => {
    if (uploadingRef.current) return;
    uploadingRef.current = true;
    try {
      setUploading(true);
      setUploadProgress(0);
      const result = await uploadImage(file, user, (progress) => setUploadProgress(progress));
      if (result.alreadyExists) message.warning('图片已存在');
      else if (result.instant) message.success('秒传成功！');
      else message.success('上传成功！');
      describeFile(file, user).catch(() => {});
      fetchImages();
    } catch (error) {
      console.error('上传错误：', error);
      if (error.tokenExpired) { message.error('登录已过期'); logout(); return; }
      message.error('上传失败！');
    } finally {
      uploadingRef.current = false;
      setUploading(false);
      setUploadProgress(0);
    }
  };

  const handleDelete = async (image) => {
    try { await deleteImage(image, user); message.success('删除成功！'); fetchImages(); }
    catch (error) { message.error('删除失败！'); }
  };

  useEffect(() => {
    if (user && user.token) { fetchImages(); }
  }, [user]);

  const handlePreview = (file) => {
    setPreviewImage(file.url);
    setPreviewTitle(file.name);
    setPreviewVisible(true);
  };

  const handleShare = async (image) => {
    try { await shareFile(image, user); message.success('分享成功！'); fetchImages(); }
    catch (error) { message.error('分享失败！'); }
  };

  const handleCancelShare = async (image) => {
    try { await cancelShareFile(image, user); message.success('取消分享成功！'); fetchImages(); }
    catch (error) { message.error('取消分享失败！'); }
  };

  const handleDownload = async (image) => {
    try { await pvFile(image, user); } catch (e) {}
    const link = document.createElement('a');
    link.href = image.url; link.download = image.name;
    document.body.appendChild(link); link.click(); document.body.removeChild(link);
  };

  return (
    <div>
      <PageHeader>
        <h1>图片</h1>
        <p>上传与管理图片文件</p>
      </PageHeader>

      <Row gutter={[16, 16]}>
        <Col xs={24} sm={12} md={8} lg={6}>
          <UploadCard>
            <Upload.Dragger
              accept="image/*"
              showUploadList={false}
              disabled={uploading}
              beforeUpload={(file) => { handleUpload(file); return false; }}
            >
              <p className="ant-upload-drag-icon"><PlusOutlined /></p>
              <p className="ant-upload-text">点击或拖拽上传图片</p>
            </Upload.Dragger>
            {uploading && <Progress percent={uploadProgress} status="active" style={{ marginTop: 12 }} strokeColor="#2563EB" />}
          </UploadCard>
        </Col>
        {images.map(image => (
          <Col xs={24} sm={12} md={8} lg={6} key={image.id}>
            <ImageCard
              cover={<img alt={image.name} src={image.url} onClick={() => handlePreview(image)} />}
              actions={[
                image.share_status === 1
                  ? <Tooltip title="已分享（点击取消）"><CheckCircleOutlined style={{ color: '#059669' }} onClick={() => handleCancelShare(image)} /></Tooltip>
                  : <Tooltip title="分享"><ShareAltOutlined onClick={() => handleShare(image)} /></Tooltip>,
                <Tooltip title="下载"><DownloadOutlined onClick={() => handleDownload(image)} /></Tooltip>,
                <Tooltip title="删除"><DeleteOutlined onClick={() => handleDelete(image)} /></Tooltip>,
              ]}
            >
              <Card.Meta title={image.name} description={`${image.pv || 0} 次下载`} />
            </ImageCard>
          </Col>
        ))}
      </Row>

      <Modal open={previewVisible} title={previewTitle} footer={null} onCancel={() => setPreviewVisible(false)} width="auto" style={{ maxWidth: '90vw' }}>
        <img alt={previewTitle} style={{ maxWidth: '100%', maxHeight: '80vh' }} src={previewImage} />
      </Modal>
    </div>
  );
};

export default ImageList;
