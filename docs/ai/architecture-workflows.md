# HydraStore｜当前架构与核心 Workflow

> 这是基于当前仓库源码、Compose、SQL migration 和前端服务层生成的架构快照。
> 生成基线：`main@807c23bc833f2ec8dd0b7a33fbff27359dbeb7a3`（2026-09-06）。
> 这份图以当前实现为准；旧版 `chunked_upload.md` 中的 Appender 合并描述属于兼容链路，不是当前前端大文件主路径。

## 怎么读

- 实线表示当前主数据流；虚线表示异步触发、兼容路径或状态回写。
- V2 的逻辑对象由 MySQL manifest 管理，FastDFS 只负责 Blob 的物理读写。
- MySQL 是 upload session、manifest、chunk、reference、AI generation 和 lifecycle 的权威状态源；Redis 只承担 token、legacy 上传协同和 V2 breaker 等临时/协调状态。
- “已发布”是可见性边界：物理 Blob 先成功，再由元数据把 part/manifest/index/evidence 标为可用。

## 1. 总体架构与请求入口

```mermaid
flowchart LR
    %% 中文注释：浏览器只访问 Nginx，Nginx 根据 URL 将请求分流到兼容链路或 V2 网关。
    %% 中文注释：MySQL 保存逻辑状态，FastDFS 保存物理 Blob，Redis 只承担协调/临时状态。
    U["用户浏览器\nReact 18 + Ant Design"]

    subgraph NOTE["中文读图注释"]
        NOTE1["入口注释：所有外部请求先经过 Nginx"]
        NOTE2["一致性注释：逻辑对象看 MySQL，物理数据看 FastDFS"]
    end

    subgraph EDGE["边缘与网关层"]
        N["Nginx\nHTTPS 443 / HTTP 80 redirect\n静态 React + FastDFS module"]
        LB["hydrastore_gateways\nleast_conn :10020\nwrite buffering on\nread streaming off"]
    end

    subgraph APP["业务与异步执行层"]
        L["fastcgi_app\nlegacy CGI :10000-10012\nlogin/upload/share/chunk/ai"]
        G1["storage_gateway_1\n16 FastCGI workers"]
        G2["storage_gateway_2\n16 FastCGI workers"]
        KW["knowledge_worker × 4\nparse / compile / repair / delete"]
        IW["knowledge_index_worker × 1\nper-user FAISS snapshot"]
        GC["storage_gc_worker\nreconcile + delete retry"]
    end

    subgraph DATA["权威数据与物理存储"]
        DB["MySQL 8\nlegacy tables + HydraStore V2\nAI V2 tables"]
        R["Redis 7\ntoken / coordination / breaker"]
        T["FastDFS Tracker\ninside nginx_fastdfs"]
        S1["FastDFS Storage 1\ninside nginx_fastdfs"]
        S2["FastDFS Storage 2\nfastdfs_storage_2"]
        F["FAISS\nper-user immutable files\n/data/faiss/users"]
    end

    X["DashScope API\nembedding / Qwen-VL / text generation"]
    M["db_migrate\nhydra_v2.sql + hydra_ai_v2.sql\nchecksum health gate"]

    U -->|HTTPS| N
    N -->|static /| U
    N -->|/group*/M*| T
    N -->|legacy /api/*| L
    N -->|/api/object/*| LB
    LB --> G1
    LB --> G2

    L --> DB
    L --> R
    L --> T
    L --> X
    L --> F

    G1 --> DB
    G1 --> R
    G1 --> T
    G2 --> DB
    G2 --> R
    G2 --> T
    T --> S1
    T --> S2

    KW --> DB
    KW -->|manifest read / legacy URL| T
    KW --> X
    KW --> F
    IW --> DB
    IW --> F
    GC --> DB
    GC --> T
    M -->|depends on healthy MySQL| DB
    M -.->|migration ready| L
    M -.->|migration ready| G1
    M -.->|migration ready| G2
    M -.->|migration ready| KW
    M -.->|migration ready| GC

    classDef user fill:#eaf2ff,stroke:#3972d6,color:#11264d;
    classDef edge fill:#fff4df,stroke:#c98719,color:#4a2b00;
    classDef app fill:#edf8f0,stroke:#3b9561,color:#123a22;
    classDef data fill:#f5efff,stroke:#8256c7,color:#28114c;
    classDef external fill:#ffecef,stroke:#c74662,color:#4a1220;
    classDef note fill:#f8fafc,stroke:#94a3b8,color:#334155;
    class U user;
    class N,LB edge;
    class L,G1,G2,KW,IW,GC,M app;
    class DB,R,T,S1,S2,F data;
    class X external;
    class NOTE1,NOTE2 note;
```

