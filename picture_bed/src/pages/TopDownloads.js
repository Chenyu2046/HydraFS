import React, { useState, useEffect } from 'react';
import { Table, Card, Tag, Button, message, Tooltip, Space } from 'antd';
import { DownloadOutlined, SaveOutlined } from '@ant-design/icons';
import styled from '@emotion/styled';
import { useAuth } from '../contexts/AuthContext';
// v2: use real backend API for download ranking
import { fetchSharedFilesRanking, saveSharedFile, pvSharedFile } from '../services/share';

const StyledCard = styled(Card)`
  .ant-table-thead > tr > th {
    background: #f7f7f7;
  }
  .download-count {
    font-weight: bold;
    color: #1890ff;
  }
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

  useEffect(() => {
    fetchTopDownloads();
  }, []);

  const fetchTopDownloads = async () => {
    setLoading(true);
    try {
      const result = await fetchSharedFilesRanking(0, 50);
      setTopFiles(result.files || []);
    } catch (error) {
      console.error('获取下载榜失败：', error);
      message.error('获取下载榜失败！');
    } finally {
      setLoading(false);
    }
  };

  const handleDownload = async (file) => {
    try {
      await pvSharedFile(file);
    } catch (e) {
      // pv更新失败不影响下载
    }
    const link = document.createElement('a');
    link.href = file.url;
    link.download = file.file_name;
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    fetchTopDownloads();
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

  const columns = [
    {
      title: '排名',
      dataIndex: 'rank',
      key: 'rank',
      width: 80,
      render: (_, __, index) => (
        <Tag color={index < 3 ? 'gold' : 'default'}>
          {index + 1}
        </Tag>
      ),
    },
    {
      title: '文件名',
      dataIndex: 'file_name',
      key: 'file_name',
    },
    {
      title: '类型',
      dataIndex: 'type',
      key: 'type',
      width: 100,
      render: (type) => (
        <Tag color={
          ['png','jpg','jpeg','gif','bmp','webp','svg'].includes((type || '').toLowerCase()) ? 'blue' : 'green'
        }>
          {(type || '').toUpperCase()}
        </Tag>
      ),
    },
    {
      title: '大小',
      dataIndex: 'size',
      key: 'size',
      width: 100,
      render: (size) => formatSize(size),
    },
    {
      title: '分享者',
      dataIndex: 'user',
      key: 'user',
      width: 120,
    },
    {
      title: '下载次数',
      dataIndex: 'pv',
      key: 'pv',
      width: 120,
      render: (pv) => (
        <span className="download-count">{pv || 0}</span>
      ),
      sorter: (a, b) => (a.pv || 0) - (b.pv || 0),
      defaultSortOrder: 'descend',
    },
    {
      title: '操作',
      key: 'action',
      width: 120,
      render: (_, record) => (
        <Space>
          <Tooltip title="下载">
            <Button
              type="text"
              icon={<DownloadOutlined />}
              onClick={() => handleDownload(record)}
            />
          </Tooltip>
          <Tooltip title="转存到我的文件">
            <Button
              type="text"
              icon={<SaveOutlined />}
              onClick={() => handleSave(record)}
            />
          </Tooltip>
        </Space>
      ),
    },
  ];

  return (
    <StyledCard title="下载榜">
      <Table
        columns={columns}
        dataSource={topFiles}
        rowKey={(record) => record.md5 + record.file_name}
        loading={loading}
        pagination={{
          pageSize: 10,
          showTotal: (total) => `共 ${total} 个文件`,
        }}
        locale={{ emptyText: '暂无共享文件数据' }}
      />
    </StyledCard>
  );
};

export default TopDownloads;
