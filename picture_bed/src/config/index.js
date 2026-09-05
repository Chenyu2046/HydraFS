export const API_CONFIG = {
  BASE_URL: '',
  STORAGE_URL: 'http://172.30.0.3:80',
  ENDPOINTS: {
    LOGIN: '/api/login',
    REGISTER: '/api/reg',
    MY_FILES: '/api/myfiles',
    MD5: '/api/md5',
    UPLOAD: '/api/upload',
    DEAL_FILE: '/api/dealfile',
    DEAL_SHARE_FILE: '/api/dealsharefile',
    SHARE_FILES: '/api/sharefiles',
    CHUNK_INIT: '/api/chunk_init',
    CHUNK_UPLOAD: '/api/chunk_upload',
    CHUNK_MERGE: '/api/chunk_merge',
    OBJECT_INIT: '/api/object/init',
    OBJECT_PART: '/api/object/part',
    OBJECT_STATUS: '/api/object/status',
    OBJECT_COMMIT: '/api/object/commit',
    OBJECT_ABORT: '/api/object/abort',
    OBJECT_DELETE: '/api/object/delete',
    OBJECT_DOWNLOAD: '/api/object/download',
    OBJECT_SHARE_DOWNLOAD: '/api/object/share-download',
    AI: '/api/ai'
  },
  CHUNK_SIZE: 10 * 1024 * 1024,  // 10MB per chunk
  CHUNK_THRESHOLD: 10 * 1024 * 1024,  // files > 10MB use chunked upload
  CHUNK_UPLOAD: {
    MODE: process.env.REACT_APP_HYDRA_UPLOAD_MODE || 'adaptive',
    INITIAL_CONCURRENCY: Number(process.env.REACT_APP_HYDRA_UPLOAD_INITIAL_CONCURRENCY || 8),
    MIN_CONCURRENCY: Number(process.env.REACT_APP_HYDRA_UPLOAD_MIN_CONCURRENCY || 4),
    MAX_CONCURRENCY: Number(process.env.REACT_APP_HYDRA_UPLOAD_MAX_CONCURRENCY || 16),
    TIMEOUT_MS: 30000,
    MAX_RETRIES: 3,
    RETRY_BASE_MS: 200
  }
};
