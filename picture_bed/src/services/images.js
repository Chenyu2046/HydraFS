import { API_CONFIG } from '../config';
import SparkMD5 from 'spark-md5';

/**
 * 获取当前用户文件列表
 * @param {object} user
 * @param {object} [opts]
 * @param {number} [opts.count=20]  分页大小（FileList=20 / Home=12 / Knowledge=Graph=200）
 * @param {number} [opts.start=0]   起始偏移
 * @param {'normal'|'pvasc'|'pvdesc'} [opts.cmd='normal']
 */
export const fetchUserImages = async (user, opts = {}) => {
  const { count = 20, start = 0, cmd = 'normal' } = opts;
  const response = await fetch(`${API_CONFIG.BASE_URL}${API_CONFIG.ENDPOINTS.MY_FILES}?cmd=${cmd}`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({
      token: user.token,
      user: user.username,
      count,
      start
    })
  });

  const data = await response.json();
  if (data.code === 0) {
    return (data.files || []).map(file => ({
      ...file,
      name: file.file_name || file.filename,
      url: file.url ? file.url.replace(API_CONFIG.STORAGE_URL, API_CONFIG.BASE_URL) : '',
      pv: file.pv || 0,
    }));
  }
  if (data.code === 1) {
    const err = new Error('token验证失败');
    err.tokenExpired = true;
    throw err;
  }
  throw new Error(data.msg || '获取图片列表失败');
};

// 计算文件MD5（分片读取，支持大文件）
const calculateMD5 = (file) => {
  return new Promise((resolve, reject) => {
    const chunkSize = 2 * 1024 * 1024; // 2MB chunks for MD5 calculation
    const chunks = Math.ceil(file.size / chunkSize);
    let currentChunk = 0;
    const spark = new SparkMD5.ArrayBuffer();
    const reader = new FileReader();

    reader.onload = (e) => {
      spark.append(e.target.result);
      currentChunk++;
      if (currentChunk < chunks) {
        loadNext();
      } else {
        resolve(spark.end());
      }
    };
    reader.onerror = reject;

    function loadNext() {
      const start = currentChunk * chunkSize;
      const end = Math.min(start + chunkSize, file.size);
      reader.readAsArrayBuffer(file.slice(start, end));
    }

    loadNext();
  });
};

const makeTokenExpiredError = () => {
  const err = new Error('token验证失败');
  err.tokenExpired = true;
  return err;
};

const tryInstantUpload = async (file, user, md5) => {
  const response = await fetch(`${API_CONFIG.BASE_URL}${API_CONFIG.ENDPOINTS.MD5}`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({
      user: user.username,
      token: user.token,
      md5,
      fileName: file.name
    })
  });

  const data = await response.json();
  if (data.code === 4) {
    throw makeTokenExpiredError();
  }
  if (data.code === 0) {
    return { instant: true, alreadyExists: false, md5 };
  }
  if (data.code === 5) {
    return { instant: true, alreadyExists: true, md5 };
  }
  if (data.code === 1) {
    return { instant: false, alreadyExists: false, md5 };
  }

  throw new Error(data.msg || '秒传检测失败');
};

const DEFAULT_CHUNK_UPLOAD_CONFIG = {
  INITIAL_CONCURRENCY: 4,
  MIN_CONCURRENCY: 4,
  MAX_CONCURRENCY: 32,
  TIMEOUT_MS: 30000,
  MAX_RETRIES: 3
};

const getChunkUploadConfig = () => ({
  ...DEFAULT_CHUNK_UPLOAD_CONFIG,
  ...(API_CONFIG.CHUNK_UPLOAD || {})
});

const clamp = (value, min, max) => Math.max(min, Math.min(max, value));

const createAbortError = () => {
  const error = new Error('Upload canceled');
  error.name = 'AbortError';
  return error;
};

const createTimeoutController = (timeoutMs, parentSignal) => {
  const controller = new AbortController();
  let timedOut = false;
  const abortFromParent = () => controller.abort();
  const timeoutId = setTimeout(() => {
    timedOut = true;
    controller.abort();
  }, timeoutMs);

  if (parentSignal) {
    if (parentSignal.aborted) {
      abortFromParent();
    } else {
      parentSignal.addEventListener('abort', abortFromParent, { once: true });
    }
  }

  return {
    signal: controller.signal,
    get timedOut() {
      return timedOut;
    },
    clear: () => {
      clearTimeout(timeoutId);
      if (parentSignal) {
        parentSignal.removeEventListener('abort', abortFromParent);
      }
    }
  };
};

