import { API_CONFIG } from '../config';
import { AdaptiveConcurrencyController, retryDelayMs } from './concurrency_controller.mjs';
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
      url: file.storage_mode === 'manifest' && file.object_id
        ? `${API_CONFIG.BASE_URL}${API_CONFIG.ENDPOINTS.OBJECT_DOWNLOAD}?objectId=${encodeURIComponent(file.object_id)}`
        : (file.url ? file.url.replace(API_CONFIG.STORAGE_URL, API_CONFIG.BASE_URL) : ''),
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
  MODE: 'adaptive',
  INITIAL_CONCURRENCY: 8,
  MIN_CONCURRENCY: 4,
  MAX_CONCURRENCY: 16,
  TIMEOUT_MS: 30000,
  MAX_RETRIES: 3,
  RETRY_BASE_MS: 200
};

const getChunkUploadConfig = () => ({
  ...DEFAULT_CHUNK_UPLOAD_CONFIG,
  ...(API_CONFIG.CHUNK_UPLOAD || {})
});

const validInteger = (value, fallback, minimum = 1) => {
  const parsed = Number(value);
  return Number.isSafeInteger(parsed) && parsed >= minimum ? parsed : fallback;
};

const normalizeChunkUploadConfig = config => {
  const initial = validInteger(config.INITIAL_CONCURRENCY, 8);
  return {
    ...config,
    MODE: config.MODE === 'fixed' ? 'fixed' : 'adaptive',
    INITIAL_CONCURRENCY: initial,
    MIN_CONCURRENCY: Math.min(initial, validInteger(config.MIN_CONCURRENCY, 4)),
    MAX_CONCURRENCY: Math.max(initial, validInteger(config.MAX_CONCURRENCY, 16)),
    TIMEOUT_MS: validInteger(config.TIMEOUT_MS, 30000),
    MAX_RETRIES: validInteger(config.MAX_RETRIES, 3, 0),
    RETRY_BASE_MS: validInteger(config.RETRY_BASE_MS, 200, 0)
  };
};

// V2 computes the legacy file digest and each CAS digest in one sequential scan.
const calculateObjectDigests = async (file, chunkSize) => {
  if (!window.crypto || !window.crypto.subtle) {
    throw new Error('当前浏览器不支持 SHA-256');
  }
  return new Promise((resolve, reject) => {
    const scanSize = 2 * 1024 * 1024;
    const chunks = Math.ceil(file.size / scanSize);
    const reader = new FileReader();
    const spark = new SparkMD5.ArrayBuffer();
    const partBuffers = [];
    const parts = [];
    let currentChunk = 0;
    let currentPart = 0;
    let currentPartSize = 0;

    const finishPart = async () => {
      if (currentPartSize === 0) return;
      const bytes = new Uint8Array(currentPartSize);
      let offset = 0;
      partBuffers.forEach(buffer => {
        bytes.set(new Uint8Array(buffer), offset);
        offset += buffer.byteLength;
      });
      const digest = await window.crypto.subtle.digest('SHA-256', bytes);
      const hash = Array.from(new Uint8Array(digest))
        .map(value => value.toString(16).padStart(2, '0')).join('');
      parts.push({ index: currentPart, size: currentPartSize, sha256: hash });
      currentPart++;
      currentPartSize = 0;
      partBuffers.length = 0;
    };

    reader.onerror = reject;
    reader.onload = async event => {
      try {
        const buffer = event.target.result;
        spark.append(buffer);
        partBuffers.push(buffer);
        currentPartSize += buffer.byteLength;
        currentChunk++;
        if (currentPartSize === chunkSize || currentChunk === chunks) {
          await finishPart();
        }
        if (currentChunk < chunks) {
          const start = currentChunk * scanSize;
          reader.readAsArrayBuffer(file.slice(start, Math.min(start + scanSize, file.size)));
        } else {
          resolve({ contentDigest: spark.end(), parts });
        }
      } catch (error) {
        reject(error);
      }
    };
    if (file.size === 0) {
      resolve({ contentDigest: spark.end(), parts: [] });
    } else {
      reader.readAsArrayBuffer(file.slice(0, Math.min(scanSize, file.size)));
    }
  });
};

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

