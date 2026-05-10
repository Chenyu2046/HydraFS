import React, { useState, useEffect } from 'react';
import { Row, Col, Card, Statistic, List, Avatar, Button, message, Input, Modal, Tag, Empty, Spin } from 'antd';
import {
  CloudUploadOutlined,
  FileOutlined,
  DownloadOutlined,
  ShareAltOutlined,
  DeleteOutlined,
  SearchOutlined
} from '@ant-design/icons';
import styled from '@emotion/styled';
import { useAuth } from '../contexts/AuthContext';
import { useNavigate } from 'react-router-dom';
import { fetchUserImages, shareFile, cancelShareFile, deleteImage, pvFile } from '../services/images';
import { aiSearch, fetchApiKey, saveApiKey, describeFileByMd5, rebuildIndex } from '../services/ai';

const StyledCard = styled(Card)`
  margin-bottom: 24px;
  .ant-card-head {
    border-bottom: none;
  }
`;

const SearchResultCard = styled(Card)`
  margin-bottom: 16px;
  .search-item {
    display: flex;
    align-items: center;
    padding: 12px 0;
    border-bottom: 1px solid #f0f0f0;
    &:last-child { border-bottom: none; }
  }
  .search-item-thumb {
    width: 60px;
    height: 60px;
    margin-right: 16px;
    border-radius: 4px;
    object-fit: cover;
    background: #f5f5f5;
    display: flex;
    align-items: center;
    justify-content: center;
  }
  .search-item-info {
    flex: 1;
  }
  .search-item-name {
    font-weight: 500;
    margin-bottom: 4px;
  }
  .search-item-desc {
    color: #888;
    font-size: 12px;
    display: -webkit-box;
    -webkit-line-clamp: 2;
    -webkit-box-orient: vertical;
    overflow: hidden;
  }
  .search-item-score {
    margin-left: 12px;
  }
`;

