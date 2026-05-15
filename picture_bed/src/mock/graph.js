/**
 * 视觉兜底用的 mock 知识图谱数据
 * 仅在真实接口为空 / 失败时作为 fallback 渲染，不污染任何真实接口调用
 */

const FILE_TYPES = {
  doc: { color: 'graphDoc', label: 'Document' },
  image: { color: 'graphImage', label: 'Image' },
  code: { color: 'graphCode', label: 'Code' },
  archive: { color: 'graphArchive', label: 'Archive' },
  other: { color: 'graphOther', label: 'Other' },
};

export const MOCK_NODES = [
  { id: 'n1',  label: 'Distributed Systems.pdf',     type: 'doc',     tags: ['paxos', 'raft'] },
  { id: 'n2',  label: 'FastDFS Architecture.md',     type: 'doc',     tags: ['fastdfs', 'storage'] },
  { id: 'n3',  label: 'Vector Search Notes.md',      type: 'doc',     tags: ['faiss', 'embedding'] },
  { id: 'n4',  label: 'Embedding Models.png',        type: 'image',   tags: ['embedding', 'llm'] },
  { id: 'n5',  label: 'graph_traversal.cpp',         type: 'code',    tags: ['graph', 'algo'] },
  { id: 'n6',  label: 'consistent_hashing.go',       type: 'code',    tags: ['storage', 'hash'] },
  { id: 'n7',  label: 'Architecture.zip',            type: 'archive', tags: ['arch'] },
  { id: 'n8',  label: 'AI Pipeline Design.md',       type: 'doc',     tags: ['ai', 'pipeline', 'embedding'] },
  { id: 'n9',  label: 'Knowledge Graph Theory.pdf',  type: 'doc',     tags: ['graph', 'knowledge'] },
  { id: 'n10', label: 'Cluster Diagram.png',         type: 'image',   tags: ['storage', 'arch'] },
  { id: 'n11', label: 'README.md',                   type: 'doc',     tags: ['intro'] },
  { id: 'n12', label: 'Bi-directional Links.md',     type: 'doc',     tags: ['knowledge', 'obsidian'] },
];

export const MOCK_EDGES = [
  { source: 'n1', target: 'n2' },
  { source: 'n1', target: 'n6' },
  { source: 'n2', target: 'n6' },
  { source: 'n2', target: 'n10' },
  { source: 'n3', target: 'n4' },
  { source: 'n3', target: 'n8' },
  { source: 'n4', target: 'n8' },
  { source: 'n5', target: 'n9' },
  { source: 'n8', target: 'n9' },
  { source: 'n8', target: 'n12' },
  { source: 'n9', target: 'n12' },
  { source: 'n11', target: 'n2' },
  { source: 'n11', target: 'n8' },
  { source: 'n7', target: 'n10' },
  { source: 'n7', target: 'n2' },
];

export const MOCK_GRAPH = { nodes: MOCK_NODES, links: MOCK_EDGES };

export const MOCK_STATS = {
  nodes: 12, edges: 15, files: 28, storageGB: 4.7, aiTasks: 6, sharedFiles: 5,
};

export const MOCK_RECENT_NODES = [
  { md5: 'mock1', title: 'AI Pipeline Design',        tags: ['ai', 'pipeline'],     time: '3m ago',  summary: 'Embedding 模型选择与 Faiss 向量索引构建路径。' },
  { md5: 'mock2', title: 'FastDFS Architecture',      tags: ['fastdfs', 'storage'], time: '1h ago',  summary: '追踪 tracker / storage 之间的协议与文件分布策略。' },
  { md5: 'mock3', title: 'Bi-directional Links',      tags: ['knowledge'],          time: '5h ago',  summary: '基于关键词与摘要构建文件级反向链接网络。' },
  { md5: 'mock4', title: 'Knowledge Graph Theory',    tags: ['graph'],              time: 'yesterday', summary: '节点 / 边 / 度数 / 中心性在文件知识图谱中的对应。' },
];

export const MOCK_AI_PIPELINE = [
  { name: 'distributed_systems.pdf', status: 'done',      ext: 'pdf' },
  { name: 'cluster_diagram.png',     status: 'embedding', ext: 'png' },
  { name: 'pipeline_design.md',      status: 'queued',    ext: 'md'  },
  { name: 'README.md',               status: 'done',      ext: 'md'  },
];

export { FILE_TYPES };

/**
 * 把真实 files[] 转成图谱数据。
 * 当前后端尚未提供节点之间的真实关系（tags / wiki backlinks），
 * 因此只返回节点，links 留空 —— 不再合成"看起来像但其实是随机"的边。
 * 真实关系上线后，在此处替换为基于后端响应的边构造逻辑。
 * 文件不可用时返回 null，由调用方决定是否回退到 MOCK_GRAPH。
 */
export const buildGraphFromFiles = (files) => {
  if (!Array.isArray(files) || files.length === 0) return null;
  const nodes = files.slice(0, 80).map(f => ({
    id: f.md5,
    label: f.file_name || f.name || f.md5,
    type: classifyFileType(f.type),
    tags: [],
  }));
  return { nodes, links: [] };
};

export const classifyFileType = (ext) => {
  if (!ext) return 'other';
  const e = String(ext).toLowerCase();
  if (['png','jpg','jpeg','gif','bmp','webp','svg','ico'].includes(e)) return 'image';
  if (['pdf','doc','docx','md','txt','ppt','pptx','xls','xlsx','log'].includes(e)) return 'doc';
  if (['c','cpp','h','hpp','js','jsx','ts','tsx','py','go','rs','java','sh'].includes(e)) return 'code';
  if (['zip','tar','gz','rar','7z'].includes(e)) return 'archive';
  return 'other';
};
