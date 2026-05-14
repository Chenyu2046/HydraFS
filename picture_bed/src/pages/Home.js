import React, { useState, useEffect } from 'react';
import { Row, Col, Card, Statistic, List, Button, message, Input, Tag, Empty, Spin } from 'antd';
import {
  FileOutlined,
  DownloadOutlined,
  ShareAltOutlined,
  CloudOutlined,
  DeleteOutlined,
  SearchOutlined,
  BookOutlined,
} from '@ant-design/icons';
import styled from '@emotion/styled';
import { useAuth } from '../contexts/AuthContext';
import { useNavigate } from 'react-router-dom';
import { fetchUserImages, shareFile, cancelShareFile, deleteImage, pvFile } from '../services/images';
import { aiSearch, fetchApiKey, saveApiKey, describeFileByMd5, rebuildIndex } from '../services/ai';

const PageHeader = styled.div`
  margin-bottom: 28px;
`;

const Title = styled.h1`
  font-size: 24px;
  font-weight: 700;
  color: #0F172A;
  margin: 0 0 4px;
  letter-spacing: -0.3px;
`;

const Subtitle = styled.p`
  font-size: 14px;
  color: #64748B;
  margin: 0;
`;

const StatGrid = styled(Row)`
  margin-bottom: 28px;
`;

const StatCard = styled(Card)`
  border-radius: 14px;
  border: 1px solid #E2E8F0;
  box-shadow: 0 1px 2px rgba(0,0,0,0.03);
  transition: box-shadow 0.2s ease;

  &:hover {
    box-shadow: 0 4px 12px rgba(0,0,0,0.06);
  }

  .ant-card-body {
    padding: 20px 24px;
  }

  .ant-statistic-title {
    font-size: 12.5px;
    color: #94A3B8;
    font-weight: 500;
    text-transform: uppercase;
    letter-spacing: 0.5px;
  }

  .ant-statistic-content {
    font-size: 26px;
    font-weight: 700;
    color: #0F172A;
  }
`;

const SectionCard = styled(Card)`
  border-radius: 14px;
  border: 1px solid #E2E8F0;
  box-shadow: 0 1px 2px rgba(0,0,0,0.03);
  margin-bottom: 24px;

  .ant-card-head {
    border-bottom: 1px solid #F1F5F9;
    padding: 18px 24px;
    min-height: auto;
  }

  .ant-card-head-title {
    font-size: 15px;
    font-weight: 600;
    color: #0F172A;
  }

  .ant-card-body {
    padding: 20px 24px;
  }
`;

const ApiKeyRow = styled.div`
  display: flex;
  gap: 8px;
  align-items: center;
  margin-bottom: 16px;

  .ant-input-password {
    border-radius: 10px;
    border-color: #E2E8F0;
  }
`;

const ApiHint = styled.div`
  font-size: 12px;
  color: #94A3B8;
  margin-top: 4px;
`;

const SearchRow = styled.div`
  display: flex;
  gap: 10px;
`;

const SearchResultItem = styled.div`
  display: flex;
  align-items: center;
  padding: 14px 0;
  border-bottom: 1px solid #F1F5F9;

  &:last-child {
    border-bottom: none;
  }
`;

const ThumbWrap = styled.div`
  width: 52px;
  height: 52px;
  margin-right: 16px;
  border-radius: 8px;
  overflow: hidden;
  background: #F1F5F9;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;

  img {
    width: 100%;
    height: 100%;
    object-fit: cover;
  }
`;

const InfoWrap = styled.div`
  flex: 1;
  min-width: 0;
`;

const FileName = styled.div`
  font-size: 14px;
  font-weight: 600;
  color: #0F172A;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
`;

const FileDesc = styled.div`
  font-size: 12.5px;
  color: #94A3B8;
  margin-top: 3px;
  display: -webkit-box;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
  overflow: hidden;
`;

const RecentListItem = styled(List.Item)`
  padding: 14px 0 !important;
  border-bottom: 1px solid #F1F5F9 !important;

  &:last-child {
    border-bottom: none !important;
  }
`;