const createAimdWindow = (config) => {
  const state = {
    size: clamp(config.INITIAL_CONCURRENCY, config.MIN_CONCURRENCY, config.MAX_CONCURRENCY),
    samples: []
  };

  const recordSample = (sample) => {
    state.samples.push(sample);
    if (state.samples.length > 16) {
      state.samples.shift();
    }
  };

  const decrease = () => {
    state.size = clamp(Math.floor(state.size / 2), config.MIN_CONCURRENCY, config.MAX_CONCURRENCY);
  };

  const tune = (sample) => {
    recordSample(sample);

    const recent = state.samples;
    const successSamples = recent.filter(item => item.success);
    const failureRate = recent.filter(item => !item.success).length / recent.length;
    const timeoutRate = recent.filter(item => item.timeout).length / recent.length;
    const avgSuccessRtt = successSamples.length > 0
      ? successSamples.reduce((sum, item) => sum + item.rtt, 0) / successSamples.length
      : sample.rtt;
    const degraded = failureRate > 0.2 || timeoutRate > 0.1 || sample.rtt > avgSuccessRtt * 2;

    if (!sample.success || sample.timeout || degraded) {
      decrease();
      return;
    }

    const healthy = failureRate === 0 && timeoutRate === 0 && sample.rtt <= avgSuccessRtt * 1.5;
    if (healthy) {
      state.size = clamp(state.size + 1, config.MIN_CONCURRENCY, config.MAX_CONCURRENCY);
    }
  };

  return {
    get size() {
      return state.size;
    },
    recordSuccess(rtt) {
      tune({ success: true, timeout: false, rtt });
    },
    recordFailure(rtt, timeout) {
      tune({ success: false, timeout, rtt });
    }
  };
};

const uploadChunkWithRetry = async ({ file, chunkSize, md5, index, config, aimdWindow, abortSignal }) => {
  const start = index * chunkSize;
  const end = Math.min(start + chunkSize, file.size);
  const chunk = file.slice(start, end);
  const uploadUrl = `${API_CONFIG.BASE_URL}${API_CONFIG.ENDPOINTS.CHUNK_UPLOAD}?md5=${encodeURIComponent(md5)}&index=${index}`;
  let lastError;

  for (let attempt = 0; attempt <= config.MAX_RETRIES; attempt++) {
    if (abortSignal && abortSignal.aborted) {
      throw createAbortError();
    }

    const startedAt = Date.now();
    const timeout = createTimeoutController(config.TIMEOUT_MS, abortSignal);

    try {
      const uploadRes = await fetch(uploadUrl, {
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream' },
        body: chunk,
        signal: timeout.signal
      });

      const uploadData = await uploadRes.json();
      const rtt = Date.now() - startedAt;
      if (uploadData.code === 0) {
        aimdWindow.recordSuccess(rtt);
        return;
      }

      lastError = new Error(`分片 ${index} 上传失败`);
      aimdWindow.recordFailure(rtt, false);
    } catch (error) {
      const rtt = Date.now() - startedAt;
      lastError = error;
      aimdWindow.recordFailure(rtt, timeout.timedOut);

      if (abortSignal && abortSignal.aborted && !timeout.timedOut) {
        throw error;
      }
    } finally {
      timeout.clear();
    }
  }

  throw lastError || new Error(`分片 ${index} 上传失败`);
};

const uploadChunksWithAimd = async ({ file, md5, chunkSize, chunkCount, uploadedSet, onProgress }) => {
  const config = getChunkUploadConfig();
  const aimdWindow = createAimdWindow(config);
  const pending = [];

  for (let i = 0; i < chunkCount; i++) {
    if (!uploadedSet.has(i)) {
      pending.push(i);
    }
  }

  let nextIndex = 0;
  let inFlight = 0;
  let completedChunks = uploadedSet.size;

  return new Promise((resolve, reject) => {
    let settled = false;
    const uploadController = new AbortController();

    const rejectAndAbort = (error) => {
      if (settled) {
        return;
      }

      settled = true;
      uploadController.abort();
      reject(error);
    };

    const finishChunk = () => {
      if (settled) {
        return;
      }

      completedChunks++;
      if (onProgress) {
        // 分片上传占 90%，合并占 10%
        onProgress(Math.round((completedChunks / chunkCount) * 90));
      }
    };

    const startChunkUpload = (chunkIndex) => {
      inFlight++;

      uploadChunkWithRetry({
        file,
        chunkSize,
        md5,
        index: chunkIndex,
        config,
        aimdWindow,
        abortSignal: uploadController.signal
      })
        .then(finishChunk)
        .catch(rejectAndAbort)
        .finally(() => {
          inFlight--;
          if (!settled) {
            launchNext();
          }
        });
    };

    const launchNext = () => {
      if (settled) {
        return;
      }

      if (nextIndex >= pending.length && inFlight === 0) {
        settled = true;
        resolve();
        return;
      }

      while (inFlight < aimdWindow.size && nextIndex < pending.length) {
        const chunkIndex = pending[nextIndex++];
        startChunkUpload(chunkIndex);
      }
    };

    launchNext();
  });
};

