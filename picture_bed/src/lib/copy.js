/**
 * HydraFS Microcopy 字典
 *
 * 原则：
 *  1. 用"你/我"对话感（你 = 用户，我 = 产品）
 *  2. 失败给"下一步"而不是只说"失败"
 *  3. 空状态用"邀请"语气而非"通知"语气
 *  4. 长度克制，一句话能说清就不堆两句
 *
 * 用法：import { copy } from '../lib/copy';  → copy.empty.files
 */

export const copy = {
  // ===== 加载 =====
  loading: {
    default:    '正在为你整理…',
    indexing:   '正在为你建立索引…',
    searching:  '正在搜索你的知识…',
    uploading:  '正在送进来…',
    analyzing:  'AI 正在理解这个文件…',
  },

  // ===== 空状态 =====
  empty: {
    files:        '这里还很安静 — 上传一个文件试试？',
    images:       '还没有图片，拖一张进来吧',
    knowledge:    '还没有知识节点 — 上传一个 Markdown 或 PDF 试试？',
    graph:        '你的知识地图还在形成… 上传几个相关的文件，看它们如何彼此找到对方。',
    shared:       '还没有共享文件',
    sharedSearch: '没有匹配的，换个说法试试？',
    searchResult: '没找到相关的，换个说法试试？',
    nodes:        '暂无节点',
    backlinks:    '这个节点还没有被引用',
  },

  // ===== 上传 =====
  upload: {
    success:    '已经放进你的工作台 ✓',
    fail:       '这个文件没能进来，要不要再试一次？',
    dragHint:   '把文件放到这里 — 我来帮你理解它',
    chunking:   '文件较大，正在切片上传…',
    merging:    '正在合并分片…',
    instant:    '秒传命中，文件已就位 ✓',
  },

  // ===== 删除 / 分享 =====
  action: {
    deleteSuccess:  '已移除',
    deleteFail:     '删除没能完成，请重试',
    shareSuccess:   '已分享，链接已就绪',
    shareFail:      '分享失败，请稍后再试',
    cancelShared:   '已取消分享',
    saveSuccess:    '已转存到你的空间',
    saveExists:     '你的空间里已经有这个文件了',
    saveFail:       '转存失败，请稍后再试',
  },

  // ===== 鉴权 =====
  auth: {
    expired:      '登录状态过期了，重新登录一下吧',
    loginSuccess: '欢迎回来',
    loginFail:    '用户名或密码不对，再确认一下？',
    regSuccess:   '注册成功，请登录开始使用',
    regUserExists:'用户名已被占用，换一个？',
    regNickExists:'昵称已被占用，换一个？',
    networkFail:  '网络好像不太通畅，稍后再试',
  },

  // ===== 搜索 =====
  search: {
    placeholder:        '描述你想找的，比如"上周的会议纪要"',
    placeholderShort:   '搜索…',
    placeholderFiles:   '按文件名搜索…',
    placeholderNodes:   '筛选节点…',
    needKey:            '还没接入 AI — 去配置一个 Provider？',
    emptyInput:         '先输入想搜的内容',
    aiFail:             '这次没搜到，换个说法试试？',
  },

  // ===== Provider / AI =====
  provider: {
    emptyKey:    '还没接入 AI — 填一个 Key 开始？',
    keySaved:    'API Key 已保存',
    keyCleared:  '已清除',
    keyFail:     '保存失败，请重试',
    rebuildDone: (ok, total) => `已重新理解 ${ok}/${total} 个文件`,
    rebuildFail: '这次没能全部完成，下次再试',
    processFail: '这次没能理解这个文件，可能是格式不支持',
    notParseable:'这个格式暂时还看不懂',
  },

  // ===== Graph =====
  graph: {
    hint:        '拖动节点 · 滚轮缩放 · 点击查看详情',
    selectHint:  '点一个节点开始探索，或在右侧列表里挑一个',
    relations:   '关系链路正在形成中',
    demoData:    '示例数据',
  },

  // ===== 通用 =====
  generic: {
    refresh:    '刷新',
    retry:      '再试一次',
    cancel:     '取消',
    confirm:    '确定',
    save:       '保存',
    saved:      '已保存',
    edit:       '编辑',
    delete:     '删除',
    share:      '分享',
    download:   '下载',
    open:       '打开',
    viewAll:    '查看全部',
    more:       '更多',
    backToHome: '回到首页',
  },
};

export default copy;
