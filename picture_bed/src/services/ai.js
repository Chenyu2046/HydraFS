import { API_CONFIG } from '../config';
import SparkMD5 from 'spark-md5';

const AI_ENDPOINT = `${API_CONFIG.BASE_URL}/api/ai`;
const API_KEY_STORAGE_PREFIX = 'dashscope_api_key_';

const getStorageKey = (user) => `${API_KEY_STORAGE_PREFIX}${user.username}`;

/* ============ API Key 错误统一处理 ============
 * - 后端返回 code=2 表示 DashScope API Key 无效 / 欠费 / 鉴权失败
 * - services 层抛出一个带 apiKeyInvalid 标志的 Error，调用方可统一弹窗
 * - 通过一个外部注册的 listener 在 App 顶层做全局节流提示，避免每次调用都弹
 */
let _apiKeyInvalidListener = null;
let _lastNotifyAt = 0;

export const setApiKeyInvalidListener = (fn) => { _apiKeyInvalidListener = fn; };

const notifyApiKeyInvalid = (info) => {
  const now = Date.now();
  if (now - _lastNotifyAt < 5000) return; // 5s 内只弹一次
  _lastNotifyAt = now;
  if (typeof _apiKeyInvalidListener === 'function') {
    try { _apiKeyInvalidListener(info); } catch {}
  }
};

const makeApiKeyError = (data) => {
  const msg = data?.msg || 'DashScope API Key 无效或欠费';
  const err = new Error(msg);
  err.apiKeyInvalid = true;
  err.errCode = data?.err_code || '';
  notifyApiKeyInvalid({ msg, errCode: err.errCode });
  return err;
};

const checkResponseCode = (data) => {
  if (!data) return;
  if (data.code === 4) {
    const err = new Error('token expired');
    err.tokenExpired = true;
    throw err;
  }
  if (data.code === 2) {
    throw makeApiKeyError(data);
  }
};

const syncApiKeyToServer = async (key, user) => {
  const response = await fetch(`${AI_ENDPOINT}?cmd=set_apikey`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      user: user.username,
      token: user.token,
      api_key: key || ''
    })
  });

  const data = await response.json();
  if (data.code === 4) {
    const err = new Error('token expired');
    err.tokenExpired = true;
    throw err;
  }
  if (data.code !== 0) {
    throw new Error(data.msg || '保存 API Key 失败');
  }
  return data;
};

const fetchApiKeyFromServer = async (user) => {
  const response = await fetch(`${AI_ENDPOINT}?cmd=get_apikey`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      user: user.username,
      token: user.token
    })
  });

  const data = await response.json();
  if (data.code === 4) {
    const err = new Error('token expired');
    err.tokenExpired = true;
    throw err;
  }
  if (data.code !== 0) {
    throw new Error(data.msg || '获取 API Key 失败');
  }
  return data.data?.api_key || '';
};

const resolveApiKey = async (user, explicitKey) => {
  if (explicitKey && explicitKey.trim()) {
    return explicitKey.trim();
  }
  return fetchApiKey(user);
};

/**
 * 优先从服务端读取 API Key；若服务端为空，则回退本地缓存并尝试回写服务端
 */
export const fetchApiKey = async (user) => {
  if (!user || !user.username) return '';
  const storageKey = getStorageKey(user);
  const localKey = localStorage.getItem(storageKey) || '';

  const serverKey = await fetchApiKeyFromServer(user);
  if (serverKey) {
    localStorage.setItem(storageKey, serverKey);
    return serverKey;
  }
  if (!localKey) return '';

  await syncApiKeyToServer(localKey, user);
  localStorage.setItem(storageKey, localKey);
  return localKey;
};

/**
 * 保存 API Key 到服务端，并同步浏览器本地缓存
 */
export const saveApiKey = async (key, user) => {
  if (!user || !user.username) {
    throw new Error('用户信息无效');
  }
  const storageKey = getStorageKey(user);
  await syncApiKeyToServer(key, user);
  if (key) {
    localStorage.setItem(storageKey, key);
  } else {
    localStorage.removeItem(storageKey);
  }
  return { code: 0, msg: 'ok' };
};

// 计算文件 MD5（与 images.js 中一致）
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

/**
 * 上传后异步调用 AI 生成文件描述 + 向量
 * 失败不影响上传流程，但 API Key 错误会通过全局 listener 弹窗
 */