// 普通上传（小文件 <= 10MB）
export const uploadImage = async (file, user, onProgress) => {
  // 大文件自动走分片上传
  if (file.size > API_CONFIG.CHUNK_THRESHOLD) {
    return uploadChunked(file, user, onProgress);
  }

  const md5 = await calculateMD5(file);
  const instantResult = await tryInstantUpload(file, user, md5);
  if (instantResult.instant) {
    if (onProgress) onProgress(100);
    return instantResult;
  }

  // FormData 字段顺序必须匹配后端 recv_save_file() 的解析顺序：
  // file 在前（含 filename），然后 user、md5、size 在后
  const formData = new FormData();
  formData.append('file', file);
  formData.append('user', user.username);
  formData.append('md5', md5);
  formData.append('size', file.size);

  const response = await fetch(`${API_CONFIG.BASE_URL}${API_CONFIG.ENDPOINTS.UPLOAD}`, {
    method: 'POST',
    body: formData
  });

  if (onProgress) onProgress(100);

  const data = await response.json();
  if (data.code === 4) {
    throw makeTokenExpiredError();
  }
  if (data.code !== 0) {
    throw new Error(data.msg || '上传失败');
  }
  return { ...data, instant: false, alreadyExists: false, md5 };
};

// 分片上传（大文件 > 10MB）
export const uploadChunked = async (file, user, onProgress) => {
  const md5 = await calculateMD5(file);
  const instantResult = await tryInstantUpload(file, user, md5);
  if (instantResult.instant) {
    if (onProgress) onProgress(100);
    return instantResult;
  }

  const chunkSize = API_CONFIG.CHUNK_SIZE;
  const chunkCount = Math.ceil(file.size / chunkSize);

  if (onProgress) onProgress(0);

  // Step 1: 初始化分片上传
  const initRes = await fetch(`${API_CONFIG.BASE_URL}${API_CONFIG.ENDPOINTS.CHUNK_INIT}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      user: user.username,
      token: user.token,
      filename: file.name,
      md5: md5,
      size: file.size,
      chunkCount: chunkCount
    })
  });

  const initData = await initRes.json();
  if (initData.code === 4) {
    throw makeTokenExpiredError();
  }
  if (initData.code !== 0) {
    throw new Error(initData.msg || '分片初始化失败');
  }

  // 获取已上传的分片索引（断点续传）
  const uploadedSet = new Set();
  const uploadedChunks = initData.uploadedChunks || initData.uploaded || '';
  if (uploadedChunks.length > 0) {
    uploadedChunks.split(',').forEach(idx => {
      const n = parseInt(idx.trim(), 10);
      if (!isNaN(n)) uploadedSet.add(n);
    });
  }

  // Step 2: AIMD 自适应并发上传分片
  await uploadChunksWithAimd({ file, md5, chunkSize, chunkCount, uploadedSet, onProgress });

  // Step 3: 请求合并
  const mergeRes = await fetch(`${API_CONFIG.BASE_URL}${API_CONFIG.ENDPOINTS.CHUNK_MERGE}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      user: user.username,
      token: user.token,
      md5: md5,
      filename: file.name
    })
  });

  const mergeData = await mergeRes.json();
  if (mergeData.code === 4) {
    throw makeTokenExpiredError();
  }
  if (mergeData.code !== 0) {
    throw new Error(mergeData.msg || '分片合并失败');
  }

  if (onProgress) onProgress(100);
  return { ...mergeData, instant: false, alreadyExists: false, md5 };
};

// 更新文件下载次数（pv+1）
export const pvFile = async (image, user) => {
  const response = await fetch(`${API_CONFIG.BASE_URL}${API_CONFIG.ENDPOINTS.DEAL_FILE}?cmd=pv`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({
      token: user.token,
      user: user.username,
      md5: image.md5,
      filename: image.file_name || image.name
    })
  });

  const data = await response.json();
  if (data.code !== 0) {
    throw new Error(data.msg || 'pv更新失败');
  }
  return data;
};

export const shareFile = async (image, user) => {
  const response = await fetch(`${API_CONFIG.BASE_URL}${API_CONFIG.ENDPOINTS.DEAL_FILE}?cmd=share`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({
      token: user.token,
      user: user.username,
      md5: image.md5,
      filename: image.file_name
    })
  });

  const data = await response.json();
  if (data.code !== 0) {
    throw new Error(data.msg || '分享失败');
  }
  return data;
};

export const cancelShareFile = async (image, user) => {
  const response = await fetch(`${API_CONFIG.BASE_URL}${API_CONFIG.ENDPOINTS.DEAL_SHARE_FILE}?cmd=cancel`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({
      user: user.username,
      md5: image.md5,
      filename: image.file_name
    })
  });

  const data = await response.json();
  if (data.code !== 0) {
    throw new Error(data.msg || '取消分享失败');
  }
  return data;
};

export const deleteImage = async (image, user) => {
  const response = await fetch(`${API_CONFIG.BASE_URL}${API_CONFIG.ENDPOINTS.DEAL_FILE}?cmd=del`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    },
    body: JSON.stringify({
      token: user.token,
      user: user.username,
      md5: image.md5,
      filename: image.file_name
    })
  });

  const data = await response.json();
  if (data.code !== 0) {
    throw new Error(data.msg || '删除失败');
  }
  return data;
};