### 当前边界

| 边界 | 当前职责 | 代码证据 |
| --- | --- | --- |
| Nginx | HTTPS、静态资源、legacy FastCGI 路由、V2 Gateway upstream、FastDFS 直读 | `docker/nginx_fastdfs/nginx.conf:24-42,63-80,82-360` |
| legacy FastCGI | 登录、普通上传、兼容 `chunk_*`、分享、AI HTTP 入口 | `docker/fastcgi_app/start.sh:49-91` |
| V2 Gateway | object init/part/status/commit/abort/delete/download/share-download | `src_cgi/storage_gateway.cpp:367-658` |
| MySQL | 业务关系、V2 session/manifest/chunk、AI task/evidence/wiki/index 状态 | `docker/mysql/init.sql:85-162`; `docker/mysql/hydra_ai_v2.sql:3-138` |
| FastDFS | BlobStore 的物理 `Put/Get/Delete`；不持有逻辑对象模型 | `common/storage_blob_store.cpp:66-130`; `include/storage_types.h:79-87` |
| AI worker | 解析、证据生成、Wiki 编译/修复；不阻塞上传响应 | `src_cgi/knowledge_worker.cpp:169-373` |

## 2. 用户上传总流程：小文件 legacy，大文件 V2

```mermaid
flowchart TD
    %% 中文注释：10 MiB 是当前前端 uploadImage 的分流阈值；大文件进入 manifest + chunk_blob 模型。
    A["FileList / QuickUpload\nuploadImage(file, user)"] --> B{"文件大小 > 10 MiB?"}

    subgraph NOTE["上传读图注释"]
        NOTE1["小文件：沿用 legacy file_info/file_id 兼容路径"]
        NOTE2["大文件：分片物理发布后，再提交逻辑 manifest"]
    end

    B -->|否| C["计算 MD5"]
    C --> D["POST /api/md5\n秒传检测"]
    D -->|命中当前用户已有关系| E["复用 file_info\n写 user_file_list\n返回 instant"]
    D -->|未命中| F["POST /api/upload\nmultipart"]
    F --> G["legacy upload CGI\nFastDFS whole-file Put\nfile_info + user relation"]

    B -->|是| H["V2 单次扫描\ncontent MD5 + per-part SHA-256"]
    H --> I["POST /api/object/init"]
    I --> J{"当前用户已拥有同 digest manifest?"}
    J -->|是| K["返回 instant=true\n复用 object/manifest"]
    J -->|否| L["创建/恢复 upload_session\n声明 upload_part\n按 (sha256,size) 绑定 chunk_blob"]
    L --> M["循环查询 object/status"]
    M --> N["AIMD 并发 POST /api/object/part\n超时/429/503 有界重试"]
    N --> O["所有 part READY?"]
    O -->|否，WAITING| M
    O -->|否，UPLOADABLE/MISSING| N
    O -->|是| P["POST /api/object/commit"]
    P --> Q["单事务提交 manifest\n递增 chunk ref_count\nfile_info + user_file_list\n入队 parse_source"]

    E --> R["前端展示已完成"]
    G --> R
    K --> R
    Q --> R
    G -.-> S["legacy/兼容数据仍可读"]
    Q -.-> T["异步 AI 知识流水线"]

    classDef decision fill:#fff4df,stroke:#c98719;
    classDef v2 fill:#edf8f0,stroke:#3b9561;
    classDef legacy fill:#f2f2f2,stroke:#777;
    classDef note fill:#f8fafc,stroke:#94a3b8,color:#334155;
    class B,J,O decision;
    class H,I,L,M,N,P,Q,T v2;
    class C,D,E,F,G,S legacy;
    class NOTE1,NOTE2 note;
```

### 大文件 V2 part workflow（并发安全重点）