const waitForRetry = (delay, abortSignal) => new Promise((resolve, reject) => {
  if (abortSignal?.aborted) {
    reject(createAbortError());
    return;
  }
  let timer;
  const onAbort = () => {
    clearTimeout(timer);
    abortSignal?.removeEventListener('abort', onAbort);
    reject(createAbortError());
  };
  timer = setTimeout(() => {
    abortSignal?.removeEventListener('abort', onAbort);
    resolve();
  }, delay);
  abortSignal?.addEventListener('abort', onAbort, { once: true });
});

const createAimdWindow = config => new AdaptiveConcurrencyController({
  mode: config.MODE,
  initial: config.INITIAL_CONCURRENCY,
  min: config.MIN_CONCURRENCY,
  max: config.MAX_CONCURRENCY
});

const uploadChunkWithRetry = async ({ file, chunkSize, md5, index, config, aimdWindow, abortSignal, uploadUrl: suppliedUrl, uploadHeaders, waitForPartStatus }) => {
  const start = index * chunkSize;
  const end = Math.min(start + chunkSize, file.size);
  const chunk = file.slice(start, end);
  const uploadUrl = suppliedUrl || `${API_CONFIG.BASE_URL}${API_CONFIG.ENDPOINTS.CHUNK_UPLOAD}?md5=${encodeURIComponent(md5)}&index=${index}`;
  let lastError;
  let partBusyPolls = 0;

  for (let attempt = 0; attempt <= config.MAX_RETRIES; attempt++) {
    if (abortSignal && abortSignal.aborted) {
      throw createAbortError();
    }

    const startedAt = Date.now();
    const timeout = createTimeoutController(config.TIMEOUT_MS, abortSignal);

    try {
      const uploadRes = await fetch(uploadUrl, {
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream', ...(uploadHeaders || {}) },
        body: chunk,
        signal: timeout.signal
      });

      const uploadData = await uploadRes.json();
      const rtt = Date.now() - startedAt;
      if (uploadRes.ok && uploadData.code === 0) {
        aimdWindow.record({ success: true, rtt, status: uploadRes.status });
        return;
      }

      if (uploadData.code === 2 && waitForPartStatus) {
        aimdWindow.record({ success: false, rtt, status: uploadRes.status });
        if (++partBusyPolls > config.MAX_RETRIES + 1) {
          lastError = new Error(`分片 ${index} 等待其他上传者超时`);
          break;
        }
        let partState;
        try {
          partState = await waitForPartStatus(index, abortSignal, config);
        } catch (error) {
          error.partBusyFailure = true;
          throw error;
        }
        if (partState === 'ready') return;
        if (partState === 'uploadable') {
          attempt--;
          continue;
        }
        lastError = new Error(`分片 ${index} 状态不可上传`);
        break;
      }

      lastError = new Error(`分片 ${index} 上传失败`);
      aimdWindow.record({ success: false, rtt, status: uploadRes.status, timeout: uploadRes.status === 408 });
      const retryable = uploadRes.status === 408 || uploadRes.status === 429 || uploadRes.status >= 500;
      if (retryable && attempt < config.MAX_RETRIES) {
        const delay = retryDelayMs({ attempt, retryAfter: uploadRes.headers?.get?.('retry-after'), baseMs: config.RETRY_BASE_MS });
        if (delay > 0) await waitForRetry(delay, abortSignal);
      } else if (!retryable) {
        break;
      }
    } catch (error) {
      const rtt = Date.now() - startedAt;
      lastError = error;
      if (error.partBusyFailure) throw error;
      aimdWindow.record({ success: false, rtt, timeout: timeout.timedOut });

      if (abortSignal && abortSignal.aborted && !timeout.timedOut) {
        throw error;
      }
      if (attempt < config.MAX_RETRIES) {
        const delay = retryDelayMs({ attempt, baseMs: config.RETRY_BASE_MS });
        if (delay > 0) await waitForRetry(delay, abortSignal);
      }
    } finally {
      timeout.clear();
    }
  }

  throw lastError || new Error(`分片 ${index} 上传失败`);
};