export const describeFile = async (file, user, apiKey) => {
  try {
    const md5 = await calculateMD5(file);
    const ext = file.name.split('.').pop() || '';
    const resolvedApiKey = await resolveApiKey(user, apiKey);

    const body = {
      user: user.username,
      token: user.token,
      md5: md5,
      filename: file.name,
      type: ext.toLowerCase()
    };
    if (resolvedApiKey) body.api_key = resolvedApiKey;

    const response = await fetch(`${AI_ENDPOINT}?cmd=describe`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body)
    });

    const data = await response.json();
    if (data.code === 2) {
      // 触发全局弹窗，但不向上抛（不影响上传成功提示）
      makeApiKeyError(data);
      return data;
    }
    if (data.code === 0) {
      console.log('AI describe success:', file.name);
    } else {
      console.warn('AI describe failed:', data.msg);
    }
    return data;
  } catch (error) {
    console.warn('AI describe error (non-blocking):', error);
    return null;
  }
};

/**
 * 对已有文件重新生成 AI 描述（通过 md5）
 */
export const describeFileByMd5 = async (md5, filename, type, user, apiKey, skipRebuild = false) => {
  const resolvedApiKey = await resolveApiKey(user, apiKey);
  const body = {
    user: user.username,
    token: user.token,
    md5: md5,
    filename: filename,
    type: type,
    force: true
  };
  if (skipRebuild) body.skip_rebuild = true;
  if (resolvedApiKey) body.api_key = resolvedApiKey;

  const response = await fetch(`${AI_ENDPOINT}?cmd=describe`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  });

  const data = await response.json();
  checkResponseCode(data);
  if (data.code !== 0) {
    throw new Error(data.msg || '生成描述失败');
  }
  return data;
};

/**
 * AI 语义搜索
 */
export const aiSearch = async (query, user, apiKey) => {
  const resolvedApiKey = await resolveApiKey(user, apiKey);
  const body = {
    user: user.username,
    token: user.token,
    query: query
  };
  if (resolvedApiKey) body.api_key = resolvedApiKey;

  const response = await fetch(`${AI_ENDPOINT}?cmd=search`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  });

  const data = await response.json();
  checkResponseCode(data);
  if (data.code !== 0) {
    throw new Error(data.msg || '搜索失败');
  }
  if (data.files) {
    data.files = data.files.map(f => ({
      ...f,
      url: f.url ? f.url.replace(API_CONFIG.STORAGE_URL, API_CONFIG.BASE_URL) : '',
    }));
  }
  return data;
};

/**
 * 重建 FAISS 索引
 */
export const rebuildIndex = async (user) => {
  const response = await fetch(`${AI_ENDPOINT}?cmd=rebuild`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      user: user.username,
      token: user.token
    })
  });

  const data = await response.json();
  if (data.code !== 0) {
    throw new Error(data.msg || '重建索引失败');
  }
  return data;
};

/**
 * 获取文件知识卡片
 */
export const fetchFileCard = async (md5, user) => {
  const response = await fetch(`${AI_ENDPOINT}?cmd=file_card`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ user: user.username, token: user.token, md5 })
  });
  const data = await response.json();
  if (data.code === 4) { const err = new Error('token expired'); err.tokenExpired = true; throw err; }
  if (data.code !== 0) throw new Error(data.msg || '获取文件卡片失败');
  return data.data;
};

/**
 * 获取文件 Wiki 页面
 */
export const fetchWiki = async (md5, user) => {
  const response = await fetch(`${AI_ENDPOINT}?cmd=wiki`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ user: user.username, token: user.token, md5 })
  });
  const data = await response.json();
  if (data.code === 4) { const err = new Error('token expired'); err.tokenExpired = true; throw err; }
  if (data.code !== 0) throw new Error(data.msg || '获取 Wiki 失败');
  return data.data;
};

/**
 * 获取反向链接
 */
export const fetchBacklinks = async (md5, user) => {
  const response = await fetch(`${AI_ENDPOINT}?cmd=backlinks`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ user: user.username, token: user.token, md5 })
  });
  const data = await response.json();
  if (data.code === 4) { const err = new Error('token expired'); err.tokenExpired = true; throw err; }
  if (data.code !== 0) throw new Error(data.msg || '获取反向链接失败');
  return data.data;
};

/**
 * 获取相关文件推荐
 */
export const fetchRelated = async (md5, user) => {
  const response = await fetch(`${AI_ENDPOINT}?cmd=related`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ user: user.username, token: user.token, md5 })
  });
  const data = await response.json();
  if (data.code === 4) { const err = new Error('token expired'); err.tokenExpired = true; throw err; }
  if (data.code !== 0) throw new Error(data.msg || '获取相关文件失败');
  return data.data;
};
