import React, { useState, useEffect } from 'react';
import { useParams, useNavigate } from 'react-router-dom';
import { Card, Tag, Descriptions, List, Typography, Spin, message, Empty, Button, Space } from 'antd';
import { FileOutlined, LinkOutlined, ArrowLeftOutlined } from '@ant-design/icons';
import styled from '@emotion/styled';
import { useAuth } from '../contexts/AuthContext';
import { fetchWiki, fetchBacklinks } from '../services/ai';

const { Title, Paragraph, Text } = Typography;

const PageHeader = styled.div`
  display: flex;
  align-items: center;
  gap: 12px;
  margin-bottom: 24px;
`;

const StyledCard = styled(Card)`
  border-radius: 14px;
  border: 1px solid #E2E8F0;
  box-shadow: 0 1px 2px rgba(0,0,0,0.03);
  margin-bottom: 16px;

  .ant-card-head {
    border-bottom: 1px solid #F1F5F9;
    padding: 16px 24px;
    .ant-card-head-title { font-size: 15px; font-weight: 600; color: #0F172A; }
  }
  .ant-card-body { padding: 20px 24px; }
`;

const InnerCard = styled(Card)`
  border-radius: 10px;
  border: 1px solid #F1F5F9;
  background: #FAFBFC;
  margin-bottom: 14px;

  .ant-card-head {
    border-bottom: none;
    padding: 12px 16px;
    min-height: auto;
    .ant-card-head-title { font-size: 13.5px; font-weight: 600; color: #475569; }
  }
  .ant-card-body { padding: 8px 16px 14px; }
`;

const WikiDetail = () => {
  const { md5 } = useParams();
  const navigate = useNavigate();
  const { user, logout } = useAuth();
  const [wiki, setWiki] = useState(null);
  const [backlinks, setBacklinks] = useState([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);

  useEffect(() => { if (user && md5) { loadWikiData(); } }, [md5, user]);

  const loadWikiData = async () => {
    setLoading(true); setError(null);
    try {
      const [wikiData, blData] = await Promise.all([fetchWiki(md5, user), fetchBacklinks(md5, user)]);
      setWiki(wikiData); setBacklinks(blData || []);
    } catch (e) {
      if (e.tokenExpired) { message.error('登录已过期'); logout(); return; }
      setError(e.message);
    } finally { setLoading(false); }
  };

  if (loading) return <div style={{ textAlign: 'center', padding: 80 }}><Spin size="large" /></div>;
  if (error) return <Empty description={error} />;
  if (!wiki) return <Empty description="Wiki 页面不存在" />;

  const parseTags = (s) => { try { return JSON.parse(s); } catch { return []; } };
  const parseOutline = (s) => { try { return JSON.parse(s); } catch { return []; } };

  const tags = parseTags(wiki.tags);
  const outline = parseOutline(wiki.outline);
  const links = wiki.links || [];

  return (
    <div>
      <PageHeader>
        <Button icon={<ArrowLeftOutlined />} onClick={() => navigate(-1)}>返回</Button>
        <Button icon={<FileOutlined />} onClick={() => navigate('/files')}>源文件</Button>
      </PageHeader>

      <StyledCard>
        <Title level={3} style={{ margin: '0 0 12px', fontWeight: 700 }}>{wiki.title || '未命名 Wiki'}</Title>
        <Descriptions column={1} size="small" style={{ marginBottom: 16 }} labelStyle={{ color: '#94A3B8', fontWeight: 500 }}>
          <Descriptions.Item label="源文件">{wiki.source?.filename || '-'}</Descriptions.Item>
          <Descriptions.Item label="类型">{(wiki.source?.type || '').toUpperCase()}</Descriptions.Item>
        </Descriptions>

        {tags.length > 0 && (
          <div style={{ marginBottom: 16 }}>
            {tags.map((t, i) => <Tag key={i} color="blue" style={{ borderRadius: 6 }}>{t}</Tag>)}
          </div>
        )}

        {wiki.summary && (
          <InnerCard title="摘要" size="small">
            <Paragraph style={{ margin: 0, fontSize: 14, lineHeight: 1.7, color: '#334155' }}>{wiki.summary}</Paragraph>
          </InnerCard>
        )}

        {outline.length > 0 && (
          <InnerCard title="大纲" size="small">
            <List size="small" dataSource={outline} renderItem={item => <List.Item style={{ padding: '6px 0', fontSize: 13.5 }}>{item}</List.Item>} />
          </InnerCard>
        )}

        {links.length > 0 && (
          <InnerCard title="概念链接" size="small">
            {links.map((link, i) => <Tag key={i} icon={<LinkOutlined />} color="green" style={{ borderRadius: 6, marginBottom: 4 }}>{link}</Tag>)}
          </InnerCard>
        )}
      </StyledCard>

      {backlinks.length > 0 && (
        <StyledCard title="反向链接">
          {backlinks.map((item, i) => (
            <InnerCard key={i} size="small">
              <Text strong style={{ fontSize: 13.5 }}>{item.concept}</Text>
              <List size="small" style={{ marginTop: 8 }}
                dataSource={item.referenced_by || []}
                renderItem={ref => (
                  <List.Item style={{ padding: '6px 0' }}>
                    <FileOutlined style={{ marginRight: 8, color: '#94A3B8' }} />
                    <Button type="link" style={{ padding: 0, fontSize: 13.5 }} onClick={() => navigate(`/wiki/${ref.md5}`)}>
                      {ref.filename}
                    </Button>
                  </List.Item>
                )} />
            </InnerCard>
          ))}
        </StyledCard>
      )}

      {backlinks.length === 0 && (
        <StyledCard><Empty description="暂无反向链接" /></StyledCard>
      )}
    </div>
  );
};

export default WikiDetail;
