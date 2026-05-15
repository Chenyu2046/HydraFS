import React from 'react';
import styled from '@emotion/styled';
import {
  FileOutlined, FilePdfOutlined, FileWordOutlined, FileExcelOutlined,
  FileZipOutlined, FileTextOutlined, FileImageOutlined, FilePptOutlined,
  CodeOutlined, PlaySquareOutlined, CustomerServiceOutlined,
} from '@ant-design/icons';
import { Tag } from 'antd';
import { classifyFileType } from '../mock/graph';

const Grid = styled.div`
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
  gap: 14px;
`;

const Card = styled.div`
  background: ${p => p.theme.colors.panel};
  border: 1px solid ${p => p.theme.colors.border};
  border-radius: 12px;
  overflow: hidden;
  cursor: pointer;
  transition: all ${p => p.theme.duration.base} ${p => p.theme.ease.out};
  display: flex; flex-direction: column;

  &:hover {
    border-color: ${p => p.theme.colors.borderStrong};
    transform: translateY(-2px);
    box-shadow: ${p => p.theme.shadow.md};
  }

  .cover {
    height: 130px;
    background: ${p => p.theme.colors.panel2};
    display: grid; place-items: center;
    border-bottom: 1px solid ${p => p.theme.colors.border};
    overflow: hidden;
    position: relative;
    img { width: 100%; height: 100%; object-fit: cover; transition: transform 220ms ${p => p.theme.ease.out}; }
    .ph {
      font-size: 38px;
      color: ${p => p.color || p.theme.colors.text3};
      opacity: 0.8;
    }
  }
  &:hover .cover img { transform: scale(1.04); }

  .body {
    padding: 12px 14px 14px;
    display: flex; flex-direction: column; gap: 6px;
  }
  .name {
    color: ${p => p.theme.colors.text};
    font-size: 13px; font-weight: 600;
    white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
  }
  .meta {
    display: flex; align-items: center; justify-content: space-between;
    font-size: 11px;
    color: ${p => p.theme.colors.text3};
    font-family: ${p => p.theme.fontFamily.mono};
  }
  .badges { display: flex; gap: 4px; flex-wrap: wrap; }
`;

const TYPE_ICON = {
  pdf:  { icon: <FilePdfOutlined />,   color: '#DC2626' },
  doc:  { icon: <FileWordOutlined />,  color: '#2563EB' },
  docx: { icon: <FileWordOutlined />,  color: '#2563EB' },
  xls:  { icon: <FileExcelOutlined />, color: '#059669' },
  xlsx: { icon: <FileExcelOutlined />, color: '#059669' },
  ppt:  { icon: <FilePptOutlined />,   color: '#D97706' },
  pptx: { icon: <FilePptOutlined />,   color: '#D97706' },
  zip:  { icon: <FileZipOutlined />,   color: '#D97706' },
  rar:  { icon: <FileZipOutlined />,   color: '#D97706' },
  '7z': { icon: <FileZipOutlined />,   color: '#D97706' },
  tar:  { icon: <FileZipOutlined />,   color: '#D97706' },
  gz:   { icon: <FileZipOutlined />,   color: '#D97706' },
  txt:  { icon: <FileTextOutlined />,  color: '#64748B' },
  md:   { icon: <FileTextOutlined />,  color: '#64748B' },
  log:  { icon: <FileTextOutlined />,  color: '#64748B' },
  c:    { icon: <CodeOutlined />,      color: '#A855F7' },
  cpp:  { icon: <CodeOutlined />,      color: '#A855F7' },
  h:    { icon: <CodeOutlined />,      color: '#A855F7' },
  js:   { icon: <CodeOutlined />,      color: '#F59E0B' },
  ts:   { icon: <CodeOutlined />,      color: '#3B82F6' },
  py:   { icon: <CodeOutlined />,      color: '#22C55E' },
  go:   { icon: <CodeOutlined />,      color: '#06B6D4' },
  mp4:  { icon: <PlaySquareOutlined />,color: '#DB2777' },
  mp3:  { icon: <CustomerServiceOutlined />, color: '#0EA5E9' },
};

const isImg = (t) => t && ['png','jpg','jpeg','gif','bmp','webp','svg'].includes(String(t).toLowerCase());
const formatBytes = (b) => {
  if (!b) return '-';
  if (b < 1024) return b + 'B';
  if (b < 1024*1024) return (b/1024).toFixed(1) + 'K';
  if (b < 1024**3) return (b/1024/1024).toFixed(1) + 'M';
  return (b/1024**3).toFixed(2) + 'G';
};

const FileGrid = ({ files = [], onPick }) => (
  <Grid>
    {files.map(f => {
      const ext = (f.type || '').toLowerCase();
      const ico = TYPE_ICON[ext];
      const showImage = isImg(ext) && f.url;
      return (
        <Card key={f.md5} onClick={() => onPick && onPick(f)}>
          <div className="cover">
            {showImage
              ? <img src={f.url} alt={f.file_name || f.name} />
              : <span className="ph">{ico ? ico.icon : <FileImageOutlined />}</span>}
          </div>
          <div className="body">
            <div className="name">{f.file_name || f.name}</div>
            <div className="meta">
              <span>{(ext || classifyFileType(ext)).toUpperCase()} · {formatBytes(f.size)}</span>
              <span className="badges">
                {f.share_status === 1 && <Tag color="blue" bordered={false} style={{ margin: 0, fontSize: 10 }}>S</Tag>}
                {f.wiki_ready === 1 && <Tag color="purple" bordered={false} style={{ margin: 0, fontSize: 10 }}>W</Tag>}
              </span>
            </div>
          </div>
        </Card>
      );
    })}
  </Grid>
);

export default FileGrid;