```mermaid
sequenceDiagram
    autonumber
    %% 中文注释：part 只有在物理写入、哈希校验、租约 fencing 全部通过后才能变成 READY。
    participant UI as React images.js
    participant N as Nginx
    participant GW as Gateway worker
    participant DB as MySQL MetadataStore
    participant R as Redis breaker
    participant DFS as FastDFS BlobStore

    UI->>N: POST /api/object/init {digest, parts}
    N->>GW: FastCGI 10020
    GW->>DB: InitOrResume transaction
    DB->>DB: (sha256,size) unique lookup\n复用 READY / 取得 active lease
    DB-->>GW: uploadId + part availability
    GW-->>UI: uploadable / waiting / reused
    Note over UI,DB: 中文注释：init 只登记上传计划并返回可复用/可上传分片，不代表对象已经提交

    loop 每个待上传 part（AIMD window）
        UI->>N: POST /api/object/part?uploadId&index&sha256
        N->>GW: bounded body + upload headers
        GW->>R: Allow() / shared breaker state
        alt overload or breaker open
            GW-->>UI: 429 / 503 + retry-after
        else admitted
            GW->>DB: ClaimPartUpload(... FOR UPDATE)
            alt other owner active
                DB-->>GW: claim denied
                GW-->>UI: PART_BUSY
                UI->>N: POST /api/object/status
                N->>GW: reread part state
            else lease granted
                GW->>DFS: streaming Put(body)
                DFS-->>GW: backend_file_id
                GW->>GW: verify bytes + SHA-256
                alt payload mismatch
                    GW->>DB: MarkPartFailed(... empty backend id)
                    GW->>DFS: best-effort Delete(untrusted blob)
                    GW-->>UI: error
                else physical blob valid
                    GW->>DB: RecordPartBackend(... owner + lease_epoch)
                    GW->>DB: MarkPartReady(... owner + lease_epoch)
                    GW-->>UI: success
                end
            end
        end
        UI->>UI: record RTT/failure/timeout\nAIMD cwnd decrease or increase
    end

    Note over GW,DFS: 中文注释：FastDFS 先保存物理 Blob，Gateway 再用字节数和 SHA-256 做完整性确认

    UI->>N: POST /api/object/commit {uploadId}
    N->>GW: FastCGI 10020
    GW->>DB: Commit transaction + FOR UPDATE
    DB->>DB: all parts READY → manifest_chunk\nref_count++ → relation → parse task
    DB-->>GW: COMMITTED objectId / manifestId
    GW-->>UI: same result on repeated commit
    Note over UI,DB: 中文注释：commit 是一次 MySQL 事务；重复 commit 返回同一个 objectId，不重复增加引用
```

### V2 的关键不变量

1. `chunk_blob` 的唯一键 `(sha256, size)` 是物理分片去重边界；`owner_upload_id + lease_epoch` 是上传租约 fencing 边界。
2. FastDFS 物理 `Put` 成功且请求体 SHA-256 校验通过后，才允许 `RecordPartBackend`/`MarkPartReady`。
3. `Commit` 只接受所有 `upload_part.state=READY` 的 session；manifest、引用计数、用户关系和异步 `parse_source` 在一个 MySQL 事务内落地。
4. 重复 commit 对已经 `COMMITTED` 的 session 返回同一个 `objectId`，不重复增加引用。
5. 前端当前默认 AIMD 窗口为 `4..16`、初始 `8`；每个 part 有超时和最多 3 次重试。上限可由前端构建配置覆盖。

## 3. 下载、分享、删除与 GC 生命周期

```mermaid
flowchart LR
    %% 中文注释：下载是按 manifest 顺序读取 chunk；删除先处理逻辑引用，物理删除交给延迟 GC。
    A["私有下载\nGET /api/object/download?objectId"] --> B["Gateway 鉴权\nMetadataStore.GetManifest"]
    C["分享下载\nGET /api/object/share-download?shareToken"] --> D["Gateway 解析 share_file_list\n获取 manifest"]
    B --> E["按 manifest_chunk.part_index 排序"]
    D --> E
    E --> F["每个 READY chunk\nBlobStore.Get"]
    F --> G["FastDFS Tracker → Storage\n流式写回 FCGI stdout"]
    G --> H["浏览器下载文件"]
    L["legacy file_info.file_id/url"] -.->|仍可读| H

    I["POST /api/object/delete"] --> J["DeleteObjectForUser\nMySQL transaction"]
    J --> K{"是否仍有其他用户引用?"}
    K -->|是| M["仅删除当前 user_file_list\nfile_info.count--\n物理 chunk 保留"]
    K -->|否| N["chunk ref_count--\nGC_PENDING + grace period\n删除 manifest/file metadata"]
    J -.-> O["enqueue delete_source"]
    N --> P["storage_gc_worker\nReconcileExpiredUploads"]
    P --> Q{"ref_count=0 且无活跃上传\n且 gc_after 到期?"}
    Q -->|否| R["留在 GC_PENDING"]
    Q -->|是| S["ClaimGc: DELETING"]
    S --> T["FastDFS Delete"]
    T -->|成功/ENOENT| U["FinishGc\n清理 upload_part + chunk_blob"]
    T -->|失败| V["回到 GC_PENDING\n指数退避 + last_error"]

    NOTE["中文注释：共享引用仍存在时只删当前用户关系；只有零引用且无活跃上传才允许 DELETING"]

    classDef read fill:#eaf2ff,stroke:#3972d6;
    classDef write fill:#fff4df,stroke:#c98719;
    classDef gc fill:#f5efff,stroke:#8256c7;
    classDef note fill:#f8fafc,stroke:#94a3b8,color:#334155;
    class A,B,C,D,E,F,G,H,L read;
    class I,J,K,M,N,O write;
    class P,Q,R,S,T,U,V gc;
    class NOTE note;
```

