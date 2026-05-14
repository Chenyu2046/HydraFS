import React, { useState, useEffect } from 'react';
import { Upload, Card, Row, Col, Modal, message, Tooltip, Progress, Segmented } from 'antd';
import { PlusOutlined, ShareAltOutlined, DeleteOutlined, DownloadOutlined, CheckCircleOutlined, PictureOutlined, AppstoreOutlined } from '@ant-design/icons';
import styled from '@emotion/styled';
import { useAuth } from '../contexts/AuthContext';
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

const UploadCard = styled(Card)`
  border-radius: 20px;
  border: 2px dashed #E2E8F0;
  background: #ffffff;
  height: 100%;
  transition: all 0.3s cubic-bezier(0.2, 0.8, 0.2, 1);

  &:hover {
    border-color: #007AFF;
    background: #F5F5F7;
    transform: translateY(-2px);
  }

  .ant-card-body {
    padding: 24px;
    height: 100%;
    display: flex;
    flex-direction: column;
  }

  .ant-upload-wrapper {
    height: 100%;
  }

  .ant-upload-drag {
    border: none !important;
    background: transparent !important;
    height: 100% !important;
    min-height: 220px;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
  }

  .ant-upload-drag-icon {
    font-size: 32px;
    color: #007AFF;
    margin-bottom: 12px;
  }

  .ant-upload-text {
    font-size: 14px;
    font-weight: 600;
    color: #1D1D1F;
  }
  
  .ant-upload-hint {
    font-size: 12px;
    color: #86868B;
    margin-top: 4px;
  }
`;

const ImageCard = styled(Card)`
  border-radius: 20px;
  border: none;
  background: #ffffff;
  overflow: hidden;
  box-shadow: 0 4px 20px rgba(0,0,0,0.04);
  transition: all 0.3s cubic-bezier(0.2, 0.8, 0.2, 1);

  &:hover {
    box-shadow: 0 12px 32px rgba(0,0,0,0.08);
    transform: translateY(-4px);
  }

  .ant-card-cover {
    height: 200px;
    background: #F5F5F7;
    position: relative;
    overflow: hidden;
    cursor: pointer;

    img {
      width: 100%;
      height: 100%;
      object-fit: cover;
      transition: transform 0.3s ease;
    }

    &:hover img {
      transform: scale(1.05);
    }
  }

  .ant-card-body {
    padding: 16px 20px;
  }

  .ant-card-meta-title {
    font-size: 14px;
    font-weight: 600;
    color: #1D1D1F;
    margin-bottom: 4px !important;
  }

  .ant-card-meta-description {
    font-size: 12px;
    color: #86868B;
  }

  .ant-card-actions {
    border-top: 1px solid #F5F5F7;
    padding: 8px 0;
    
    > li {
      margin: 0;
    }

    > li > span {
      color: #86868B;
      font-size: 16px;
      transition: all 0.2s ease;
      
      &:hover {
        color: #007AFF;
        transform: scale(1.1);
      }
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
        <div className="title-area">
          <h1>图片</h1>
          <p>上传与管理图片文件</p>
        </div>
      </PageHeader>

      <Row gutter={[20, 20]} align="stretch">
        <Col xs={24} sm={12} md={8} lg={6}>
          <UploadCard>
            <Upload.Dragger
              accept="image/*"
              showUploadList={false}
              disabled={uploading}
              beforeUpload={(file) => { handleUpload(file); return false; }}
            >
              <p className="ant-upload-drag-icon"><PlusOutlined /></p>
              <p className="ant-upload-text">点击或拖拽上传</p>
              <p className="ant-upload-hint">支持 JPG, PNG 等常用格式</p>
            </Upload.Dragger>
            {uploading && <Progress percent={uploadProgress} status="active" style={{ marginTop: 16 }} strokeColor="#007AFF" />}                                                      </UploadCard>
        </Col>
        {images.map(image => (
          <Col xs={24} sm={12} md={8} lg={6} key={image.md5 || image.id}>
            <ImageCard
              cover={<img alt={image.name} src={image.url} onClick={() => handlePreview(image)} />}
              actions={[
                image.share_status === 1
                  ? <Tooltip title="已分享（点击取消）"><CheckCircleOutlined style={{ color: '#34C759' }} onClick={() => handleCancelShare(image)} /></Tooltip>                               : <Tooltip title="分享"><ShareAltOutlined onClick={() => handleShare(image)} /></Tooltip>,                                                                                <Tooltip title="下载"><DownloadOutlined onClick={() => handleDownload(image)} /></Tooltip>,                                                                                 <Tooltip title="删除"><DeleteOutlined onClick={() => handleDelete(image)} style={{ color: '#FF3B30' }} /></Tooltip>,                                                      ]}
            >
              <Card.Meta 
                title={image.name} 
                description={`${image.pv || 0} 次查看 · ${new Date(image.create_time).toLocaleDateString()}`}                                                                               />
            </ImageCard>
          </Col>
        ))}
      </Row>

      <Modal open={previewVisible} title={null} footer={null} onCancel={() => setPreviewVisible(false)} width="auto" style={{ maxWidth: '90vw' }} centered>                         <img alt={previewTitle} style={{ maxWidth: '100%', maxHeight: '85vh', display: 'block', margin: '0 auto', borderRadius: '8px' }} src={previewImage} />                    </Modal>
    </div>
  );
};

export default ImageList;
