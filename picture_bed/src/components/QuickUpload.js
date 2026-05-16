import React from 'react';
import styled from '@emotion/styled';
import { Upload, Progress, message } from 'antd';
import { CloudUploadOutlined, ThunderboltOutlined } from '@ant-design/icons';
import { uploadImage } from '../services/images';
import { describeFile } from '../services/ai';
import { useAuth } from '../contexts/AuthContext';

const Drop = styled.div`
  border: 1.5px dashed ${p => p.theme.colors.border};
  border-radius: 12px;
  background: ${p => p.theme.colors.panel2};
  padding: 22px;
  display: flex; align-items: center; gap: 18px;
  transition: all ${p => p.theme.duration.base} ${p => p.theme.ease.out};
  cursor: pointer;

  &:hover {
    border-color: ${p => p.theme.colors.accentBorder};
    background: ${p => p.theme.colors.accentSoft};
  }

  .icon-wrap {
    width: 48px; height: 48px;
    display: grid; place-items: center;
    border-radius: 12px;
    background: ${p => p.theme.colors.accentSoft};
    color: ${p => p.theme.colors.accent};
    font-size: 22px;
    flex-shrink: 0;
  }
  .meta {
    flex: 1; min-width: 0;
  }
  .title {
    color: ${p => p.theme.colors.text};
    font-size: 14px; font-weight: 600;
    display: flex; align-items: center; gap: 8px;
  }
  .desc {
    color: ${p => p.theme.colors.text2};
    font-size: 12.5px; margin-top: 3px;
  }
  .badge {
    display: inline-flex; align-items: center; gap: 4px;
    padding: 2px 7px;
    border-radius: 999px;
    background: ${p => p.theme.colors.bg};
    color: ${p => p.theme.colors.text2};
    font-size: 10.5px;
    font-family: ${p => p.theme.fontFamily.mono};
    border: 1px solid ${p => p.theme.colors.border};
  }
`;

const ProgressWrap = styled.div`
  margin-top: 10px;
  .ant-progress-bg { transition: width 200ms ease; }
`;

const QuickUpload = ({ onDone }) => {
  const { user, logout } = useAuth();
  const [uploading, setUploading] = React.useState(false);
  const [pct, setPct] = React.useState(0);
  const lockRef = React.useRef(false);

  const handle = async (file) => {
    if (lockRef.current) return false;
    lockRef.current = true;
    try {
      setUploading(true); setPct(0);
      const result = await uploadImage(file, user, (p) => setPct(p));
      if (result.alreadyExists) message.warning('文件已存在');
      else if (result.instant) message.success('秒传成功');
      else message.success('上传成功');
      // 触发后台 AI 分析；若 API Key 失效会通过 services/ai.js 全局弹窗提示
      describeFile(file, user).catch(() => {});
      onDone && onDone();
    } catch (e) {
      if (e.tokenExpired) { message.error('登录已过期'); logout(); return false; }
      message.error('上传失败');
    } finally {
      lockRef.current = false;
      setUploading(false); setPct(0);
    }
    return false;
  };

  return (
    <div>
      <Upload.Dragger
        showUploadList={false}
        beforeUpload={handle}
        disabled={uploading}
        style={{ background: 'transparent', border: 'none', padding: 0 }}
      >
        <Drop>
          <div className="icon-wrap"><CloudUploadOutlined /></div>
          <div className="meta">
            <div className="title">
              拖拽文件到这里，或点击上传
              <span className="badge"><ThunderboltOutlined style={{ fontSize: 10 }} /> 秒传</span>
              <span className="badge">分片</span>
              <span className="badge">AI 分析</span>
            </div>
            <div className="desc">支持任意类型，大文件 &gt; 10MB 自动切分上传，相同文件秒传到云。</div>
          </div>
        </Drop>
      </Upload.Dragger>
      {uploading && (
        <ProgressWrap>
          <Progress percent={pct} size="small" status="active" showInfo />
        </ProgressWrap>
      )}
    </div>
  );
};

export default QuickUpload;