### Chunk 生命周期状态机

```mermaid
stateDiagram-v2
    %% 中文注释：READY 是可被 manifest 引用的可见状态；DELETING 是物理删除中的不可回退窗口。
    [*] --> MISSING
    MISSING --> UPLOADING: 申请上传租约\nowner + lease_epoch
    UPLOADING --> READY: 物理 Put + 哈希校验通过\n记录 backend + 标记 READY
    UPLOADING --> GC_PENDING: 上传失败或租约丢失\n可恢复时保留 backend id
    FAILED --> UPLOADING: 重试且 ref_count=0
    GC_PENDING --> UPLOADING: GC 前被新上传重新认领
    GC_PENDING --> DELETING: grace period 到期且无活跃引用/上传
    DELETING --> [*]: FastDFS 删除成功 + DB 清理完成
    DELETING --> GC_PENDING: 物理删除或 DB 清理失败
    READY --> GC_PENDING: 最后一条引用被删除
    note right of READY: 中文注释：只有 READY 分片才能进入 commit
    note right of GC_PENDING: 中文注释：延迟回收，给并发上传和恢复留下窗口
    note right of DELETING: 中文注释：删除失败可回到 GC_PENDING 重试
```

## 4. AI Knowledge Layer：异步证据 → Wiki → 索引

```mermaid
flowchart TD
    %% 中文注释：AI 任务、证据 generation、Wiki revision、FAISS snapshot 都采用“先 staging、后发布”。
    A["对象 Commit 事务\n或 /api/ai?cmd=describe"] -.enqueue.-> B["ai_parse_task\nparse_source"]

    subgraph NOTE["AI 读图注释"]
        NOTE1["上传响应不等待 AI；worker 通过 lease + epoch 防止旧任务覆盖新结果"]
        NOTE2["只有 PUBLISHED generation 对搜索可见，STAGING 失败会被清理"]
    end
    B --> C["knowledge_worker\nClaimTask FOR UPDATE SKIP LOCKED\nworker_id + lease_epoch"]
    C --> D{"任务类型"}

    D -->|parse_source| E["LoadSourceObject\nmanifest → ObjectReader 按 chunk 重组\nlegacy → URL/Blob 读取"]
    E --> F{"文件类型"}
    F -->|图片| G["DashScope Qwen-VL\n生成受限文本描述"]
    F -->|文本/文档| H["document_extractor\n有界提取 + 超时"]
    G --> I["deterministic chunker\n重叠窗口 + bounded evidence"]
    H --> I
    I --> J["DashScope text-embedding\n1024-d normalized vector"]
    J --> K["knowledge_chunk + knowledge_vector\nSTAGING generation"]
    K --> L["PublishEvidenceGeneration transaction\n旧 generation superseded\nnew generation PUBLISHED\nevidence_state=READY"]
    L -.-> M["enqueue compile_wiki"]
    L -.-> N["MarkIndexDirty(user)"]

    D -->|compile_wiki / repair_wiki| O["WikiCompiler\n读取 published evidence + candidates"]
    O --> P["DashScope text generation\nJSON-only patch"]
    P --> Q["validator\n安全 Markdown\n1..4 citations\n只允许 supplied active chunks"]
    Q --> R["PublishWikiPatch transaction\npage lock + base_revision CAS\nSTAGING revision → PUBLISHED\nclaims/citations/links/vector"]
    R -.-> N

    N --> S["knowledge_index_state\nDIRTY generation"]
    S --> T["knowledge_index_worker\nClaimDirtyIndex + lease_epoch"]
    T --> U["LoadActiveVectors(user)"]
    U --> V["FAISS IndexFlatIP / IndexIDMap2\n稳定 label = knowledge_vector.id"]
    V --> W["vectors.<generation>.<lease_epoch>.faiss"]
    W --> X["PublishIndexGeneration\n发布 generation + lease fence"]

    C -.lease renew / expiry recovery.-> Y["RenewTask / RecoverExpiredTasks\n失败重试或 terminal failed/skipped"]
    L -.publish failure.-> Z["AbortEvidenceGeneration\n清理 staging，保留旧 published generation"]

    classDef queue fill:#fff4df,stroke:#c98719;
    classDef worker fill:#edf8f0,stroke:#3b9561;
    classDef store fill:#f5efff,stroke:#8256c7;
    classDef external fill:#ffecef,stroke:#c74662;
    classDef note fill:#f8fafc,stroke:#94a3b8,color:#334155;
    class B,S queue;
    class C,E,H,I,K,O,Q,R,T,U,V,X,Y,Z worker;
    class L,N,W store;
    class G,J,P external;
    class NOTE1,NOTE2 note;
```

