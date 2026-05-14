import React, { useState, useEffect } from 'react';
import { Table, Card, Tag, Button, message, Tooltip, Space } from 'antd';
import { DownloadOutlined, SaveOutlined } from '@ant-design/icons';
import styled from '@emotion/styled';
import { useAuth } from '../contexts/AuthContext';
import { fetchSharedFilesRanking, saveSharedFile, pvSharedFile } from '../services/share';

const PageHeader = styled.div`
  margin-bottom: 24px;
  h1 { font-size: 22px; font-weight: 700; color: #0F172A; margin: 0 0 4px; letter-spacing: -0.3px; }
  p { font-size: 14px; color: #64748B; margin: 0; }
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

const RankBadge = styled.span`
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 28px;
  height: 28px;
  border-radius: 8px;
  font-weight: 700;
  font-size: 13px;
  background: ${props => props.$rank === 0 ? '#FEF3C7' : props.$rank === 1 ? '#E2E8F0' : props.$rank === 2 ? '#FED7AA' : '#F1F5F9'};
  color: ${props => props.$rank === 0 ? '#92400E' : props.$rank === 1 ? '#475569' : props.$rank === 2 ? '#9A3412' : '#94A3B8'};
`;

const formatSize = (bytes) => {
  if (!bytes || bytes === 0) return '-';
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / 1024 / 1024).toFixed(2)} MB`;
};

const TopDownloads = () => {
  const [topFiles, setTopFiles] = useState([]);
  const [loading, setLoading] = useState(false);
  const { user } = useAuth();

  useEffect(() => { fetchTopDownloads(); }, []);

  const fetchTopDownloads = async () => {
    setLoading(true);
    try { const result = await fetchSharedFilesRanking(0, 50); setTopFiles(result.files || []); }
    catch (error) { message.error('获取下载榜失败！'); }
    finally { setLoading(false); }
  };

  const handleDownload = async (file) => {
    try { await pvSharedFile(file); } catch (e) {}
    const link = document.createElement('a'); link.href = file.url; link.download = file.file_name;
    document.body.appendChild(link); link.click(); document.body.removeChild(link);
    fetchTopDownloads();
  };

  const handleSave = async (file) => {
    if (!user?.token) { message.warning('请先登录'); return; }
    try { await saveSharedFile(file, user); message.success('转存成功！'); }
    catch (error) { message[error.message === '文件已存在' ? 'warning' : 'error'](error.message === '文件已存在' ? '文件已存在' : '转存失败！'); }
  };

  const columns = [
    {
      title: '#', key: 'rank', width: 60,
      render: (_, __, index) => <RankBadge $rank={index}>{index + 1}</RankBadge>,
    },
    { title: '文件名', dataIndex: 'file_name', key: 'file_name', render: (t) => <span style={{ fontWeight: 500 }}>{t}</span> },
    {
      title: '类型', dataIndex: 'type', key: 'type', width: 90,
      render: (t) => <Tag style={{ borderRadius: 6, background: '#F1F5F9', border: 'none', color: '#475569', fontWeight: 500, fontSize: 11 }}>{(t||'').toUpperCase()}</Tag>,
    },
    { title: '大小', dataIndex: 'size', key: 'size', width: 100, render: (s) => formatSize(s) },
    { title: '分享者', dataIndex: 'user', key: 'user', width: 120 },
    {
      title: '下载次数', dataIndex: 'pv', key: 'pv', width: 100,
      render: (pv) => <span style={{ fontWeight: 700, color: '#2563EB' }}>{pv || 0}</span>,
      sorter: (a, b) => (a.pv || 0) - (b.pv || 0),
      defaultSortOrder: 'descend',
    },
    {
      title: '操作', key: 'action', width: 100,
      render: (_, record) => (
        <Space size={4}>
          <Tooltip title="下载"><Button type="text" size="small" icon={<DownloadOutlined />} onClick={() => handleDownload(record)} /></Tooltip>
          <Tooltip title="转存"><Button type="text" size="small" icon={<SaveOutlined />} onClick={() => handleSave(record)} /></Tooltip>
        </Space>
      ),
    },
  ];

  return (
    <div>
      <PageHeader>
        <h1>下载榜</h1>
        <p>热门共享文件排行</p>
      </PageHeader>
      <StyledTable
        columns={columns}
        dataSource={topFiles}
        rowKey={(r) => r.md5 + r.file_name}
        loading={loading}
        pagination={{ pageSize: 10, showTotal: (total) => `共 ${total} 个文件` }}
        locale={{ emptyText: '暂无数据' }}
      />
    </div>
  );
};

export default TopDownloads;