const Home = () => {
  const [stats, setStats] = useState({
    totalFiles: 0,
    totalDownloads: 0,
    totalShares: 0,
    storageUsed: '0 B'
  });
  const [recentFiles, setRecentFiles] = useState([]);
  const [searchQuery, setSearchQuery] = useState('');
  const [searchResults, setSearchResults] = useState(null);
  const [searching, setSearching] = useState(false);
  const [apiKeyInput, setApiKeyInput] = useState('');
  const [apiKeySaved, setApiKeySaved] = useState(false);
  const [apiKeyLoaded, setApiKeyLoaded] = useState('');  // 记录从浏览器本地加载的 key
  const [rebuilding, setRebuilding] = useState(false);
  const { user, logout } = useAuth();
  const navigate = useNavigate();

  useEffect(() => {
    if (user && user.token) {
      fetchDashboardData();
      // 从浏览器本地加载 API Key
      fetchApiKey(user).then(key => {
        if (key) {
          setApiKeyInput(key);
          setApiKeySaved(true);
          setApiKeyLoaded(key);
        }
      }).catch(err => {
        if (err.tokenExpired) {
          message.error('登录已过期，请重新登录');
          logout();
        }
      });
    }
  }, [user]);

  const formatSize = (bytes) => {
    if (!bytes || bytes === 0) return '0 B';
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
    if (bytes < 1024 * 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(2)} MB`;
    return `${(bytes / 1024 / 1024 / 1024).toFixed(2)} GB`;
  };

  const fetchDashboardData = async () => {
    try {
      const files = await fetchUserImages(user);

      const totalDownloads = files.reduce((sum, f) => sum + (f.pv || 0), 0);
      const totalShares = files.filter(f => f.share_status === 1).length;
      const totalSize = files.reduce((sum, f) => sum + (f.size || 0), 0);

      setStats({
        totalFiles: files.length,
        totalDownloads,
        totalShares,
        storageUsed: formatSize(totalSize)
      });

      const recent = files
        .sort((a, b) => new Date(b.create_time) - new Date(a.create_time))
        .slice(0, 5)
        .map(f => ({
          id: f.md5,
          md5: f.md5,
          file_name: f.file_name || f.name,
          name: f.file_name || f.name,
          type: f.type,
          uploadTime: f.create_time,
          size: formatSize(f.size),
          url: f.url,
          share_status: f.share_status
        }));
      setRecentFiles(recent);
    } catch (error) {
      console.error('获取数据失败：', error);
      if (error.tokenExpired) {
        message.error('登录已过期，请重新登录');
        logout();
        return;
      }
      message.error('无法加载仪表盘数据');
    }
  };

  const handleDownload = async (item) => {
    try {
      await pvFile(item, user);
    } catch (e) {
      // pv更新失败不影响下载
    }
    const link = document.createElement('a');
    link.href = item.url;
    link.download = item.name;
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    fetchDashboardData();
  };

  const handleShare = async (item) => {
    try {
      await shareFile(item, user);
      message.success('分享成功！');
      fetchDashboardData();
    } catch (error) {
      message.error('分享失败！');
    }
  };

  const handleCancelShare = async (item) => {
    try {
      await cancelShareFile(item, user);
      message.success('取消分享成功！');
      fetchDashboardData();
    } catch (error) {
      message.error('取消分享失败！');
    }
  };

  const handleDelete = async (item) => {
    try {
      await deleteImage(item, user);
      message.success('删除成功！');
      fetchDashboardData();
    } catch (error) {
      message.error('删除失败！');
    }
  };

  const handleSaveApiKey = async () => {
    if (!apiKeyInput.trim()) {
      message.warning('请输入 API Key');
      return;
    }
    try {
      await saveApiKey(apiKeyInput.trim(), user);
      setApiKeySaved(true);
      setApiKeyLoaded(apiKeyInput.trim());
      message.success('API Key 已保存');
    } catch (error) {
      if (error.tokenExpired) {
        message.error('登录已过期，请重新登录');
        logout();
        return;
      }
      message.error('保存失败：' + (error.message || '未知错误'));
    }
  };

  const handleClearApiKey = async () => {
    try {
      await saveApiKey('', user);
      setApiKeyInput('');
      setApiKeySaved(false);
      setApiKeyLoaded('');
      message.info('API Key 已清除');
    } catch (error) {
      if (error.tokenExpired) {
        message.error('登录已过期，请重新登录');
        logout();
        return;
      }
      message.error('清除失败：' + (error.message || '未知错误'));
    }
  };

  const handleAiSearch = async () => {
    if (!searchQuery.trim()) {
      message.warning('请输入搜索内容');
      return;
    }
    setSearching(true);
    try {
      const data = await aiSearch(searchQuery, user, apiKeyLoaded);
      setSearchResults(data.files || []);
      if (!data.files || data.files.length === 0) {
        message.info('未找到匹配的文件');
      }
    } catch (error) {
      if (error.tokenExpired) {
        message.error('登录已过期，请重新登录');
        logout();
        return;
      }
      message.error('AI 搜索失败：' + (error.message || '未知错误'));
    } finally {
      setSearching(false);
    }
  };

  const isImageType = (type) => {
    if (!type) return false;
    return ['png','jpg','jpeg','gif','bmp','webp','svg'].includes(type.toLowerCase());
  };

  const handleRebuildDescriptions = async () => {
    setRebuilding(true);
    try {
      const files = await fetchUserImages(user);
      let success = 0;
      for (const f of files) {
        try {
          await describeFileByMd5(f.md5, f.file_name || f.name, f.type, user, apiKeyLoaded, true);
          success++;
        } catch (e) {
          console.warn('describe failed for', f.md5, e);
        }
      }
      await rebuildIndex(user);
      message.success(`AI 描述重建完成：${success}/${files.length} 个文件`);
    } catch (error) {
      if (error.tokenExpired) {
        message.error('登录已过期，请重新登录');
        logout();
        return;
      }
      message.error('重建失败：' + (error.message || '未知错误'));
    } finally {
      setRebuilding(false);
    }
  };

  return (
    <div>
      <StyledCard title="AI 智能搜索" style={{ marginBottom: 24 }}>
        <div style={{ marginBottom: 16 }}>
          <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
            <Input.Password
              placeholder="请输入阿里百炼 API Key（sk-...）"
              value={apiKeyInput}
              onChange={(e) => { setApiKeyInput(e.target.value); setApiKeySaved(false); }}
              style={{ flex: 1 }}
              visibilityToggle
            />
            <Button type="primary" onClick={handleSaveApiKey} disabled={apiKeySaved && apiKeyInput === apiKeyLoaded}>
              {apiKeySaved ? '已保存' : '保存'}
            </Button>
            {apiKeySaved && (
              <Button onClick={handleClearApiKey}>清除</Button>
            )}
          </div>
          <div style={{ color: '#888', fontSize: 12, marginTop: 4 }}>
            API Key 仅保存在浏览器本地，不会上传到服务器存储。用于调用阿里百炼 AI 服务生成文件描述和语义搜索。
          </div>
        </div>
        <div style={{ display: 'flex', gap: 8 }}>
          <Input.Search
            placeholder="描述你想找的文件，例如：红色沙发上的猫、上周的报告..."
            enterButton={<><SearchOutlined /> 搜索</>}
            size="large"
            value={searchQuery}
            onChange={(e) => setSearchQuery(e.target.value)}
            onSearch={handleAiSearch}
            loading={searching}
            allowClear
            disabled={!apiKeySaved}
            style={{ flex: 1 }}
          />
          <Button
            size="large"
            onClick={handleRebuildDescriptions}
            loading={rebuilding}
            disabled={!apiKeySaved}
          >
            {rebuilding ? '生成中...' : '重建 AI 描述'}
          </Button>
        </div>
        {searching && <div style={{ textAlign: 'center', marginTop: 16 }}><Spin tip="AI 搜索中..." /></div>}
        {searchResults !== null && !searching && (
          <div style={{ marginTop: 16 }}>
            {searchResults.length === 0 ? (
              <Empty description="未找到匹配的文件" />
            ) : (
              <List
                dataSource={searchResults}
                renderItem={item => (
                  <div className="search-item" key={item.md5} style={{ display: 'flex', alignItems: 'center', padding: '12px 0', borderBottom: '1px solid #f0f0f0' }}>
                    <div style={{ width: 60, height: 60, marginRight: 16, borderRadius: 4, overflow: 'hidden', background: '#f5f5f5', display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
                      {isImageType(item.type) && item.url ? (
                        <img src={item.url} alt={item.filename} style={{ maxWidth: '100%', maxHeight: '100%', objectFit: 'cover' }} />
                      ) : (
                        <FileOutlined style={{ fontSize: 24, color: '#999' }} />
                      )}
                    </div>
                    <div style={{ flex: 1 }}>
                      <div style={{ fontWeight: 500 }}>{item.filename}</div>
                      <div style={{ color: '#888', fontSize: 12, marginTop: 4 }}>{item.description}</div>
                    </div>
                    <Tag color={item.score >= 0.6 ? 'green' : item.score >= 0.4 ? 'blue' : 'orange'} style={{ marginLeft: 12 }}>
                      相似度 {(Math.max(0, item.score) * 100).toFixed(1)}%
                    </Tag>
                    {item.url && (
                      <Button type="link" icon={<DownloadOutlined />} href={item.url} target="_blank" download={item.filename}>
                        下载
                      </Button>
                    )}
                  </div>
                )}
              />
            )}
          </div>
        )}
      </StyledCard>

      <Row gutter={24}>
        <Col xs={24} sm={12} md={6}>
          <StyledCard>
            <Statistic
              title="总文件数"
              value={stats.totalFiles}
              prefix={<FileOutlined />}
            />
          </StyledCard>
        </Col>
        <Col xs={24} sm={12} md={6}>
          <StyledCard>
            <Statistic
              title="总下载次数"
              value={stats.totalDownloads}
              prefix={<DownloadOutlined />}
            />
          </StyledCard>
        </Col>
        <Col xs={24} sm={12} md={6}>
          <StyledCard>
            <Statistic
              title="已分享文件"
              value={stats.totalShares}
              prefix={<ShareAltOutlined />}
            />
          </StyledCard>
        </Col>
        <Col xs={24} sm={12} md={6}>
          <StyledCard>
            <Statistic
              title="存储空间使用"
              value={stats.storageUsed}
              prefix={<CloudUploadOutlined />}
            />
          </StyledCard>
        </Col>
      </Row>

      <StyledCard title="最近上传">
        <List
          itemLayout="horizontal"
          dataSource={recentFiles}
          locale={{ emptyText: '暂无文件' }}
          renderItem={item => (
            <List.Item
              actions={[
                item.share_status === 1 ? (
                  <Button type="link" icon={<ShareAltOutlined />} onClick={() => handleCancelShare(item)} style={{ color: '#52c41a' }}>
                    已分享
                  </Button>
                ) : (
                  <Button type="link" icon={<ShareAltOutlined />} onClick={() => handleShare(item)}>
                    分享
                  </Button>
                ),
                <Button type="link" icon={<DownloadOutlined />} onClick={() => handleDownload(item)}>
                  下载
                </Button>,
                <Button type="link" danger icon={<DeleteOutlined />} onClick={() => handleDelete(item)}>
                  删除
                </Button>
              ]}
            >
              <List.Item.Meta
                avatar={
                  ['png','jpg','jpeg','gif','bmp','webp','svg'].includes((item.type || '').toLowerCase())
                    ? <Avatar src={item.url} shape="square" />
                    : <Avatar icon={<FileOutlined />} shape="square" />
                }
                title={item.name}
                description={`上传时间：${item.uploadTime || '-'} | 大小：${item.size}`}
              />
            </List.Item>
          )}
        />
      </StyledCard>
    </div>
  );
};

export default Home;