### AI 状态与可见性

| 阶段 | 权威状态 | 对外行为 |
| --- | --- | --- |
| 任务排队 | `ai_parse_task.status=pending` | 上传响应不等待 AI；`describe` 返回 `queued` |
| 证据构建 | `knowledge_document.current_generation` 前进，chunk/vector 为 `STAGING` | 旧 `published_generation` 仍可读 |
| 证据发布 | chunk 为 `PUBLISHED`，document `published_generation` 更新、`evidence_state=READY` | 触发 Wiki task 和 per-user index dirty |
| 证据失败 | staging chunk/vector 删除/失活，document `FAILED` | 保留旧 published generation，不把半成品暴露给搜索 |
| 索引构建 | `knowledge_index_state=BUILDING` | 搜索继续读取旧 published snapshot |
| 索引发布 | `published_generation` 与 `published_lease_epoch` 更新 | 新搜索读取新快照 |
| Wiki 并发冲突 | page 的 `current_revision_id` 与 patch base 不一致 | 当前 task 失败/重试，不覆盖新 revision |

## 5. AI 语义搜索：只读已发布索引 + SQL hydration

```mermaid
sequenceDiagram
    autonumber
    participant UI as Home / Knowledge UI
    participant N as Nginx
    participant AI as ai_cgi :10012
    participant DS as DashScope embedding
    participant DB as MySQL KnowledgeStore
    participant F as per-user FAISS snapshot

    UI->>N: POST /api/ai?cmd=search {user, token, query}
    N->>AI: FastCGI
    AI->>DB: token/API-key/source state checks
    AI->>DS: embed(query)
    DS-->>AI: normalized query vector
    AI->>DB: LoadIndexState(user)\n读取 published_generation + lease_epoch
    AI->>F: LoadCachedIndex(user, published snapshot)
    F-->>AI: top-30 stable vector IDs + scores
    AI->>AI: score threshold filter\n按 source type 聚合
    AI->>DB: one read transaction\nLoadSearchHydration(vector IDs)\nLoadWikiClaims(revision IDs)
    DB-->>AI: user-filtered file chunks / Wiki claims / snippets
    AI-->>N: files + matches{chunkId,score,snippet} + wiki
    N-->>UI: JSON result

    Note over AI,DB: 中文注释：search 不 rebuild、不 append、不写索引\n只消费已发布 snapshot，再由 MySQL 做权限和证据校验
```

## 6. 启动与依赖 workflow

```mermaid
flowchart TD
    %% 中文注释：启动顺序先保证 MySQL 和 migration，再启动业务 worker；GC 还依赖 FastDFS 健康。
    A["docker compose up"] --> B["tc_mysql\nMySQL healthy"]
    B --> C["db_migrate\napply hydra_v2 + hydra_ai_v2\nverify checksums"]
    B --> D["nginx_fastdfs\nTracker + Storage 1 + Nginx healthy"]
    C --> E["fastcgi_app\nlegacy CGI + ai + knowledge workers"]
    C --> F["storage_gateway_1 / _2\n16 worker pool each"]
    C --> G["storage_gc_worker"]
    D --> G
    D --> H["fastdfs_storage_2\nsecondary storage healthy"]
    E --> I["HTTP traffic ready"]
    F --> I

    E -.PID supervisor every 2s.-> E
    F -.worker-count watchdog.-> F
    G -.10s GC loop.-> G

    NOTE["中文注释：healthcheck 只代表依赖就绪，不等价于完整业务 E2E 已验证"]
```