const uploadChunksWithAimd = async ({ file, md5, chunkSize, chunkCount, uploadedSet, pendingIndices, onProgress, uploadUrlForIndex, uploadHeaders, waitForPartStatus }) => {
  const config = getChunkUploadConfig();
  const aimdWindow = createAimdWindow(config);
  const pending = [];

  if (pendingIndices) {
    pending.push(...pendingIndices);
  } else {
    for (let i = 0; i < chunkCount; i++) {
      if (!uploadedSet.has(i)) pending.push(i);
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
        abortSignal: uploadController.signal,
        uploadUrl: uploadUrlForIndex ? uploadUrlForIndex(chunkIndex) : undefined,
        uploadHeaders,
        waitForPartStatus
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

const objectRequest = async (endpoint, user, body) => {
  const config = normalizeChunkUploadConfig(getChunkUploadConfig());
  for (let attempt = 0; attempt <= config.MAX_RETRIES; attempt++) {
    let response;
    let data = {};
    try {
      const timeout = createTimeoutController(config.TIMEOUT_MS);
      try {
        response = await fetch(`${API_CONFIG.BASE_URL}${endpoint}`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ ...body, user: user.username, token: user.token }),
          signal: timeout.signal
        });
        data = await response.json().catch(() => ({}));
      } finally {
        timeout.clear();
      }
    } catch (error) {
      if (attempt === config.MAX_RETRIES) throw error;
      const delay = retryDelayMs({ attempt, baseMs: config.RETRY_BASE_MS });
      if (delay > 0) await new Promise(resolve => setTimeout(resolve, delay));
      continue;
    }
    if (data.code === 4) throw makeTokenExpiredError();
    if (response.ok && data.code === 0) return data;
    const retryable = response.status === 429 || response.status === 503;
    if (!retryable || attempt === config.MAX_RETRIES) {
      throw new Error(data.msg || '对象存储请求失败');
    }
    const delay = retryDelayMs({ attempt, retryAfter: response.headers?.get?.('retry-after'),
      baseMs: config.RETRY_BASE_MS });
    if (delay > 0) await new Promise(resolve => setTimeout(resolve, delay));
  }
  throw new Error('对象存储请求失败');
};

const waitForObjectPartStatus = async ({ uploadId, user, index, config, abortSignal }) => {
  const maxPolls = config.MAX_RETRIES + 1;
  for (let poll = 0; poll < maxPolls; poll++) {
    let status;
    try {
      status = await objectRequest(API_CONFIG.ENDPOINTS.OBJECT_STATUS, user, { uploadId });
    } catch (error) {
      error.partBusyFailure = true;
      throw error;
    }
    if ((status.reusedParts || []).includes(index)) return 'ready';
    if ((status.uploadableParts || []).includes(index) || (status.missingParts || []).includes(index)) {
      return 'uploadable';
    }
    if (!(status.waitingParts || []).includes(index)) {
      const error = new Error(`分片 ${index} 状态不可上传`);
      error.partBusyFailure = true;
      throw error;
    }
    if (poll + 1 < maxPolls) await waitForRetry(config.RETRY_BASE_MS, abortSignal);
  }
  const error = new Error(`分片 ${index} 等待其他上传者超时`);
  error.partBusyFailure = true;
  throw error;
};

const objectUploadableParts = status => (
  Array.isArray(status.uploadableParts) && status.uploadableParts.length > 0
    ? status.uploadableParts
    : (status.missingParts || [])
);

const objectPartsReady = (status, count) => (
  Array.isArray(status.reusedParts)
    ? status.reusedParts.length === count
    : objectUploadableParts(status).length === 0 && (status.missingParts || []).length === 0
);

