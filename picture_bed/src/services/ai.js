import { API_CONFIG } from '../config';
import SparkMD5 from 'spark-md5';

const AI_ENDPOINT = `${API_CONFIG.BASE_URL}/api/ai`;

/**
 * AI 服务前端 SDK
 *
 * 说明：API Key 已迁移到后端 cfg.json，前端不再保存/同步任何 key。
 * 所有 AI 请求由后端按用户 token 鉴权后用全局 key 执行。
 */

// 计算文件 MD5（保留给可能的本地秒传场景）
const calculateMD5 = (file) => {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = (e) => {
      const spark = new SparkMD5.ArrayBuffer();
      spark.append(e.target.result);
      resolve(spark.end());
    };
    reader.onerror = reject;
    reader.readAsArrayBuffer(file);
  });
};

const postJson = async (cmd, body, { silentTokenError = false } = {}) => {
  const response = await fetch(`${AI_ENDPOINT}?cmd=${cmd}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  const data = await response.json();
  if (data.code === 4) {
    const err = new Error('token expired');
    err.tokenExpired = true;
    if (!silentTokenError) throw err;
    return null;
  }
  if (data.code !== 0) {
    throw new Error(data.msg || `${cmd} 失败`);
  }
  return data;
};

/**
 * 上传后异步触发 AI 描述（后端 worker 也会自动入队，此处可作冗余触发；失败不影响主流程）
 */
export const describeFile = async (file, user) => {
  try {
    const md5 = await calculateMD5(file);
    const ext = (file.name.split('.').pop() || '').toLowerCase();
    return await postJson('describe', {
      user: user.username,
      token: user.token,
      md5,
      filename: file.name,
      type: ext,
    });
  } catch (error) {
    console.warn('AI describe error (non-blocking):', error);
    return null;
  }
};

/**
 * 对已有文件重新生成 AI 描述（通过 md5）
 */
export const describeFileByMd5 = async (md5, filename, type, user, _unused, skipRebuild = false) => {
  const body = {
    user: user.username,
    token: user.token,
    md5,
    filename,
    type,
    force: true,
  };
  if (skipRebuild) body.skip_rebuild = true;
  return postJson('describe', body);
};

/**
 * AI 语义搜索
 */
export const aiSearch = async (query, user) => {
  const data = await postJson('search', {
    user: user.username,
    token: user.token,
    query,
  });
  if (data.files) {
    data.files = data.files.map((f) => ({
      ...f,
      url: f.url ? f.url.replace(API_CONFIG.STORAGE_URL, API_CONFIG.BASE_URL) : '',
    }));
  }
  return data;
};

/**
 * 重建 FAISS 索引
 */
export const rebuildIndex = async (user) =>
  postJson('rebuild', { user: user.username, token: user.token });

/** 获取文件知识卡片 */
export const fetchFileCard = async (md5, user) => {
  const d = await postJson('file_card', { user: user.username, token: user.token, md5 });
  return d.data;
};

/** 获取文件 Wiki 页面 */
export const fetchWiki = async (md5, user) => {
  const d = await postJson('wiki', { user: user.username, token: user.token, md5 });
  return d.data;
};

/** 获取反向链接（含显式 + 隐式自动链接） */
export const fetchBacklinks = async (md5, user) => {
  const d = await postJson('backlinks', { user: user.username, token: user.token, md5 });
  return d.data;
};

/** 获取相关文件推荐 */
export const fetchRelated = async (md5, user) => {
  const d = await postJson('related', { user: user.username, token: user.token, md5 });
  return d.data;
};