## 7. 重点实现与风险边界

- **逻辑对象与物理 Blob 分离**：V2 以 `object_manifest → manifest_chunk → chunk_blob` 表达对象；FastDFS 的 `backend_file_id` 只是物理句柄。
- **MySQL 是一致性边界**：session、part、manifest、引用计数、AI generation、Wiki revision、index generation 均由 MySQL transaction + 行锁/条件更新保护。
- **Redis 不是元数据源**：V2 upload status 从 MySQL 返回；Redis 的 V2 作用主要是跨 Gateway breaker 状态，legacy 路径还保留 token/分片协同。
- **物理先行、元数据后见**：Blob 先写 FastDFS，校验通过后才标记 READY；失败 Blob 由立即清理或 GC 重试兜底。
- **共享引用安全**：删除一个用户的关系不会删除仍被其他用户引用的 chunk；只有 `ref_count=0` 且无活跃上传、经过 grace period 才进入物理删除。
- **AI 不阻塞主上传**：Commit 只在同一事务里入队 `parse_source`；解析、embedding、Wiki、FAISS 都是异步 worker。
- **搜索发布隔离**：搜索只读已发布的 per-user FAISS snapshot，MySQL hydration 再确认用户关系、source 和 citations。

## 8. 相关产物

- [交互式 HTML 图集](../../reports/architecture/hydrastore-current/architecture-workflows.html)
- [离线静态 SVG 图集](../../reports/architecture/hydrastore-current/architecture-workflows.svg)
- [节点与源码证据追踪 JSON](../../reports/architecture/hydrastore-current/architecture-workflows.json)

## 9. 证据索引

| 主题 | 主要源码锚点 |
| --- | --- |
| Compose 服务、健康依赖、双 Gateway、GC、第二存储 | `docker/docker-compose.yaml:4-232` |
| Nginx 路由、V2 upstream、buffering、failover 边界 | `docker/nginx_fastdfs/nginx.conf:24-360` |
| legacy CGI、V2 Gateway、AI worker 启动和 watchdog | `docker/fastcgi_app/start.sh:23-142` |
| 前端文件大小分流、V2 init/status/part/commit、删除/下载 | `picture_bed/src/services/images.js:149-208,444-564,566-790` |
| AIMD 窗口、RTT/失败/超时调节与重试退避 | `picture_bed/src/services/concurrency_controller.mjs:18-120` |
| V2 init、dedup、lease、part ready、commit、delete、GC | `common/storage_metadata_store.cpp:318-865` |
| Gateway part admission、breaker、streaming Put/Get、failpoint | `src_cgi/storage_gateway.cpp:367-658` |
| FastDFS BlobStore Put/Get/Delete | `common/storage_blob_store.cpp:54-143` |
| manifest 重组读取与完整性校验 | `common/storage_object_reader.cpp:81-150` |
| AI task lease、evidence generation、index state | `common/knowledge_store.cpp:172-576` |
| AI parse/embedding/Wiki task worker | `src_cgi/knowledge_worker.cpp:169-373` |
| per-user immutable FAISS build/publish | `src_cgi/knowledge_index_worker.cpp:36-101` |
| AI search、published snapshot、SQL hydration | `src_cgi/ai_cgi.cpp:256-375` |
| AI Wiki citation/CAS/publish/delete repair | `common/knowledge_store.cpp:915-1105`; `common/wiki_compiler.cpp:350-758` |
| V2 storage schema与外键/唯一约束 | `docker/mysql/init.sql:85-162`; `docker/mysql/hydra_v2.sql:12-145` |
| AI V2 schema与generation/index/task扩展 | `docker/mysql/hydra_ai_v2.sql:3-173` |

## 10. 本图未声称的内容

这是一份静态架构图，不等价于当前机器已经完成完整 Linux/Docker E2E 验证。吞吐、P99、真实多 Storage 故障恢复、DashScope smoke 和长时间 lease/GC 压测，仍应以对应测试命令和运行证据为准。