const Home = () => {
  const [stats, setStats] = useState({ totalFiles: 0, totalDownloads: 0, totalShares: 0, storageUsed: '0 B' });
  const [recentFiles, setRecentFiles] = useState([]);
  const [searchQuery, setSearchQuery] = useState('');
  const [searchResults, setSearchResults] = useState(null);
  const [searching, setSearching] = useState(false);
  const [apiKeyInput, setApiKeyInput] = useState('');
  const [apiKeySaved, setApiKeySaved] = useState(false);
  const [apiKeyLoaded, setApiKeyLoaded] = useState('');
  const [rebuilding, setRebuilding] = useState(false);
  const { user, logout } = useAuth();
  const navigate = useNavigate();

  useEffect(() => {
    if (user && user.token) {
      fetchDashboardData();
      fetchApiKey(user).then(key => {
        if (key) { setApiKeyInput(key); setApiKeySaved(true); setApiKeyLoaded(key); }
      }).catch(err => {
        if (err.tokenExpired) { message.error('登录已过期，请重新登录'); logout(); }
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
      setStats({ totalFiles: files.length, totalDownloads, totalShares, storageUsed: formatSize(totalSize) });
      const recent = files
        .sort((a, b) => new Date(b.create_time) - new Date(a.create_time))
        .slice(0, 5)
        .map(f => ({
          id: f.md5, md5: f.md5, file_name: f.file_name || f.name,
          name: f.file_name || f.name, type: f.type, uploadTime: f.create_time,
          size: formatSize(f.size), url: f.url, share_status: f.share_status
        }));
      setRecentFiles(recent);
    } catch (error) {
      console.error('获取数据失败：', error);
      if (error.tokenExpired) { message.error('登录已过期'); logout(); return; }
      message.error('无法加载仪表盘数据');
    }
  };

  const handleDownload = async (item) => {
    try { await pvFile(item, user); } catch (e) {}
    const link = document.createElement('a');
    link.href = item.url; link.download = item.name;
    document.body.appendChild(link); link.click(); document.body.removeChild(link);
    fetchDashboardData();
  };

  const handleShare = async (item) => {
    try { await shareFile(item, user); message.success('分享成功！'); fetchDashboardData(); }
    catch (error) { message.error('分享失败！'); }
  };

  const handleCancelShare = async (item) => {
    try { await cancelShareFile(item, user); message.success('取消分享成功！'); fetchDashboardData(); }
    catch (error) { message.error('取消分享失败！'); }
  };

  const handleDelete = async (item) => {
    try { await deleteImage(item, user); message.success('删除成功！'); fetchDashboardData(); }
    catch (error) { message.error('删除失败！'); }
  };

  const handleSaveApiKey = async () => {
    if (!apiKeyInput.trim()) { message.warning('请输入 API Key'); return; }
    try { await saveApiKey(apiKeyInput.trim(), user); setApiKeySaved(true); setApiKeyLoaded(apiKeyInput.trim()); message.success('API Key 已保存'); }
    catch (error) {
      if (error.tokenExpired) { message.error('登录已过期'); logout(); return; }
      message.error('保存失败：' + (error.message || '未知错误'));
    }
  };

  const handleClearApiKey = async () => {
    try { await saveApiKey('', user); setApiKeyInput(''); setApiKeySaved(false); setApiKeyLoaded(''); message.info('API Key 已清除'); }
    catch (error) {
      if (error.tokenExpired) { message.error('登录已过期'); logout(); return; }
      message.error('清除失败：' + (error.message || '未知错误'));
    }
  };

  const handleAiSearch = async () => {
    if (!searchQuery.trim()) { message.warning('请输入搜索内容'); return; }
    setSearching(true);
    try {
      const data = await aiSearch(searchQuery, user, apiKeyLoaded);
      setSearchResults(data.files || []);
      if (!data.files || data.files.length === 0) message.info('未找到匹配的文件');
    } catch (error) {
      if (error.tokenExpired) { message.error('登录已过期'); logout(); return; }
      message.error('AI 搜索失败：' + (error.message || '未知错误'));
    } finally { setSearching(false); }
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
        try { await describeFileByMd5(f.md5, f.file_name || f.name, f.type, user, apiKeyLoaded, true); success++; }
        catch (e) { console.warn('describe failed for', f.md5, e); }
      }
      await rebuildIndex(user);
      message.success(`AI 描述重建完成：${success}/${files.length} 个文件`);
    } catch (error) {
      if (error.tokenExpired) { message.error('登录已过期'); logout(); return; }
      message.error('重建失败：' + (error.message || '未知错误'));
    } finally { setRebuilding(false); }
  };

  return (
    <div>
      <PageHeader>
        <Title>仪表盘</Title>
        <Subtitle>文件概览与智能搜索</Subtitle>
      </PageHeader>

      <StatGrid gutter={16}>
        <Col xs={24} sm={12} md={6}>
          <StatCard>
            <Statistic title="文件总数" value={stats.totalFiles} prefix={<FileOutlined style={{ color: '#2563EB' }} />} />
          </StatCard>
        </Col>
        <Col xs={24} sm={12} md={6}>
          <StatCard>
            <Statistic title="下载次数" value={stats.totalDownloads} prefix={<DownloadOutlined style={{ color: '#059669' }} />} />
          </StatCard>
        </Col>
        <Col xs={24} sm={12} md={6}>
          <StatCard>
            <Statistic title="已分享" value={stats.totalShares} prefix={<ShareAltOutlined style={{ color: '#D97706' }} />} />
          </StatCard>
        </Col>
        <Col xs={24} sm={12} md={6}>
          <StatCard>
            <Statistic title="存储用量" value={stats.storageUsed} prefix={<CloudOutlined style={{ color: '#7C3AED' }} />} />
          </StatCard>
        </Col>
      </StatGrid>

      <SectionCard title="AI 智能搜索">
        <ApiKeyRow>
          <Input.Password
            placeholder="阿里百炼 API Key（sk-...）"
            value={apiKeyInput}
            onChange={(e) => { setApiKeyInput(e.target.value); setApiKeySaved(false); }}
            style={{ flex: 1 }}
          />
          <Button type="primary" onClick={handleSaveApiKey} disabled={apiKeySaved && apiKeyInput === apiKeyLoaded}>
            {apiKeySaved ? '已保存' : '保存'}
          </Button>
          {apiKeySaved && <Button onClick={handleClearApiKey}>清除</Button>}
        </ApiKeyRow>
        <ApiHint>API Key 保存到当前账号，用于调用阿里百炼 AI 服务生成文件描述和语义搜索。</ApiHint>

        <SearchRow>
          <Input.Search
            placeholder="描述你想找的文件，例如：红色沙发上的猫..."
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
          <Button size="large" onClick={handleRebuildDescriptions} loading={rebuilding} disabled={!apiKeySaved}>
            {rebuilding ? '生成中...' : '重建 AI 描述'}
          </Button>
        </SearchRow>

        {searching && <div style={{ textAlign: 'center', marginTop: 20 }}><Spin tip="AI 搜索中..." /></div>}

        {searchResults !== null && !searching && (
          <div style={{ marginTop: 20 }}>
            {searchResults.length === 0 ? (
              <Empty description="未找到匹配的文件" />
            ) : (
              <div>
                {searchResults.map(item => (
                  <SearchResultItem key={item.md5}>
                    <ThumbWrap>
                      {isImageType(item.type) && item.url ? (
                        <img src={item.url} alt={item.filename} />
                      ) : (
                        <FileOutlined style={{ fontSize: 20, color: '#94A3B8' }} />
                      )}
                    </ThumbWrap>
                    <InfoWrap>
                      <FileName>{item.filename}</FileName>
                      <FileDesc>{item.description}</FileDesc>
                      {item.reason && (
                        <div style={{ color: '#2563EB', fontSize: 12, marginTop: 2 }}>{item.reason}</div>
                      )}
                    </InfoWrap>
                    <Tag color={item.score >= 0.6 ? 'green' : item.score >= 0.4 ? 'blue' : 'orange'} style={{ marginLeft: 12 }}>
                      {(Math.max(0, item.score) * 100).toFixed(0)}%
                    </Tag>
                    {item.url && (
                      <Button type="link" icon={<DownloadOutlined />} href={item.url} target="_blank" download={item.filename} style={{ marginLeft: 8 }} />
                    )}
                    {item.wiki_ready === 1 && (
                      <Button type="link" icon={<BookOutlined />} onClick={() => navigate(`/wiki/${item.md5}`)} title="查看 Wiki" />
                    )}
                  </SearchResultItem>
                ))}
              </div>
            )}
          </div>
        )}
      </SectionCard>

      <SectionCard title="最近上传">
        <List
          itemLayout="horizontal"
          dataSource={recentFiles}
          locale={{ emptyText: '暂无文件' }}
          renderItem={item => (
            <RecentListItem
              actions={[
                item.share_status === 1
                  ? <Button type="link" icon={<ShareAltOutlined />} onClick={() => handleCancelShare(item)} style={{ color: '#059669' }}>已分享</Button>
                  : <Button type="link" icon={<ShareAltOutlined />} onClick={() => handleShare(item)}>分享</Button>,
                <Button type="link" icon={<DownloadOutlined />} onClick={() => handleDownload(item)}>下载</Button>,
                <Button type="link" danger icon={<DeleteOutlined />} onClick={() => handleDelete(item)}>删除</Button>
              ]}
            >
              <List.Item.Meta
                avatar={
                  isImageType(item.type)
                    ? <img src={item.url} alt="" style={{ width: 40, height: 40, borderRadius: 8, objectFit: 'cover' }} />
                    : <div style={{ width: 40, height: 40, borderRadius: 8, background: '#F1F5F9', display: 'flex', alignItems: 'center', justifyContent: 'center' }}><FileOutlined style={{ color: '#94A3B8' }} /></div>
                }
                title={<span style={{ fontWeight: 600, fontSize: 14 }}>{item.name}</span>}
                description={<span style={{ color: '#94A3B8', fontSize: 12.5 }}>{item.uploadTime || '-'} · {item.size}</span>}
              />
            </RecentListItem>
          )}
        />
      </SectionCard>
    </div>
  );
};

export default Home;
