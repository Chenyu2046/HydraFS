import { API_CONFIG } from '../config';
import { uploadChunked } from './images';

jest.mock('spark-md5', () => ({
  __esModule: true,
  default: {
    ArrayBuffer: function MockSparkMD5ArrayBuffer() {
      this.append = () => {};
      this.end = () => 'mock-md5';
    }
  }
}));

class MockFileReader {
  readAsArrayBuffer() {
    this.onload({ target: { result: new ArrayBuffer(1) } });
  }
}

const jsonResponse = (data) => ({
  json: () => Promise.resolve(data)
});

const makeFile = (size) => ({
  name: 'large.png',
  size,
  slice: jest.fn((start, end) => ({ start, end }))
});

const deferred = () => {
  let resolve;
  let reject;
  const promise = new Promise((res, rej) => {
    resolve = res;
    reject = rej;
  });
  return { promise, resolve, reject };
};

const flushPromises = async () => {
  for (let i = 0; i < 6; i++) {
    await Promise.resolve();
  }
};

describe('uploadChunked AIMD scheduler', () => {
  const originalChunkSize = API_CONFIG.CHUNK_SIZE;
  const originalChunkUpload = API_CONFIG.CHUNK_UPLOAD;

  beforeEach(() => {
    global.FileReader = MockFileReader;
    API_CONFIG.CHUNK_SIZE = 1;
    API_CONFIG.CHUNK_UPLOAD = {
      INITIAL_CONCURRENCY: 4,
      MIN_CONCURRENCY: 4,
      MAX_CONCURRENCY: 32,
      TIMEOUT_MS: 1000,
      MAX_RETRIES: 3
    };
    jest.clearAllMocks();
  });

  afterEach(() => {
    jest.useRealTimers();
    API_CONFIG.CHUNK_SIZE = originalChunkSize;
    API_CONFIG.CHUNK_UPLOAD = originalChunkUpload;
    delete global.fetch;
  });

  test('starts with four chunk uploads and grows concurrency after healthy successes', async () => {
    let activeUploads = 0;
    let maxActiveUploads = 0;
    let sawInitialFour = false;
    let exceededInitialBeforeFirstCompletion = false;
    let firstUploadCompleted = false;
    const progress = jest.fn();

    global.fetch = jest.fn((url) => {
      if (url === '/api/md5') {
        return Promise.resolve(jsonResponse({ code: 1 }));
      }
      if (url === '/api/chunk_init') {
        return Promise.resolve(jsonResponse({ code: 0, uploadedChunks: '' }));
      }
      if (url.startsWith('/api/chunk_upload')) {
        activeUploads++;
        maxActiveUploads = Math.max(maxActiveUploads, activeUploads);
        if (!firstUploadCompleted && activeUploads === 4) {
          sawInitialFour = true;
        }
        if (!firstUploadCompleted && activeUploads > 4) {
          exceededInitialBeforeFirstCompletion = true;
        }
        return new Promise(resolve => {
          setTimeout(() => {
            firstUploadCompleted = true;
            activeUploads--;
            resolve(jsonResponse({ code: 0 }));
          }, 5);
        });
      }
      if (url === '/api/chunk_merge') {
        return Promise.resolve(jsonResponse({ code: 0, url: '/large.png' }));
      }
      throw new Error(`unexpected url ${url}`);
    });

    const result = await uploadChunked(makeFile(12), { username: 'u', token: 't' }, progress);

    expect(result).toMatchObject({ instant: false, alreadyExists: false, md5: 'mock-md5' });
    expect(sawInitialFour).toBe(true);
    expect(exceededInitialBeforeFirstCompletion).toBe(false);
    expect(maxActiveUploads).toBeGreaterThan(4);
    expect(progress).toHaveBeenNthCalledWith(1, 0);
    expect(progress).toHaveBeenLastCalledWith(100);
    expect(progress).toHaveBeenCalledWith(90);
  });

  test('retries a failed chunk without counting progress until the chunk succeeds', async () => {
    const attemptsByIndex = new Map();
    const progress = jest.fn();

    global.fetch = jest.fn((url) => {
      if (url === '/api/md5') {
        return Promise.resolve(jsonResponse({ code: 1 }));
      }
      if (url === '/api/chunk_init') {
        return Promise.resolve(jsonResponse({ code: 0, uploadedChunks: '' }));
      }
      if (url.startsWith('/api/chunk_upload')) {
        const index = Number(new URL(`http://test${url}`).searchParams.get('index'));
        const attempts = (attemptsByIndex.get(index) || 0) + 1;
        attemptsByIndex.set(index, attempts);
        if (index === 2 && attempts === 1) {
          return Promise.resolve(jsonResponse({ code: 500, msg: 'temporary failure' }));
        }
        return Promise.resolve(jsonResponse({ code: 0 }));
      }
      if (url === '/api/chunk_merge') {
        return Promise.resolve(jsonResponse({ code: 0 }));
      }
      throw new Error(`unexpected url ${url}`);
    });

    await uploadChunked(makeFile(6), { username: 'u', token: 't' }, progress);

    expect(attemptsByIndex.get(2)).toBe(2);
    expect(progress).toHaveBeenCalledWith(90);
    expect(progress).toHaveBeenLastCalledWith(100);
    expect(progress.mock.calls.filter(([value]) => value === 45)).toHaveLength(1);
  });

  test('retries timed out chunk uploads up to the configured max retries', async () => {
    jest.useFakeTimers();
    API_CONFIG.CHUNK_UPLOAD = {
      ...API_CONFIG.CHUNK_UPLOAD,
      TIMEOUT_MS: 10,
      MAX_RETRIES: 1
    };
    const uploadSignals = [];

    global.fetch = jest.fn((url, options = {}) => {
      if (url === '/api/md5') {
        return Promise.resolve(jsonResponse({ code: 1 }));
      }
      if (url === '/api/chunk_init') {
        return Promise.resolve(jsonResponse({ code: 0, uploadedChunks: '' }));
      }
      if (url.startsWith('/api/chunk_upload')) {
        uploadSignals.push(options.signal);
        return new Promise((resolve, reject) => {
          options.signal.addEventListener('abort', () => {
            const error = new Error('aborted');
            error.name = 'AbortError';
            reject(error);
          }, { once: true });
        });
      }
      if (url === '/api/chunk_merge') {
        return Promise.resolve(jsonResponse({ code: 0 }));
      }
      throw new Error(`unexpected url ${url}`);
    });

    const uploadPromise = uploadChunked(makeFile(1), { username: 'u', token: 't' });

    await flushPromises();
    expect(uploadSignals).toHaveLength(1);
    jest.advanceTimersByTime(10);
    await flushPromises();
    expect(uploadSignals).toHaveLength(2);
    jest.advanceTimersByTime(10);

    await expect(uploadPromise).rejects.toHaveProperty('name', 'AbortError');
    expect(uploadSignals).toHaveLength(2);
    expect(global.fetch).not.toHaveBeenCalledWith('/api/chunk_merge', expect.anything());
  });

  test('stops after max retries and does not merge failed chunks', async () => {
    API_CONFIG.CHUNK_UPLOAD = {
      ...API_CONFIG.CHUNK_UPLOAD,
      MAX_RETRIES: 2
    };
    let uploadAttempts = 0;

    global.fetch = jest.fn((url) => {
      if (url === '/api/md5') {
        return Promise.resolve(jsonResponse({ code: 1 }));
      }
      if (url === '/api/chunk_init') {
        return Promise.resolve(jsonResponse({ code: 0, uploadedChunks: '' }));
      }
      if (url.startsWith('/api/chunk_upload')) {
        uploadAttempts++;
        return Promise.resolve(jsonResponse({ code: 500, msg: 'still failing' }));
      }
      if (url === '/api/chunk_merge') {
        return Promise.resolve(jsonResponse({ code: 0 }));
      }
      throw new Error(`unexpected url ${url}`);
    });

    await expect(uploadChunked(makeFile(1), { username: 'u', token: 't' }))
      .rejects.toThrow('分片 0 上传失败');

    expect(uploadAttempts).toBe(3);
    expect(global.fetch).not.toHaveBeenCalledWith('/api/chunk_merge', expect.anything());
  });

  test('aborts in-flight sibling chunk requests when one chunk fails terminally', async () => {
    API_CONFIG.CHUNK_UPLOAD = {
      ...API_CONFIG.CHUNK_UPLOAD,
      MAX_RETRIES: 0
    };
    const siblingSignals = [];

    global.fetch = jest.fn((url, options = {}) => {
      if (url === '/api/md5') {
        return Promise.resolve(jsonResponse({ code: 1 }));
      }
      if (url === '/api/chunk_init') {
        return Promise.resolve(jsonResponse({ code: 0, uploadedChunks: '' }));
      }
      if (url.startsWith('/api/chunk_upload')) {
        const index = Number(new URL(`http://test${url}`).searchParams.get('index'));
        if (index === 0) {
          return Promise.resolve(jsonResponse({ code: 500, msg: 'terminal failure' }));
        }
        siblingSignals.push(options.signal);
        return new Promise((resolve, reject) => {
          options.signal.addEventListener('abort', () => {
            const error = new Error('aborted');
            error.name = 'AbortError';
            reject(error);
          }, { once: true });
        });
      }
      if (url === '/api/chunk_merge') {
        return Promise.resolve(jsonResponse({ code: 0 }));
      }
      throw new Error(`unexpected url ${url}`);
    });

    await expect(uploadChunked(makeFile(4), { username: 'u', token: 't' }))
      .rejects.toThrow('分片 0 上传失败');

    expect(siblingSignals).toHaveLength(3);
    siblingSignals.forEach(signal => expect(signal.aborted).toBe(true));
    expect(global.fetch).not.toHaveBeenCalledWith('/api/chunk_merge', expect.anything());
  });

  test('requests merge only after every chunk has succeeded', async () => {
    const chunkRequests = new Map();
    let mergeCalled = false;

    global.fetch = jest.fn((url) => {
      if (url === '/api/md5') {
        return Promise.resolve(jsonResponse({ code: 1 }));
      }
      if (url === '/api/chunk_init') {
        return Promise.resolve(jsonResponse({ code: 0, uploadedChunks: '' }));
      }
      if (url.startsWith('/api/chunk_upload')) {
        const index = Number(new URL(`http://test${url}`).searchParams.get('index'));
        const request = deferred();
        chunkRequests.set(index, request);
        return request.promise;
      }
      if (url === '/api/chunk_merge') {
        mergeCalled = true;
        return Promise.resolve(jsonResponse({ code: 0 }));
      }
      throw new Error(`unexpected url ${url}`);
    });

    const uploadPromise = uploadChunked(makeFile(4), { username: 'u', token: 't' });
    await flushPromises();

    expect(chunkRequests.size).toBe(4);
    chunkRequests.get(0).resolve(jsonResponse({ code: 0 }));
    chunkRequests.get(1).resolve(jsonResponse({ code: 0 }));
    chunkRequests.get(2).resolve(jsonResponse({ code: 0 }));
    await flushPromises();
    expect(mergeCalled).toBe(false);

    chunkRequests.get(3).resolve(jsonResponse({ code: 0 }));
    await expect(uploadPromise).resolves.toMatchObject({ instant: false, alreadyExists: false });
    expect(mergeCalled).toBe(true);
  });
});