export const uploadObject = async (file, user, onProgress) => {
  const chunkSize = API_CONFIG.CHUNK_SIZE;
  const { contentDigest, parts } = await calculateObjectDigests(file, chunkSize);
  const init = await objectRequest(API_CONFIG.ENDPOINTS.OBJECT_INIT, user, {
    filename: file.name, md5: contentDigest, contentDigest, size: file.size, parts
  });
  if (onProgress) onProgress(0);
  if (init.instant) {
    if (onProgress) onProgress(100);
    return { ...init, instant: true, alreadyExists: true, md5: contentDigest };
  }

  const allParts = Array.from({ length: parts.length }, (_, index) => index);
  let status = init;
  for (let attempt = 0; attempt < 60; attempt++) {
    const uploadable = new Set(objectUploadableParts(status));
    if (uploadable.size > 0) {
      const uploadedSet = new Set(allParts.filter(index => !uploadable.has(index)));
      await uploadChunksWithAimd({
        file, md5: contentDigest, chunkSize, chunkCount: parts.length, uploadedSet,
        pendingIndices: [...uploadable], onProgress,
        uploadHeaders: { 'X-Upload-User': user.username, 'X-Upload-Token': user.token },
        waitForPartStatus: (index, abortSignal, uploadConfig) => waitForObjectPartStatus({
          uploadId: init.uploadId, user, index, config: uploadConfig, abortSignal
        }),
        uploadUrlForIndex: index => `${API_CONFIG.BASE_URL}${API_CONFIG.ENDPOINTS.OBJECT_PART}`
          + `?uploadId=${encodeURIComponent(init.uploadId)}&index=${index}&sha256=${parts[index].sha256}`
      });
    }
    status = await objectRequest(API_CONFIG.ENDPOINTS.OBJECT_STATUS, user, { uploadId: init.uploadId });
    const waiting = status.waitingParts || [];
    const remaining = objectUploadableParts(status);
    if (waiting.length === 0 && remaining.length === 0 && objectPartsReady(status, parts.length)) break;
    if (waiting.length > 0 && remaining.length === 0) {
      await new Promise(resolve => setTimeout(resolve, 500));
    }
  }
  const finalStatus = status;
  if ((finalStatus.waitingParts || []).length > 0 || objectUploadableParts(finalStatus).length > 0 ||
      !objectPartsReady(finalStatus, parts.length)) {
    throw new Error('对象分片尚未全部就绪');
  }
  const committed = await objectRequest(API_CONFIG.ENDPOINTS.OBJECT_COMMIT, user, { uploadId: init.uploadId });
  if (onProgress) onProgress(100);
  return { ...committed, instant: false, alreadyExists: false, md5: contentDigest };
};

// 普通上传（小文件 <= 10MB）
export const uploadImage = async (file, user, onProgress) => {
  // 大文件自动走分片上传
  if (file.size > API_CONFIG.CHUNK_THRESHOLD) {
    return uploadObject(file, user, onProgress);
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
  if (image.storage_mode === 'manifest' && image.object_id) {
    return objectRequest(API_CONFIG.ENDPOINTS.OBJECT_DELETE, user, { objectId: image.object_id });
  }
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

export const downloadImage = async (image, user) => {
  if (image.storage_mode === 'manifest' && image.object_id) {
    const response = await fetch(`${API_CONFIG.BASE_URL}${API_CONFIG.ENDPOINTS.OBJECT_DOWNLOAD}`
      + `?objectId=${encodeURIComponent(image.object_id)}`, {
      headers: { 'X-Upload-User': user.username, 'X-Upload-Token': user.token }
    });
    if (!response.ok) throw new Error('文件下载失败');
    if ((response.headers.get('content-type') || '').includes('application/json')) {
      const data = await response.json();
      throw new Error(data.msg || '文件下载失败');
    }
    const url = URL.createObjectURL(await response.blob());
    const link = document.createElement('a');
    link.href = url;
    link.download = image.file_name || image.name || 'download';
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    URL.revokeObjectURL(url);
    return;
  }
  const link = document.createElement('a');
  link.href = image.url;
  link.download = image.file_name || image.name || 'download';
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
};
