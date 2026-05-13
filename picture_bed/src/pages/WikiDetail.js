import React, { useState, useEffect } from 'react';
import { useParams, useNavigate } from 'react-router-dom';
import { Card, Tag, Descriptions, List, Typography, Spin, message, Empty, Button, Space } from 'antd';
import { FileOutlined, LinkOutlined, ArrowLeftOutlined } from '@ant-design/icons';
import { useAuth } from '../contexts/AuthContext';
import { fetchWiki, fetchBacklinks } from '../services/ai';

const { Title, Paragraph, Text } = Typography;

const WikiDetail = () => {
  const { md5 } = useParams();
  const navigate = useNavigate();
  const { user, logout } = useAuth();
  const [wiki, setWiki] = useState(null);
  const [backlinks, setBacklinks] = useState([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);

  useEffect(() => {
    if (user && md5) {
      loadWikiData();
    }
  }, [md5, user]);

  const loadWikiData = async () => {
    setLoading(true);
    setError(null);
    try {
      const [wikiData, blData] = await Promise.all([
        fetchWiki(md5, user),
        fetchBacklinks(md5, user)
      ]);
      setWiki(wikiData);
      setBacklinks(blData || []);
    } catch (e) {
      if (e.tokenExpired) { message.error('登录已过期'); logout(); return; }
      console.error('load wiki failed:', e);
      setError(e.message);
    } finally {
      setLoading(false);
    }
  };

  if (loading) return <div style={{ textAlign: 'center', padding: 60 }}><Spin size="large" tip="加载 Wiki..." /></div>;
  if (error) return <Empty description={error} />;
  if (!wiki) return <Empty description="Wiki 页面不存在" />;

  const parseTags = (tagsStr) => {
    try { return JSON.parse(tagsStr); } catch { return []; }
  };

  const parseOutline = (outlineStr) => {
    try { return JSON.parse(outlineStr); } catch { return []; }
  };

  const tags = parseTags(wiki.tags);
  const outline = parseOutline(wiki.outline);
  const links = wiki.links || [];

  return (
    <div>
      <Space style={{ marginBottom: 16 }}>
        <Button icon={<ArrowLeftOutlined />} onClick={() => navigate(-1)}>返回</Button>
        <Button type="primary" icon={<FileOutlined />}
          href={wiki.source?.md5 ? `/files` : '#'}
          onClick={() => navigate('/files')}>
          查看源文件
        </Button>
      </Space>

      <Card>
        <Title level={3}>{wiki.title || '未命名 Wiki'}</Title>
        <Descriptions column={1} size="small" style={{ marginBottom: 16 }}>
          <Descriptions.Item label="源文件">{wiki.source?.filename || '-'}</Descriptions.Item>
          <Descriptions.Item label="类型">{(wiki.source?.type || '').toUpperCase()}</Descriptions.Item>
        </Descriptions>

        {tags.length > 0 && (
          <div style={{ marginBottom: 16 }}>
            <Text strong>标签：</Text>
            {tags.map((t, i) => <Tag key={i} color="blue">{t}</Tag>)}
          </div>
        )}

        {wiki.summary && (
          <Card type="inner" title="摘要" size="small" style={{ marginBottom: 16 }}>
            <Paragraph>{wiki.summary}</Paragraph>
          </Card>
        )}

        {outline.length > 0 && (
          <Card type="inner" title="内容大纲" size="small" style={{ marginBottom: 16 }}>
            <List size="small" dataSource={outline}
              renderItem={item => <List.Item>{item}</List.Item>} />
          </Card>
        )}

        {links.length > 0 && (
          <Card type="inner" title="概念链接" size="small" style={{ marginBottom: 16 }}>
            {links.map((link, i) => (
              <Tag key={i} icon={<LinkOutlined />} color="green" style={{ marginBottom: 4 }}>{link}</Tag>
            ))}
          </Card>
        )}
      </Card>

      {backlinks.length > 0 && (
        <Card title="反向链接（引用这些概念的文件）" style={{ marginTop: 16 }}>
          {backlinks.map((item, i) => (
            <Card key={i} type="inner" size="small" style={{ marginBottom: 8 }}>
              <Text strong>{item.concept}</Text>
              <List size="small" style={{ marginTop: 8 }}
                dataSource={item.referenced_by || []}
                renderItem={ref => (
                  <List.Item>
                    <FileOutlined style={{ marginRight: 8 }} />
                    <Button type="link" style={{ padding: 0 }}
                      onClick={() => navigate(`/wiki/${ref.md5}`)}>
                      {ref.filename}
                    </Button>
                  </List.Item>
                )} />
            </Card>
          ))}
        </Card>
      )}

      {backlinks.length === 0 && (
        <Card style={{ marginTop: 16 }}>
          <Empty description="暂无反向链接（其他文件未引用此文件中的概念）" />
        </Card>
      )}
    </div>
  );
};

export default WikiDetail;
